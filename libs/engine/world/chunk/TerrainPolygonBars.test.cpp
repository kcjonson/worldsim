// Point bars (terrain-polygons-architecture.md D12): the tiles a crescent on the
// inner bank of a tight bend covers, marked by TerrainPolygonBuilder in
// ChunkTerrainPolygons::barTiles.

#include "world/chunk/TerrainPolygonBuilder.h"

#include "world/chunk/TerrainPolygonTestSupport.h"

#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <functional>
#include <vector>

using namespace engine::world;
using namespace engine::world::terrain_test;

namespace {

	constexpr uint64_t kSeed = 0xBA125EEDULL;

	// A U-bend of radius 14 m around (256, 256), half-width 4 m: curvature 1/14 m,
	// past kBarCurvature.
	constexpr double kCenterX = 256.0;
	constexpr double kCenterY = 256.0;
	constexpr double kRadius  = 14.0;
	constexpr float	 kHalfWidth = 4.0F;

	std::vector<TerrainPolygonBuilder::RiverSegment> bendRiver(double radius) {
		return riverSegmentsThrough(uBend(kCenterX, kCenterY, radius, 60.0, 2.0), kHalfWidth);
	}

	// Every bar tile of the chunk square that `keep` accepts, as tile centers.
	std::vector<geometry::Vec2d> barCenters(const ChunkTerrainPolygons& polys, const std::function<bool(double, double)>& keep = {}) {
		std::vector<geometry::Vec2d> out;
		for (int64_t ty = 0; ty < kChunkSize; ++ty) {
			for (int64_t tx = 0; tx < kChunkSize; ++tx) {
				const double x = static_cast<double>(tx) + 0.5;
				const double y = static_cast<double>(ty) + 0.5;
				if (polys.barTiles.test(tx, ty) && (!keep || keep(x, y))) {
					out.push_back({x, y});
				}
			}
		}
		return out;
	}

	double fromCenter(const geometry::Vec2d& p) {
		return geometry::length(p - geometry::Vec2d{kCenterX, kCenterY});
	}

} // namespace

// The bend grows a bar inside its inner bank; the outer bank grows none.
TEST(TerrainPolygonBarsTest, ABendGrowsABarOnItsInnerSideOnly) {
	const HandTiles					   tiles({0, 0}, Biome::TemperateGrassland);
	const ChunkTerrainPolygons		   polys = buildHand(tiles, kSeed, bendRiver(kRadius));
	const std::vector<geometry::Vec2d> bars	 = barCenters(polys);
	EXPECT_GT(bars.size(), 15U);
	for (const geometry::Vec2d& p : bars) {
		EXPECT_LT(fromCenter(p), kRadius - 1.0) << p.x << ", " << p.y << " lies outside the inner bank";
		EXPECT_GT(p.x, kCenterX - 3.0) << p.x << ", " << p.y << " lies along a straight lead";
	}
}

// Neither a straight reach nor a bend gentler than kBarCurvature grows a bar.
TEST(TerrainPolygonBarsTest, StraightReachesAndGentleBendsGrowNone) {
	const HandTiles tiles({0, 0}, Biome::TemperateGrassland);
	const auto		straight = riverSegmentsThrough({{100.0, 300.0}, {250.0, 300.0}, {400.0, 300.0}}, kHalfWidth);
	EXPECT_TRUE(buildHand(tiles, kSeed, straight).barTiles.words.empty());
	// 1 / 25 m < kBarCurvature.
	EXPECT_TRUE(buildHand(tiles, kSeed, bendRiver(25.0)).barTiles.words.empty());
}

// A bar never lands on a Water tile, whatever the crescent covers.
TEST(TerrainPolygonBarsTest, BarsNeverCoverWaterTiles) {
	HandTiles				   tiles({0, 0}, Biome::TemperateGrassland);
	const auto				   rivers = bendRiver(kRadius);
	const ChunkTerrainPolygons first  = buildHand(tiles, kSeed, rivers);
	// The tile raster's channel: every tile whose center the ribbon covers.
	for (int32_t ey = 0; ey < tiles.size; ++ey) {
		for (int32_t ex = 0; ex < tiles.size; ++ex) {
			const Vec2i64 center{(tiles.originX() + ex) * 1000 + 500, (tiles.originY() + ey) * 1000 + 500};
			for (const TerrainRing& ring : first.rings) {
				if (geometry::pointInPolygon(center, ring.ring) != geometry::PointInPolygon::Outside) {
					tiles.at(ex, ey).surface = Surface::Water;
				}
			}
		}
	}
	const ChunkTerrainPolygons polys = buildHand(tiles, kSeed, rivers);
	int						   bars	 = 0;
	for (int32_t ey = 0; ey < tiles.size; ++ey) {
		for (int32_t ex = 0; ex < tiles.size; ++ex) {
			if (polys.barTiles.test(tiles.originX() + ex, tiles.originY() + ey)) {
				++bars;
				EXPECT_NE(tiles.at(ex, ey).surface, Surface::Water);
			}
		}
	}
	EXPECT_GT(bars, 10);
}

// A bar fades out over a mouth flare as the bend asymmetry does (D7): with a
// lake taking the bend's second half (raw tiles left as land, so only the fade
// can take a bar away), no bar grows past the mouth, and the flare upstream of
// it narrows the bar the open bend has there.
TEST(TerrainPolygonBarsTest, ABarFadesOutOverAMouthFlare) {
	const auto rivers = bendRiver(kRadius);
	auto	   lake	  = [](int64_t tx, int64_t ty) {
		  return tx >= 180 && tx < 330 && ty >= 256 && ty < 330 ? Biome::Lake : Biome::TemperateGrassland;
	};
	HandTiles lakeTiles({0, 0}, lake);
	for (TileData& tile : lakeTiles.tiles) {
		tile.surface = Surface::Grass;
	}
	const ChunkTerrainPolygons open	  = buildHand(HandTiles({0, 0}, Biome::TemperateGrassland), kSeed, rivers);
	const ChunkTerrainPolygons mouth  = buildHand(lakeTiles, kSeed, rivers);
	auto					   pastIt = [](double, double y) { return y > 259.0; };
	auto					   flared = [](double x, double y) { return y < 256.0 && x > kCenterX; };
	EXPECT_GT(barCenters(open, pastIt).size(), 5U);
	EXPECT_TRUE(barCenters(mouth, pastIt).empty());
	EXPECT_LT(barCenters(mouth, flared).size(), barCenters(open, flared).size());
}
