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
#include "world/chunk/TerrainPolygonBuilderDetail.h"
#include "world/chunk/TerrainPolygonTestSupport.h"

#include <nav/NavMesh.h>
#include <polygon/Polygon.h>
#include <random/HashNoise.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace engine::world;
using namespace engine::world::terrain_test;

namespace {

	constexpr uint64_t kWorldSeed = 0x5EA11E55ULL;

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

	// A straight 1-wide run's sides lie within two tiles of its center line, which
	// the warp moves by up to kMaxWarpMm.
	constexpr int64_t kRunWindowMm = TerrainPolygonBuilder::kMaxWarpMm + 2 * TerrainPolygonBuilder::kTileMm;

	struct Span {
		double fromMm;
		double toMm;
	};

	// Where the line across a straight run at `stationMm` along it is water (or
	// land), within kRunWindowMm of the run's center line `centerMm`: a run along x
	// is crossed by x = stationMm, one along y by y = stationMm. The rings are
	// waterlines (even-odd) and the line starts on the land outside them.
	std::vector<Span> spansAcrossRun(const std::vector<TerrainRing>& rings, bool alongX, int64_t stationMm, int64_t centerMm, bool water) {
		std::vector<double> crossings;
		for (const TerrainRing& ring : rings) {
			for (size_t i = 0; i < ring.ring.size(); ++i) {
				const Vec2i64& a	  = ring.ring[i];
				const Vec2i64& b	  = ring.ring[(i + 1) % ring.ring.size()];
				const int64_t  alongA = alongX ? a.x : a.y;
				const int64_t  alongB = alongX ? b.x : b.y;
				if ((alongA <= stationMm) == (alongB <= stationMm)) {
					continue;
				}
				const double t		 = static_cast<double>(stationMm - alongA) / static_cast<double>(alongB - alongA);
				const double acrossA = static_cast<double>(alongX ? a.y : a.x);
				const double acrossB = static_cast<double>(alongX ? b.y : b.x);
				crossings.push_back(acrossA + t * (acrossB - acrossA));
			}
		}
		std::sort(crossings.begin(), crossings.end());
		const double	  lo = static_cast<double>(centerMm - kRunWindowMm);
		const double	  hi = static_cast<double>(centerMm + kRunWindowMm);
		std::vector<Span> spans;
		// Between crossings k and k + 1 the line is in water when k is even.
		for (size_t k = 0; k + 1 < crossings.size(); ++k) {
			if (((k % 2) == 0) == water && crossings[k + 1] > lo && crossings[k] < hi) {
				spans.push_back({crossings[k], crossings[k + 1]});
			}
		}
		return spans;
	}

	// Unwarped, a guarded 1-wide run is a band 1.17 m wide (0.85 on the run, 0.25
	// beside it, so 0.5 falls 0.583 m either side of its center); the warp moves its
	// width by up to about a fifth.
	constexpr double kRunMinWidthMm = 900.0;

	// A straight 1-wide run on extended row `run` (along x) or column `run` (along
	// y), of water or of land, checked from extended tile `from` to `to` along it.
	struct StraightRun {
		ChunkCoordinate coord;
		bool			alongX;
		int32_t			run;
		int32_t			from;
		int32_t			to;
		bool			water;
	};

	// Every 100 mm along the run, the line across it meets one span of the run's
	// side, closed off by the other side within the window and never under
	// kRunMinWidthMm wide.
	void expectRunHolds(const ChunkTerrainPolygons& polys, const StraightRun& r) {
		const Vec2i64 first	 = r.alongX ? extendedTileCenterMm(r.coord, r.from, r.run) : extendedTileCenterMm(r.coord, r.run, r.from);
		const Vec2i64 last	 = r.alongX ? extendedTileCenterMm(r.coord, r.to, r.run) : extendedTileCenterMm(r.coord, r.run, r.to);
		const int64_t center = r.alongX ? first.y : first.x;
		double		  narrowest = std::numeric_limits<double>::max();
		double		  widest	= 0.0;
		for (int64_t s = r.alongX ? first.x : first.y; s <= (r.alongX ? last.x : last.y); s += 100) {
			const std::vector<Span> spans = spansAcrossRun(polys.rings, r.alongX, s, center, r.water);
			ASSERT_EQ(spans.size(), 1U) << (r.water ? "water" : "land") << " spans across the run at " << s;
			EXPECT_GT(spans[0].fromMm, static_cast<double>(center - kRunWindowMm)) << s;
			EXPECT_LT(spans[0].toMm, static_cast<double>(center + kRunWindowMm)) << s;
			narrowest = std::min(narrowest, spans[0].toMm - spans[0].fromMm);
			widest	  = std::max(widest, spans[0].toMm - spans[0].fromMm);
		}
		std::cout << "[ run width ] " << (r.water ? "water" : "land") << " run " << r.run << (r.alongX ? " along x" : " along y")
				  << ": " << narrowest << " to " << widest << " mm\n";
		EXPECT_GE(narrowest, kRunMinWidthMm);
	}

	// Two pairs of 50 m blocks of `biome`, each pair joined corner to corner by a
	// 40-tile diagonal 1-wide run: rising from blocks (150..199)^2 to (240..289)^2,
	// falling from x 300..349, y 240..289 to x 390..439, y 150..199 (extended tiles).
	// Each pair spans 140 m on both axes; one broken at a saddle leaves pieces of
	// 90 m at most.
	void paintDiagonalPairs(HandTiles& tiles, Biome biome) {
		auto block = [&tiles, biome](int32_t x0, int32_t y0) {
			for (int32_t ey = y0; ey < y0 + 50; ++ey) {
				for (int32_t ex = x0; ex < x0 + 50; ++ex) {
					tiles.at(ex, ey) = tileOf(biome);
				}
			}
		};
		block(150, 150);
		block(240, 240);
		block(300, 240);
		block(390, 150);
		for (int32_t i = 0; i < 40; ++i) {
			tiles.at(200 + i, 200 + i) = tileOf(biome);
			tiles.at(350 + i, 239 - i) = tileOf(biome);
		}
	}

	constexpr int64_t kDiagonalPairSpanMm = 130 * TerrainPolygonBuilder::kTileMm;

	// The width and height of a ring's bounding box.
	Vec2i64 extentOf(const Ring& ring) {
		Vec2i64 lo = ring.front();
		Vec2i64 hi = ring.front();
		for (const Vec2i64& v : ring) {
			lo = {std::min(lo.x, v.x), std::min(lo.y, v.y)};
			hi = {std::max(hi.x, v.x), std::max(hi.y, v.y)};
		}
		return {hi.x - lo.x, hi.y - lo.y};
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

} // namespace

// ============================================================================
// Thin features (D5 thin-feature guard)
// ============================================================================

TEST(TerrainPolygonBuilderTest, OneTilePoolSurvives) {
	const ChunkCoordinate coord{3, -2};
	HandTiles			  tiles(coord, Biome::TemperateGrassland);
	const int32_t		  ex = kApronTiles + 200;
	const int32_t		  ey = kApronTiles + 311;
	tiles.at(ex, ey)		 = tileOf(Biome::Lake);

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
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
	HandTiles			  tiles(coord, Biome::Lake);
	const int32_t		  ex = kApronTiles + 57;
	const int32_t		  ey = kApronTiles + 402;
	tiles.at(ex, ey)		 = tileOf(Biome::TemperateGrassland);
	const Vec2i64 center	 = extendedTileCenterMm(coord, ex, ey);

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
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

// The guard sample by sample: a straight 1-wide run of either side has two
// same-side neighbors, opposite, and a blur of exactly 0.5, so it is thin; an
// L-corner (two, adjacent) and a 2-wide run keep their plain blur and round freely.
TEST(TerrainPolygonBuilderTest, GuardFiresOnStraightOneWideRunsNotOnCornersOrTwoWideRuns) {
	using Water = std::function<bool(int64_t, int64_t)>;
	auto blur = [](const Water& water, int64_t x, int64_t y) {
		int sum = 0;
		for (int64_t dy = -1; dy <= 1; ++dy) {
			for (int64_t dx = -1; dx <= 1; ++dx) {
				sum += water(x + dx, y + dy) ? static_cast<int>((2 - std::abs(dx)) * (2 - std::abs(dy))) : 0;
			}
		}
		return static_cast<float>(sum) / 16.0F;
	};
	auto value = [](const Water& water, int64_t x, int64_t y) { return terrain_detail::coarseWaterValue(water, x, y); };

	const Water inletAlongX = [](int64_t, int64_t y) { return y == 0; };
	const Water inletAlongY = [](int64_t x, int64_t) { return x == 0; };
	const Water wallAlongX	= [](int64_t, int64_t y) { return y != 0; };
	const Water wallAlongY	= [](int64_t x, int64_t) { return x != 0; };
	EXPECT_EQ(blur(inletAlongX, 5, 0), 0.5F);
	EXPECT_EQ(blur(wallAlongY, 0, 5), 0.5F);
	EXPECT_EQ(value(inletAlongX, 5, 0), kThinFeatureFloor);
	EXPECT_EQ(value(inletAlongY, 0, 5), kThinFeatureFloor);
	EXPECT_EQ(value(wallAlongX, 5, 0), kThinFeatureCeil);
	EXPECT_EQ(value(wallAlongY, 0, 5), kThinFeatureCeil);
	EXPECT_EQ(value(inletAlongX, 5, 1), 0.25F) << "beside the run, three same-side neighbors";
	EXPECT_EQ(value(wallAlongX, 5, 1), 0.75F);

	// A lake's convex and concave corners, and 2-wide runs of water and of land.
	const std::vector<Water> unguarded = {
		[](int64_t x, int64_t y) { return x >= 0 && y >= 0; },
		[](int64_t x, int64_t y) { return x >= 0 || y >= 0; },
		[](int64_t, int64_t y) { return y == 0 || y == 1; },
		[](int64_t x, int64_t) { return x != 0 && x != 1; },
	};
	for (size_t i = 0; i < unguarded.size(); ++i) {
		for (int64_t y = -3; y <= 3; ++y) {
			for (int64_t x = -3; x <= 3; ++x) {
				EXPECT_EQ(value(unguarded[i], x, y), blur(unguarded[i], x, y)) << "layout " << i << " at " << x << ", " << y;
			}
		}
	}

	// A 1-wide L: its arms are guarded, its corner (arms on adjacent sides) is not.
	const Water lPath = [](int64_t x, int64_t y) { return (y == 0 && x >= 0) || (x == 0 && y >= 0); };
	EXPECT_EQ(value(lPath, 0, 0), blur(lPath, 0, 0));
	EXPECT_EQ(value(lPath, 5, 0), kThinFeatureFloor);
	EXPECT_EQ(value(lPath, 0, 5), kThinFeatureFloor);
}

// A straight 1-wide inlet blurs to exactly 0.5 along its centerline, so the warp
// flickered it in and out. Floored, it is a band the warp bends but never pinches
// shut: a lake with an inlet along each axis is one ring.
TEST(TerrainPolygonBuilderTest, StraightOneWideInletStaysOpen) {
	const ChunkCoordinate coord{5, 2};
	HandTiles			  tiles(coord, Biome::TemperateGrassland);
	for (int32_t ey = 200; ey < 240; ++ey) {
		for (int32_t ex = 200; ex < 240; ++ex) {
			tiles.at(ex, ey) = tileOf(Biome::Lake);
		}
	}
	for (int32_t k = 240; k < 280; ++k) {
		tiles.at(k, 220) = tileOf(Biome::Lake);
		tiles.at(220, k) = tileOf(Biome::Lake);
	}

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
	expectAllSimple(polys, "inlets");
	ASSERT_EQ(polys.rings.size(), 1U) << "the lake and its inlets are one ring";
	// Clear of the lake's corners at the mouth and of the rounded tip.
	expectRunHolds(polys, {.coord = coord, .alongX = true, .run = 220, .from = 244, .to = 276, .water = true});
	expectRunHolds(polys, {.coord = coord, .alongX = false, .run = 220, .from = 244, .to = 276, .water = true});
}

// The same run on land: a 1-wide wall between two lakes blurs to 0.5, and the lakes
// flooded over it. Capped, the wall holds a land band its whole length, so four
// lakes parted by a cross of walls stay four rings.
TEST(TerrainPolygonBuilderTest, StraightOneWideIsthmusKeepsTheWaterApart) {
	const ChunkCoordinate coord{-2, 3};
	HandTiles			  tiles(coord, Biome::TemperateGrassland);
	for (int32_t ey = 300; ey < 360; ++ey) {
		for (int32_t ex = 300; ex < 360; ++ex) {
			if (ex != 330 && ey != 330) {
				tiles.at(ex, ey) = tileOf(Biome::Lake);
			}
		}
	}

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
	expectAllSimple(polys, "isthmus");
	ASSERT_EQ(polys.rings.size(), 4U) << "one lake per quadrant, none joined across a wall";
	for (const TerrainRing& ring : polys.rings) {
		EXPECT_EQ(geometry::windingOrder(ring.ring), geometry::Winding::CounterClockwise);
	}
	// Each arm of the cross, clear of its ends and of the junction.
	for (const auto& [from, to] : {std::pair{304, 326}, std::pair{334, 356}}) {
		expectRunHolds(polys, {.coord = coord, .alongX = true, .run = 330, .from = from, .to = to, .water = false});
		expectRunHolds(polys, {.coord = coord, .alongX = false, .run = 330, .from = from, .to = to, .water = false});
	}
}

// A diagonal 1-wide run has no same-side cardinal neighbor, so it was guarded
// before the axis rule too: its tiles join through bilinear saddles worth
// (2 x 0.85 + 2 x 0.25) / 4 = 0.55, and 0.525 where it meets a lake's corner. Two
// lakes joined corner to corner by a diagonal channel, rising or falling, are one ring.
TEST(TerrainPolygonBuilderTest, DiagonalOneWideChannelStaysConnected) {
	const ChunkCoordinate coord{1, -3};
	HandTiles			  tiles(coord, Biome::TemperateGrassland);
	paintDiagonalPairs(tiles, Biome::Lake);

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
	expectAllSimple(polys, "diagonal channels");
	ASSERT_EQ(polys.rings.size(), 2U) << "one ring per pair of lakes";
	for (const TerrainRing& ring : polys.rings) {
		const Vec2i64 extent = extentOf(ring.ring);
		EXPECT_GE(extent.x, kDiagonalPairSpanMm);
		EXPECT_GE(extent.y, kDiagonalPairSpanMm);
	}
}

// The same on land: two islands in a lake joined corner to corner by a diagonal
// 1-wide isthmus, rising or falling. Its saddles are 0.45, and 0.475 at the
// islands' corners, so each pair of islands is one hole.
TEST(TerrainPolygonBuilderTest, DiagonalOneWideIsthmusStaysConnected) {
	const ChunkCoordinate coord{-4, -1};
	HandTiles			  tiles(coord, Biome::Lake);
	paintDiagonalPairs(tiles, Biome::TemperateGrassland);

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
	expectAllSimple(polys, "diagonal isthmus");
	ASSERT_EQ(polys.rings.size(), 3U) << "the lake and one hole per pair of islands";
	int holes = 0;
	for (const TerrainRing& ring : polys.rings) {
		if (geometry::windingOrder(ring.ring) != geometry::Winding::Clockwise) {
			continue;
		}
		++holes;
		const Vec2i64 extent = extentOf(ring.ring);
		EXPECT_GE(extent.x, kDiagonalPairSpanMm);
		EXPECT_GE(extent.y, kDiagonalPairSpanMm);
	}
	EXPECT_EQ(holes, 2);
}

// ============================================================================
// Boundary closure (D4 synthetic edges)
// ============================================================================

TEST(TerrainPolygonBuilderTest, BoundaryTouchingLakeClosesSyntheticallyOutsideTheChunk) {
	const ChunkCoordinate coord{0, 0};
	HandTiles			  tiles(coord, Biome::TemperateGrassland);
	for (int32_t ey = 0; ey < kExtendedSize; ++ey) {
		for (int32_t ex = 0; ex < 100; ++ex) {
			tiles.at(ex, ey) = tileOf(Biome::Lake);
		}
	}

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
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
	HandTiles			  tiles(coord, Biome::TemperateGrassland);
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

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
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

// Slope is a heuristic (tile elevation carries no local relief): never a
// degenerate 0 or 255, and never uniform along a shore (D15).
TEST(TerrainPolygonBuilderTest, ShoreSlopeIsBoundedAndVariesAlongTheShore) {
	const ChunkCoordinate coord{0, 0};
	HandTiles			  tiles(coord, Biome::TemperateGrassland);
	for (int32_t ey = 0; ey < kExtendedSize; ++ey) {
		for (int32_t ex = 0; ex < kExtendedSize / 2; ++ex) {
			tiles.at(ex, ey) = tileOf(Biome::Lake);
		}
	}

	const ChunkTerrainPolygons polys = buildHand(tiles, kWorldSeed);
	ASSERT_EQ(polys.rings.size(), 1U);
	const TerrainRing& lake = polys.rings[0];

	const auto minByte = static_cast<int>(std::lround(TerrainPolygonBuilder::kSlopeMin * 255.0F));
	const auto maxByte = static_cast<int>(std::lround(TerrainPolygonBuilder::kSlopeMax * 255.0F));
	int		   lo	   = 255;
	int		   hi	   = 0;
	size_t	   real	   = 0;
	for (size_t i = 0; i < lake.ring.size(); ++i) {
		if ((lake.profiles[i].flags & ShoreProfile::kFlagSynthetic) != 0) {
			continue;
		}
		const int s = lake.profiles[i].slope;
		EXPECT_GE(s, minByte);
		EXPECT_LE(s, maxByte);
		lo = std::min(lo, s);
		hi = std::max(hi, s);
		++real;
	}
	ASSERT_GT(real, 40U);
	EXPECT_GT(hi - lo, 40) << "a straight shore still varies along its length";
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

	const SeamSide				 a	  = seamChunk(*west);
	const SeamSide				 b	  = seamChunk(*east);
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

	const SeamSide				 a	  = seamChunk(*south);
	const SeamSide				 b	  = seamChunk(*north);
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

	const std::vector<SeamSide> all  = {seamChunk(*c00), seamChunk(*c10), seamChunk(*c01), seamChunk(*c11)};
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
	static constexpr std::array<Biome, 6> kPalette = {
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
			const SeamSide				 a	  = seamChunk(*west);
			const SeamSide				 b	  = seamChunk(*east);
			const geometry::nav::NavMesh mesh = buildSeamMesh({a, b});
			ASSERT_FALSE(mesh.triangles.empty());
			expectSeamClassificationAgrees(mesh, a, b, true);
		}
	}
	// The sweep is only worth something if shores actually cross the seam.
	EXPECT_GT(shoresOnBorder, 0);
}
