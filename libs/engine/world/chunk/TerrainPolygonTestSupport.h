#pragma once

// Shared by the TerrainPolygonBuilder tests: hand-built extended tiles, border
// vertex queries, ring-set comparison, and the two-chunk seam check of
// terrain-polygons-architecture.md section 5 (identical border vertices, and a
// nav mesh over both chunks that agrees with each chunk's rings 1 mm either
// side of the border).

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/TerrainPolygonBuilder.h"
#include "world/chunk/TerrainPolygons.h"

#include <nav/NavMesh.h>
#include <nav/PathQuery.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace engine::world::terrain_test {

using geometry::Ring;
using geometry::Vec2i64;

inline constexpr int64_t kChunkMm = static_cast<int64_t>(kChunkSize) * TerrainPolygonBuilder::kTileMm;
inline constexpr int64_t kApronMm = static_cast<int64_t>(kApronTiles) * TerrainPolygonBuilder::kTileMm;

inline TileData tileOf(Biome biome) {
	TileData tile;
	tile.primaryBiome = biome;
	tile.secondaryBiome = biome;
	tile.surface = (isWater(biome) || biome == Biome::TemperateWetland) ? Surface::Water : Surface::Grass;
	return tile;
}

using BiomeAt = std::function<Biome(int64_t tx, int64_t ty)>;

// A hand-built extended region (chunk plus apron), for features smaller than
// the 16-tile biome sectors a sampler can place, plus the world around it for
// the builder's biome water query: the stored tiles where they reach, `beyond`
// elsewhere.
struct HandTiles {
	ChunkCoordinate		  coord;
	int32_t				  apronTiles;
	int32_t				  size;
	BiomeAt				  beyond;
	std::vector<TileData> tiles;

	// Every tile `fill`; edit the stored ones with at().
	HandTiles(ChunkCoordinate newCoord, Biome fill, int32_t newApronTiles = kApronTiles)
		: HandTiles(newCoord, [fill](int64_t, int64_t) { return fill; }, newApronTiles) {}

	// Each tile's biome a function of its world tile coordinate, so neighboring
	// chunks see the same world.
	HandTiles(ChunkCoordinate newCoord, BiomeAt biomeAt, int32_t newApronTiles = kApronTiles)
		: coord(newCoord),
		  apronTiles(newApronTiles),
		  size(kChunkSize + 2 * newApronTiles),
		  beyond(std::move(biomeAt)),
		  tiles(static_cast<size_t>(size) * static_cast<size_t>(size)) {
		for (int32_t ey = 0; ey < size; ++ey) {
			for (int32_t ex = 0; ex < size; ++ex) {
				at(ex, ey) = tileOf(beyond(originX() + ex, originY() + ey));
			}
		}
	}

	[[nodiscard]] int64_t originX() const { return static_cast<int64_t>(coord.x) * kChunkSize - apronTiles; }
	[[nodiscard]] int64_t originY() const { return static_cast<int64_t>(coord.y) * kChunkSize - apronTiles; }

	TileData& at(int32_t ex, int32_t ey) { return tiles[static_cast<size_t>(ey) * static_cast<size_t>(size) + static_cast<size_t>(ex)]; }
	[[nodiscard]] const TileData& at(int32_t ex, int32_t ey) const {
		return tiles[static_cast<size_t>(ey) * static_cast<size_t>(size) + static_cast<size_t>(ex)];
	}

	[[nodiscard]] TerrainPolygonBuilder::ExtendedTileFn fn() const {
		return [this](int32_t ex, int32_t ey) -> const TileData& { return at(ex, ey); };
	}

	[[nodiscard]] TerrainPolygonBuilder::BiomeWaterFn waterFn() const {
		return [this](int64_t tx, int64_t ty) {
			const int64_t ex = tx - originX();
			const int64_t ey = ty - originY();
			if (ex >= 0 && ey >= 0 && ex < size && ey < size) {
				return isBiomeWater(at(static_cast<int32_t>(ex), static_cast<int32_t>(ey)));
			}
			return isBiomeWater(beyond(tx, ty));
		};
	}
};

inline ChunkTerrainPolygons buildHand(
	const HandTiles&									  tiles,
	uint64_t											  worldSeed,
	std::span<const TerrainPolygonBuilder::RiverSegment> segments = {},
	std::span<const TerrainPolygonBuilder::Pond>		  ponds	   = {}
) {
	return TerrainPolygonBuilder::build(tiles.coord, worldSeed, tiles.fn(), tiles.waterFn(), segments, ponds, tiles.apronTiles);
}

inline int64_t absArea2(const Ring& ring) {
	const geometry::Int128 a = geometry::signedAreaDoubled(ring);
	return static_cast<int64_t>(std::llround(std::abs(a.toDouble())));
}

// Every published ring, clipped or not, must be simple.
inline void expectAllSimple(const ChunkTerrainPolygons& polys, const std::string& label) {
	for (size_t i = 0; i < polys.rings.size(); ++i) {
		EXPECT_TRUE(geometry::isSimple(polys.rings[i].ring).pass) << label << " rings[" << i << "]";
		EXPECT_EQ(polys.rings[i].profiles.size(), polys.rings[i].ring.size()) << label << " rings[" << i << "]";
	}
	for (size_t i = 0; i < polys.navRings.size(); ++i) {
		EXPECT_TRUE(geometry::isSimple(polys.navRings[i].ring).pass) << label << " navRings[" << i << "]";
	}
}

// navRings vertices on the line x = line (vertical) or y = line, sorted.
inline std::vector<Vec2i64> navVerticesOnLine(const ChunkTerrainPolygons& polys, bool vertical, int64_t line) {
	std::vector<Vec2i64> out;
	for (const TerrainRing& ring : polys.navRings) {
		for (const Vec2i64& v : ring.ring) {
			if ((vertical ? v.x : v.y) == line) {
				out.push_back(v);
			}
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}

inline bool sameRings(const std::vector<TerrainRing>& a, const std::vector<TerrainRing>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].ring != b[i].ring || a[i].profiles != b[i].profiles || a[i].kind != b[i].kind || a[i].water != b[i].water ||
			a[i].blocksMovement != b[i].blocksMovement || a[i].holeCapable != b[i].holeCapable ||
			a[i].meanHalfWidthM != b[i].meanHalfWidthM) {
			return false;
		}
	}
	return true;
}

inline bool sameThalwegs(const std::vector<ThalwegPath>& a, const std::vector<ThalwegPath>& b) {
	if (a.size() != b.size()) {
		return false;
	}
	for (size_t i = 0; i < a.size(); ++i) {
		if (a[i].points != b[i].points || a[i].halfWidthM != b[i].halfWidthM || a[i].widthRatio != b[i].widthRatio ||
			a[i].curvature != b[i].curvature) {
			return false;
		}
	}
	return true;
}

// Water the way nav classifies it (D9): blocking rings only, even-odd over the
// hole-capable ones (waterline), solid containment for the rest (channel, pond).
inline bool navWater(const std::vector<TerrainRing>& rings, Vec2i64 p, bool& onBoundary) {
	int	 parity = 0;
	bool solid	= false;
	for (const TerrainRing& ring : rings) {
		if (!ring.blocksMovement) {
			continue;
		}
		const geometry::PointInPolygon where = geometry::pointInPolygon(p, ring.ring);
		onBoundary = onBoundary || where == geometry::PointInPolygon::OnBoundary;
		if (where != geometry::PointInPolygon::Inside) {
			continue;
		}
		if (ring.holeCapable) {
			++parity;
		} else {
			solid = true;
		}
	}
	return solid || (parity % 2) == 1;
}

struct SeamSide {
	const ChunkTerrainPolygons* polys;
	Vec2i64 min;
	Vec2i64 max;
};

inline SeamSide seamSide(ChunkCoordinate c, const ChunkTerrainPolygons& polys) {
	const Vec2i64 min{static_cast<int64_t>(c.x) * kChunkMm, static_cast<int64_t>(c.y) * kChunkMm};
	return {&polys, min, {min.x + kChunkMm, min.y + kChunkMm}};
}

inline SeamSide seamChunk(const Chunk& chunk) {
	return seamSide(chunk.coordinate(), chunk.terrainPolygons());
}

// The nav mesh NavInputBuilder would build over these chunks: one unblocked
// border over their bounding rectangle, then every blocking navRing as a
// blocked water polygon (D9).
inline geometry::nav::NavMesh buildSeamMesh(const std::vector<SeamSide>& sides) {
	Vec2i64 lo = sides.front().min;
	Vec2i64 hi = sides.front().max;
	for (const SeamSide& s : sides) {
		lo = {std::min(lo.x, s.min.x), std::min(lo.y, s.min.y)};
		hi = {std::max(hi.x, s.max.x), std::max(hi.y, s.max.y)};
	}
	geometry::nav::NavMeshInput input;
	input.polygons.push_back({{lo, {hi.x, lo.y}, hi, {lo.x, hi.y}}, false, -3});
	for (const SeamSide& s : sides) {
		for (const TerrainRing& ring : s.polys->navRings) {
			if (ring.blocksMovement) {
				input.polygons.push_back({ring.ring, true, -1, geometry::nav::kNoOpening, ring.holeCapable});
			}
		}
	}
	return geometry::nav::buildNavMesh(input);
}

// Along the border between `a` (low side) and `b`, sample 1 mm either side.
// Everywhere: the mesh has a triangle (no gap) and classifies the point the way
// the owning chunk's unclipped rings do. Away from the border vertices (where
// a crossing shoreline legitimately flips within a mm): both sides agree, so
// there is no walkable sliver or blocked strip along the border.
inline void expectSeamClassificationAgrees(const geometry::nav::NavMesh& mesh, const SeamSide& a, const SeamSide& b, bool vertical) {
	constexpr int64_t kStepMm = 50;
	constexpr int64_t kCrossingClearance = 20;
	const int64_t line = vertical ? a.max.x : a.max.y;
	const int64_t from = vertical ? a.min.y : a.min.x;
	const int64_t to = vertical ? a.max.y : a.max.x;

	std::vector<int64_t> crossings;
	for (const SeamSide* side : {&a, &b}) {
		for (const Vec2i64& v : navVerticesOnLine(*side->polys, vertical, line)) {
			crossings.push_back(vertical ? v.y : v.x);
		}
	}

	int checked = 0;
	int agreed = 0;
	for (int64_t s = from + kStepMm / 2; s < to; s += kStepMm) {
		const Vec2i64 pa = vertical ? Vec2i64{line - 1, s} : Vec2i64{s, line - 1};
		const Vec2i64 pb = vertical ? Vec2i64{line + 1, s} : Vec2i64{s, line + 1};

		bool water[2] = {false, false};
		for (int k = 0; k < 2; ++k) {
			const Vec2i64& p = k == 0 ? pa : pb;
			const SeamSide& owner = k == 0 ? a : b;
			const std::int32_t tri = geometry::nav::locateTriangle(mesh, p);
			ASSERT_GE(tri, 0) << "nav mesh gap at (" << p.x << ", " << p.y << ")";
			water[k] = !geometry::nav::isFloorFace(mesh.triangles[static_cast<size_t>(tri)]);
			bool onBoundary = false;
			const bool truth = navWater(owner.polys->rings, p, onBoundary);
			if (!onBoundary) {
				EXPECT_EQ(water[k], truth) << "nav disagrees with rings at (" << p.x << ", " << p.y << ")";
			}
		}

		const bool nearCrossing = std::any_of(crossings.begin(), crossings.end(), [s](int64_t c) {
			return std::abs(c - s) <= kCrossingClearance;
		});
		if (!nearCrossing) {
			++checked;
			agreed += water[0] == water[1] ? 1 : 0;
			EXPECT_EQ(water[0], water[1]) << "seam sliver at " << (vertical ? "y=" : "x=") << s;
		}
	}
	EXPECT_GT(checked, 0);
	EXPECT_EQ(agreed, checked);
}

// The border between `a` and `b` (b to the east or north): identical navRings
// vertices on it.
inline void expectBorderVerticesMatch(const SeamSide& a, const SeamSide& b, bool vertical) {
	const int64_t line = vertical ? b.min.x : b.min.y;
	const std::vector<Vec2i64> va = navVerticesOnLine(*a.polys, vertical, line);
	const std::vector<Vec2i64> vb = navVerticesOnLine(*b.polys, vertical, line);
	EXPECT_FALSE(va.empty());
	EXPECT_EQ(va, vb);
}

inline void expectBorderVerticesMatch(const Chunk& a, const Chunk& b, bool vertical) {
	expectBorderVerticesMatch(seamChunk(a), seamChunk(b), vertical);
}

} // namespace engine::world::terrain_test
