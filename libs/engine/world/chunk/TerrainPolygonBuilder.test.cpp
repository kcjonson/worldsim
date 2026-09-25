// TerrainPolygonBuilder tests (terrain-polygons-architecture.md D4-D6, D14, section 5):
// thin features survive, a boundary-touching lake closes synthetically outside the
// chunk square, two chunks built independently meet at bit-identical border
// vertices and a nav mesh over both has no seam gap or sliver, and the result is
// independent of thread count and completion order.

#include "TerrainPolygonBuilder.h"

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/IWorldSampler.h"

#include <nav/NavMesh.h>
#include <nav/PathQuery.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>
#include <random/HashNoise.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <future>
#include <memory>
#include <string>
#include <vector>

using namespace engine::world;
using geometry::Ring;
using geometry::Vec2i64;

namespace {

	constexpr int64_t kChunkMm = static_cast<int64_t>(kChunkSize) * TerrainPolygonBuilder::kTileMm;
	constexpr int64_t kApronMm = static_cast<int64_t>(kApronTiles) * TerrainPolygonBuilder::kTileMm;
	constexpr uint64_t kWorldSeed = 0x5EA11E55ULL;

	TileData tileOf(Biome biome) {
		TileData tile;
		tile.primaryBiome	= biome;
		tile.secondaryBiome = biome;
		tile.surface		= (isWater(biome) || biome == Biome::TemperateWetland) ? Surface::Water : Surface::Grass;
		return tile;
	}

	// A hand-built extended region (chunk plus apron), for features smaller than
	// the 16-tile biome sectors a sampler can place.
	struct HandTiles {
		std::vector<TileData> tiles;

		explicit HandTiles(Biome fill)
			: tiles(static_cast<size_t>(kExtendedSize) * static_cast<size_t>(kExtendedSize), tileOf(fill)) {}

		TileData& at(int32_t ex, int32_t ey) {
			return tiles[static_cast<size_t>(ey) * static_cast<size_t>(kExtendedSize) + static_cast<size_t>(ex)];
		}

		[[nodiscard]] TerrainPolygonBuilder::ExtendedTileFn fn() const {
			return [this](int32_t ex, int32_t ey) -> const TileData& {
				return tiles[static_cast<size_t>(ey) * static_cast<size_t>(kExtendedSize) + static_cast<size_t>(ex)];
			};
		}
	};

	// World mm of the center of extended tile (ex, ey) of chunk `coord`.
	Vec2i64 extendedTileCenterMm(ChunkCoordinate coord, int32_t ex, int32_t ey) {
		return {
			static_cast<int64_t>(coord.x) * kChunkMm - kApronMm + ex * TerrainPolygonBuilder::kTileMm + 500,
			static_cast<int64_t>(coord.y) * kChunkMm - kApronMm + ey * TerrainPolygonBuilder::kTileMm + 500
		};
	}

	// A lone tile's loop is the tile's thin-feature bump, displaced by the warp:
	// the 26 m shore term moves it as a whole by up to kMaxWarpMm per axis, so it
	// need not contain the tile center, but it lies within reach of it.
	void expectLoopFromTile(const Ring& ring, Vec2i64 tileCenter) {
		constexpr int64_t kReach = TerrainPolygonBuilder::kMaxWarpMm + TerrainPolygonBuilder::kTileMm;
		for (const Vec2i64& v : ring) {
			EXPECT_LE(std::abs(v.x - tileCenter.x), kReach) << v.x << ", " << v.y;
			EXPECT_LE(std::abs(v.y - tileCenter.y), kReach) << v.x << ", " << v.y;
		}
	}

	int64_t absArea2(const Ring& ring) {
		const geometry::Int128 a = geometry::signedAreaDoubled(ring);
		return static_cast<int64_t>(std::llround(std::abs(a.toDouble())));
	}

	// Every published ring, clipped or not, must be simple.
	void expectAllSimple(const ChunkTerrainPolygons& polys, const std::string& label) {
		for (size_t i = 0; i < polys.rings.size(); ++i) {
			EXPECT_TRUE(geometry::isSimple(polys.rings[i].ring).pass) << label << " rings[" << i << "]";
			EXPECT_EQ(polys.rings[i].profiles.size(), polys.rings[i].ring.size()) << label << " rings[" << i << "]";
		}
		for (size_t i = 0; i < polys.navRings.size(); ++i) {
			EXPECT_TRUE(geometry::isSimple(polys.navRings[i].ring).pass) << label << " navRings[" << i << "]";
		}
	}

	// A sampler that places biomes by chunk-corner lattice point, so a lake can be
	// put exactly where several chunks meet. Tile biomes are corner-interpolated
	// per 16-tile sector, so a Lake corner makes a staircase-edged lake around it.
	class CornerBiomeSampler : public IWorldSampler {
	  public:
		using BiomeAt	  = std::function<Biome(int32_t lx, int32_t ly)>;
		using ElevationAt = std::function<float(int32_t lx, int32_t ly)>;

		CornerBiomeSampler(uint64_t seed, BiomeAt biomeAt, ElevationAt elevationAt)
			: m_seed(seed),
			  m_biomeAt(std::move(biomeAt)),
			  m_elevationAt(std::move(elevationAt)) {}

		[[nodiscard]] ChunkSampleResult sampleChunk(ChunkCoordinate coord) const override {
			ChunkSampleResult result;
			const std::array<ChunkCorner, 4> corners = {
				ChunkCorner::NorthWest, ChunkCorner::NorthEast, ChunkCorner::SouthWest, ChunkCorner::SouthEast
			};
			for (size_t i = 0; i < 4; ++i) {
				result.cornerBiomes[i]	   = biomeAtWorld(coord.corner(corners[i]));
				result.cornerElevations[i] = sampleElevation(coord.corner(corners[i]));
			}
			result.computeSectorGrid();
			fillNeighborhoodCorners(
				result,
				coord,
				[this](WorldPosition p) { return biomeAtWorld(p); },
				[this](WorldPosition p) { return sampleElevation(p); }
			);
			return result;
		}

		[[nodiscard]] float sampleElevation(WorldPosition pos) const override {
			const auto [lx, ly] = lattice(pos);
			return m_elevationAt(lx, ly);
		}

		[[nodiscard]] uint64_t getWorldSeed() const override { return m_seed; }

	  private:
		static std::pair<int32_t, int32_t> lattice(WorldPosition pos) {
			return {
				static_cast<int32_t>(std::lround(pos.x / kChunkWorldSize)), static_cast<int32_t>(std::lround(pos.y / kChunkWorldSize))
			};
		}

		[[nodiscard]] BiomeWeights biomeAtWorld(WorldPosition pos) const {
			const auto [lx, ly] = lattice(pos);
			return BiomeWeights::single(m_biomeAt(lx, ly));
		}

		uint64_t	m_seed;
		BiomeAt		m_biomeAt;
		ElevationAt m_elevationAt;
	};

	// Lake at the lattice point (1, 1), the corner chunks (0,0), (1,0), (0,1), and
	// (1,1) share; everything else grassland. The shore crosses every border
	// between those four chunks.
	CornerBiomeSampler cornerLakeSampler() {
		return CornerBiomeSampler(
			kWorldSeed,
			[](int32_t lx, int32_t ly) { return (lx == 1 && ly == 1) ? Biome::Lake : Biome::TemperateGrassland; },
			[](int32_t lx, int32_t ly) { return (lx == 1 && ly == 1) ? 2.0F : 12.0F + static_cast<float>(lx + 2 * ly); }
		);
	}

	std::unique_ptr<Chunk> generateChunk(const IWorldSampler& sampler, ChunkCoordinate coord) {
		auto chunk = std::make_unique<Chunk>(coord, sampler.sampleChunk(coord), sampler.getWorldSeed());
		chunk->generate();
		return chunk;
	}

	// navRings vertices on the line x = line (vertical) or y = line, sorted.
	std::vector<Vec2i64> navVerticesOnLine(const ChunkTerrainPolygons& polys, bool vertical, int64_t line) {
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

	bool evenOddWater(const std::vector<TerrainRing>& rings, Vec2i64 p, bool& onBoundary) {
		int inside = 0;
		for (const TerrainRing& ring : rings) {
			const geometry::PointInPolygon where = geometry::pointInPolygon(p, ring.ring);
			onBoundary							 = onBoundary || where == geometry::PointInPolygon::OnBoundary;
			inside += where == geometry::PointInPolygon::Inside ? 1 : 0;
		}
		return (inside % 2) == 1;
	}

	struct SeamChunk {
		const Chunk* chunk;
		Vec2i64		 min;
		Vec2i64		 max;
	};

	SeamChunk seamChunk(const Chunk& chunk) {
		const ChunkCoordinate c = chunk.coordinate();
		const Vec2i64		  min{static_cast<int64_t>(c.x) * kChunkMm, static_cast<int64_t>(c.y) * kChunkMm};
		return {&chunk, min, {min.x + kChunkMm, min.y + kChunkMm}};
	}

	// The nav mesh NavInputBuilder would build over these chunks: one unblocked
	// border over their bounding rectangle, then every navRing as a blocked,
	// hole-capable water polygon.
	geometry::nav::NavMesh buildSeamMesh(const std::vector<SeamChunk>& chunks) {
		Vec2i64 lo = chunks.front().min;
		Vec2i64 hi = chunks.front().max;
		for (const SeamChunk& c : chunks) {
			lo = {std::min(lo.x, c.min.x), std::min(lo.y, c.min.y)};
			hi = {std::max(hi.x, c.max.x), std::max(hi.y, c.max.y)};
		}
		geometry::nav::NavMeshInput input;
		input.polygons.push_back({{lo, {hi.x, lo.y}, hi, {lo.x, hi.y}}, false, -3});
		for (const SeamChunk& c : chunks) {
			for (const TerrainRing& ring : c.chunk->terrainPolygons().navRings) {
				input.polygons.push_back({ring.ring, true, -1, geometry::nav::kNoOpening, true});
			}
		}
		return geometry::nav::buildNavMesh(input);
	}

	// Along the border between `a` (low side) and `b`, sample 1 mm either side.
	// Everywhere: the mesh has a triangle (no gap) and classifies the point the way
	// the owning chunk's unclipped rings do. Away from the border vertices (where
	// a crossing shoreline legitimately flips within a mm): both sides agree, so
	// there is no walkable sliver or blocked strip along the border.
	void expectSeamClassificationAgrees(const geometry::nav::NavMesh& mesh, const SeamChunk& a, const SeamChunk& b, bool vertical) {
		constexpr int64_t kStepMm			 = 50;
		constexpr int64_t kCrossingClearance = 20;
		const int64_t	  line				 = vertical ? a.max.x : a.max.y;
		const int64_t	  from				 = vertical ? a.min.y : a.min.x;
		const int64_t	  to				 = vertical ? a.max.y : a.max.x;

		std::vector<int64_t> crossings;
		for (const SeamChunk* side : {&a, &b}) {
			for (const Vec2i64& v : navVerticesOnLine(side->chunk->terrainPolygons(), vertical, line)) {
				crossings.push_back(vertical ? v.y : v.x);
			}
		}

		int checked = 0;
		int agreed	= 0;
		for (int64_t s = from + kStepMm / 2; s < to; s += kStepMm) {
			const Vec2i64 pa = vertical ? Vec2i64{line - 1, s} : Vec2i64{s, line - 1};
			const Vec2i64 pb = vertical ? Vec2i64{line + 1, s} : Vec2i64{s, line + 1};

			bool water[2] = {false, false};
			for (int k = 0; k < 2; ++k) {
				const Vec2i64&	   p	 = k == 0 ? pa : pb;
				const SeamChunk&   owner = k == 0 ? a : b;
				const std::int32_t tri	 = geometry::nav::locateTriangle(mesh, p);
				ASSERT_GE(tri, 0) << "nav mesh gap at (" << p.x << ", " << p.y << ")";
				water[k]				 = !geometry::nav::isFloorFace(mesh.triangles[static_cast<size_t>(tri)]);
				bool	   onBoundary	 = false;
				const bool truth		 = evenOddWater(owner.chunk->terrainPolygons().rings, p, onBoundary);
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

	void expectBorderVerticesMatch(const Chunk& a, const Chunk& b, bool vertical) {
		const int64_t line = vertical ? static_cast<int64_t>(b.coordinate().x) * kChunkMm : static_cast<int64_t>(b.coordinate().y) * kChunkMm;
		const std::vector<Vec2i64> va = navVerticesOnLine(a.terrainPolygons(), vertical, line);
		const std::vector<Vec2i64> vb = navVerticesOnLine(b.terrainPolygons(), vertical, line);
		EXPECT_FALSE(va.empty());
		EXPECT_EQ(va, vb);
	}

	bool sameRings(const std::vector<TerrainRing>& a, const std::vector<TerrainRing>& b) {
		if (a.size() != b.size()) {
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i) {
			if (a[i].ring != b[i].ring || a[i].profiles != b[i].profiles || a[i].kind != b[i].kind || a[i].water != b[i].water ||
				a[i].blocksMovement != b[i].blocksMovement || a[i].holeCapable != b[i].holeCapable) {
				return false;
			}
		}
		return true;
	}

} // namespace

// ============================================================================
// Thin features (D5 thin-feature guard)
// ============================================================================

TEST(TerrainPolygonBuilderTest, OneTilePoolSurvives) {
	const ChunkCoordinate coord{3, -2};
	HandTiles			  tiles(Biome::TemperateGrassland);
	const int32_t		  ex = kApronTiles + 200;
	const int32_t		  ey = kApronTiles + 311;
	tiles.at(ex, ey)		 = tileOf(Biome::Lake);

	const ChunkTerrainPolygons polys = TerrainPolygonBuilder::build(coord, kWorldSeed, tiles.fn());
	ASSERT_EQ(polys.rings.size(), 1U);
	const Ring& ring = polys.rings[0].ring;
	EXPECT_EQ(geometry::windingOrder(ring), geometry::Winding::CounterClockwise);
	EXPECT_TRUE(geometry::isSimple(ring).pass);
	EXPECT_GE(absArea2(ring), 2 * TerrainPolygonBuilder::kMinLoopAreaMm2);
	expectLoopFromTile(ring, extendedTileCenterMm(coord, ex, ey));
	EXPECT_EQ(polys.rings[0].water, WaterKind::Lake);
	ASSERT_EQ(polys.navRings.size(), 1U);
	EXPECT_EQ(polys.navRings[0].ring, ring);
	EXPECT_TRUE(polys.navRings[0].profiles.empty());
}

TEST(TerrainPolygonBuilderTest, OneTileIsletSurvivesAsHole) {
	const ChunkCoordinate coord{-1, 4};
	HandTiles			  tiles(Biome::Lake);
	const int32_t		  ex = kApronTiles + 57;
	const int32_t		  ey = kApronTiles + 402;
	tiles.at(ex, ey)		 = tileOf(Biome::TemperateGrassland);
	const Vec2i64 center	 = extendedTileCenterMm(coord, ex, ey);

	const ChunkTerrainPolygons polys = TerrainPolygonBuilder::build(coord, kWorldSeed, tiles.fn());
	expectAllSimple(polys, "islet");
	int holes = 0;
	for (const TerrainRing& ring : polys.rings) {
		if (geometry::windingOrder(ring.ring) != geometry::Winding::Clockwise) {
			continue;
		}
		++holes;
		expectLoopFromTile(ring.ring, center);
		EXPECT_GE(absArea2(ring.ring), 2 * TerrainPolygonBuilder::kMinLoopAreaMm2);
		EXPECT_EQ(ring.water, WaterKind::Lake);
	}
	EXPECT_EQ(holes, 1);
	// The water around it: one CCW ring closed along the extended boundary.
	EXPECT_EQ(polys.rings.size(), 2U);
}

// ============================================================================
// Boundary closure (D4 synthetic edges)
// ============================================================================

TEST(TerrainPolygonBuilderTest, BoundaryTouchingLakeClosesSyntheticallyOutsideTheChunk) {
	const ChunkCoordinate coord{0, 0};
	HandTiles			  tiles(Biome::TemperateGrassland);
	for (int32_t ey = 0; ey < kExtendedSize; ++ey) {
		for (int32_t ex = 0; ex < 100; ++ex) {
			tiles.at(ex, ey) = tileOf(Biome::Lake);
		}
	}

	const ChunkTerrainPolygons polys = TerrainPolygonBuilder::build(coord, kWorldSeed, tiles.fn());
	expectAllSimple(polys, "boundary lake");
	ASSERT_EQ(polys.rings.size(), 1U);

	const Vec2i64 extMin{-kApronMm, -kApronMm};
	const Vec2i64 extMax{kChunkMm + kApronMm, kChunkMm + kApronMm};
	auto		  nearExtendedBoundary = [&](const Vec2i64& v) {
		 return v.x - extMin.x <= TerrainPolygonBuilder::kFineCellMm || extMax.x - v.x <= TerrainPolygonBuilder::kFineCellMm ||
				v.y - extMin.y <= TerrainPolygonBuilder::kFineCellMm || extMax.y - v.y <= TerrainPolygonBuilder::kFineCellMm;
	};

	const TerrainRing& lake		 = polys.rings[0];
	int				   synthetic = 0;
	for (size_t i = 0; i < lake.ring.size(); ++i) {
		if ((lake.profiles[i].flags & ShoreProfile::kFlagSynthetic) == 0) {
			continue;
		}
		++synthetic;
		const Vec2i64& a = lake.ring[i];
		const Vec2i64& b = lake.ring[(i + 1) % lake.ring.size()];
		EXPECT_TRUE(nearExtendedBoundary(a) && nearExtendedBoundary(b));
		// Nothing synthetic inside the chunk square.
		EXPECT_FALSE(a.x > 0 && a.x < kChunkMm && a.y > 0 && a.y < kChunkMm);
		EXPECT_FALSE(b.x > 0 && b.x < kChunkMm && b.y > 0 && b.y < kChunkMm);
	}
	EXPECT_GT(synthetic, 0);
	// The real shore is not flagged.
	EXPECT_LT(synthetic, static_cast<int>(lake.ring.size()));
	// And all of the closure is: unflagged edges hugging the boundary are only the
	// short stubs where the real shore meets it (top and bottom), not a closure
	// that wandered out of the flagged band.
	constexpr int64_t kHug			  = 2 * TerrainPolygonBuilder::kTileMm;
	auto			  hugsBoundary	  = [&](const Vec2i64& v) {
		   return v.x - extMin.x <= kHug || extMax.x - v.x <= kHug || v.y - extMin.y <= kHug || extMax.y - v.y <= kHug;
	};
	double unflaggedHugMm = 0.0;
	for (size_t i = 0; i < lake.ring.size(); ++i) {
		const Vec2i64& a = lake.ring[i];
		const Vec2i64& b = lake.ring[(i + 1) % lake.ring.size()];
		if ((lake.profiles[i].flags & ShoreProfile::kFlagSynthetic) == 0 && hugsBoundary(a) && hugsBoundary(b)) {
			unflaggedHugMm += std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y));
		}
	}
	EXPECT_LT(unflaggedHugMm, 10000.0);

	// navRings end at the chunk square: every vertex inside it, the west border
	// run along x = 0 from corner to corner.
	ASSERT_EQ(polys.navRings.size(), 1U);
	for (const Vec2i64& v : polys.navRings[0].ring) {
		EXPECT_TRUE(v.x >= 0 && v.x <= kChunkMm && v.y >= 0 && v.y <= kChunkMm) << v.x << ", " << v.y;
	}
	const std::vector<Vec2i64> west = navVerticesOnLine(polys, true, 0);
	EXPECT_NE(std::find(west.begin(), west.end(), Vec2i64{0, 0}), west.end());
	EXPECT_NE(std::find(west.begin(), west.end(), Vec2i64{0, kChunkMm}), west.end());
}

// ============================================================================
// Shore profiles (D15)
// ============================================================================

// A lake filling the west half, its east shore sandy in the south and rocky in
// the north: profiles read the land side of each vertex.
TEST(TerrainPolygonBuilderTest, ShoreProfilesReadTheLandSide) {
	const ChunkCoordinate coord{0, 0};
	HandTiles			  tiles(Biome::TemperateGrassland);
	constexpr int32_t	  kShoreEx = kExtendedSize / 2;
	for (int32_t ey = 0; ey < kExtendedSize; ++ey) {
		for (int32_t ex = 0; ex < kExtendedSize; ++ex) {
			TileData& t = tiles.at(ex, ey);
			if (ex < kShoreEx) {
				t = tileOf(Biome::Lake);
				continue;
			}
			t.surface  = ey < kExtendedSize / 2 ? Surface::Sand : Surface::Rock;
			t.moisture = 200;
		}
	}

	const ChunkTerrainPolygons polys = TerrainPolygonBuilder::build(coord, kWorldSeed, tiles.fn());
	ASSERT_EQ(polys.rings.size(), 1U);
	const TerrainRing& lake = polys.rings[0];
	EXPECT_EQ(lake.water, WaterKind::Lake);

	const int64_t midY		= -kApronMm + static_cast<int64_t>(kExtendedSize / 2) * TerrainPolygonBuilder::kTileMm;
	int			  sandy		= 0;
	int			  rocky		= 0;
	for (size_t i = 0; i < lake.ring.size(); ++i) {
		const ShoreProfile& p = lake.profiles[i];
		const Vec2i64&		v = lake.ring[i];
		// Skip the closure and the corners where the real shore meets it.
		const bool nearBoundary = v.y < -kApronMm + 3000 || v.y > kChunkMm + kApronMm - 3000;
		if ((p.flags & ShoreProfile::kFlagSynthetic) != 0 || nearBoundary) {
			continue;
		}
		EXPECT_EQ(p.moisture, 200) << v.x << ", " << v.y;
		EXPECT_LE(static_cast<int>(p.sand) + static_cast<int>(p.mud), 255);
		if (v.y < midY - 5000) {
			++sandy;
			EXPECT_GT(p.sand, p.mud);
			EXPECT_EQ(static_cast<int>(p.sand) + static_cast<int>(p.mud), 255);
			EXPECT_EQ(p.flags & ShoreProfile::kFlagRock, 0);
		} else if (v.y > midY + 5000) {
			++rocky;
			EXPECT_NE(p.flags & ShoreProfile::kFlagRock, 0);
			EXPECT_EQ(p.sand, 0);
			EXPECT_EQ(p.mud, 0);
		}
	}
	EXPECT_GT(sandy, 10);
	EXPECT_GT(rocky, 10);
}

// ============================================================================
// Seams (D4, section 5)
// ============================================================================

TEST(TerrainPolygonBuilderTest, HorizontalNeighborsMeetAtIdenticalBorderVertices) {
	const CornerBiomeSampler sampler = cornerLakeSampler();
	const auto				 west	 = generateChunk(sampler, {0, 0});
	const auto				 east	 = generateChunk(sampler, {1, 0});
	expectAllSimple(west->terrainPolygons(), "west");
	expectAllSimple(east->terrainPolygons(), "east");
	for (const TerrainRing& ring : west->terrainPolygons().rings) {
		EXPECT_EQ(ring.water, WaterKind::Lake);
	}

	expectBorderVerticesMatch(*west, *east, true);

	const SeamChunk				 a	  = seamChunk(*west);
	const SeamChunk				 b	  = seamChunk(*east);
	const geometry::nav::NavMesh mesh = buildSeamMesh({a, b});
	ASSERT_FALSE(mesh.triangles.empty());
	expectSeamClassificationAgrees(mesh, a, b, true);
}

TEST(TerrainPolygonBuilderTest, VerticalNeighborsMeetAtIdenticalBorderVertices) {
	const CornerBiomeSampler sampler = cornerLakeSampler();
	const auto				 south	 = generateChunk(sampler, {0, 0});
	const auto				 north	 = generateChunk(sampler, {0, 1});
	expectAllSimple(south->terrainPolygons(), "south");
	expectAllSimple(north->terrainPolygons(), "north");

	expectBorderVerticesMatch(*south, *north, false);

	const SeamChunk				 a	  = seamChunk(*south);
	const SeamChunk				 b	  = seamChunk(*north);
	const geometry::nav::NavMesh mesh = buildSeamMesh({a, b});
	ASSERT_FALSE(mesh.triangles.empty());
	expectSeamClassificationAgrees(mesh, a, b, false);
}

TEST(TerrainPolygonBuilderTest, FourChunksMeetingAtACornerAgreeOnEveryBorder) {
	const CornerBiomeSampler sampler = cornerLakeSampler();
	const auto				 c00	 = generateChunk(sampler, {0, 0});
	const auto				 c10	 = generateChunk(sampler, {1, 0});
	const auto				 c01	 = generateChunk(sampler, {0, 1});
	const auto				 c11	 = generateChunk(sampler, {1, 1});

	expectBorderVerticesMatch(*c00, *c10, true);
	expectBorderVerticesMatch(*c01, *c11, true);
	expectBorderVerticesMatch(*c00, *c01, false);
	expectBorderVerticesMatch(*c10, *c11, false);

	// All four share the corner point, which lies in the lake.
	const Vec2i64 corner{kChunkMm, kChunkMm};
	for (const Chunk* c : {c00.get(), c10.get(), c01.get(), c11.get()}) {
		expectAllSimple(c->terrainPolygons(), "corner");
		const std::vector<Vec2i64> onLine = navVerticesOnLine(c->terrainPolygons(), true, kChunkMm);
		EXPECT_NE(std::find(onLine.begin(), onLine.end(), corner), onLine.end());
	}

	const std::vector<SeamChunk> all  = {seamChunk(*c00), seamChunk(*c10), seamChunk(*c01), seamChunk(*c11)};
	const geometry::nav::NavMesh mesh = buildSeamMesh(all);
	ASSERT_FALSE(mesh.triangles.empty());
	expectSeamClassificationAgrees(mesh, all[0], all[1], true);
	expectSeamClassificationAgrees(mesh, all[2], all[3], true);
	expectSeamClassificationAgrees(mesh, all[0], all[2], false);
	expectSeamClassificationAgrees(mesh, all[1], all[3], false);
}

// ============================================================================
// Determinism (D14)
// ============================================================================

TEST(TerrainPolygonBuilderTest, IdenticalAcrossThreadCountsAndCompletionOrders) {
	const CornerBiomeSampler			 sampler = cornerLakeSampler();
	const std::vector<ChunkCoordinate> coords	 = {{0, 0}, {1, 0}, {0, 1}, {1, 1}};

	std::vector<std::unique_ptr<Chunk>> serial;
	for (const ChunkCoordinate& c : coords) {
		serial.push_back(generateChunk(sampler, c));
	}

	// Concurrent, launched in reverse so completion order differs from the serial run.
	std::vector<std::future<std::unique_ptr<Chunk>>> futures;
	for (auto it = coords.rbegin(); it != coords.rend(); ++it) {
		const ChunkCoordinate c = *it;
		futures.push_back(std::async(std::launch::async, [&sampler, c] { return generateChunk(sampler, c); }));
	}
	std::vector<std::unique_ptr<Chunk>> concurrent(coords.size());
	for (size_t i = 0; i < futures.size(); ++i) {
		concurrent[coords.size() - 1 - i] = futures[i].get();
	}

	for (size_t i = 0; i < coords.size(); ++i) {
		const ChunkTerrainPolygons& a = serial[i]->terrainPolygons();
		const ChunkTerrainPolygons& b = concurrent[i]->terrainPolygons();
		EXPECT_FALSE(a.rings.empty());
		EXPECT_TRUE(sameRings(a.rings, b.rings)) << "chunk " << i;
		EXPECT_TRUE(sameRings(a.navRings, b.navRings)) << "chunk " << i;
		EXPECT_EQ(a.version, 1U);
	}
}

// ============================================================================
// Randomized sweep
// ============================================================================

TEST(TerrainPolygonBuilderTest, RandomizedCornerBiomesStaySimpleAndSeamed) {
	constexpr std::array<Biome, 6> kPalette = {
		Biome::Lake, Biome::Ocean, Biome::TemperateWetland, Biome::TemperateGrassland, Biome::Beach, Biome::TemperateDeciduousForest
	};
	int shoresOnBorder = 0;
	for (const uint32_t seed : {11U, 29U, 47U}) {
		const CornerBiomeSampler sampler(
			seed,
			[seed](int32_t lx, int32_t ly) {
				return kPalette[foundation::hash3(lx, ly, 0, seed) % kPalette.size()];
			},
			[seed](int32_t lx, int32_t ly) { return static_cast<float>(foundation::hash3(lx, ly, 1, seed) % 4000U) / 100.0F; }
		);
		const auto west = generateChunk(sampler, {0, 0});
		const auto east = generateChunk(sampler, {1, 0});
		expectAllSimple(west->terrainPolygons(), "sweep west " + std::to_string(seed));
		expectAllSimple(east->terrainPolygons(), "sweep east " + std::to_string(seed));
		const std::vector<Vec2i64> westBorder = navVerticesOnLine(west->terrainPolygons(), true, kChunkMm);
		EXPECT_EQ(westBorder, navVerticesOnLine(east->terrainPolygons(), true, kChunkMm)) << "seed " << seed;
		// A shore crossing the border pins vertices strictly between its corners.
		const bool crossed = std::any_of(westBorder.begin(), westBorder.end(), [](const Vec2i64& v) {
			return v.y > 0 && v.y < kChunkMm;
		});
		if (crossed) {
			++shoresOnBorder;
			const SeamChunk				 a	  = seamChunk(*west);
			const SeamChunk				 b	  = seamChunk(*east);
			const geometry::nav::NavMesh mesh = buildSeamMesh({a, b});
			ASSERT_FALSE(mesh.triangles.empty());
			expectSeamClassificationAgrees(mesh, a, b, true);
		}
	}
	// The sweep is only worth something if shores actually cross the seam.
	EXPECT_GT(shoresOnBorder, 0);
}
