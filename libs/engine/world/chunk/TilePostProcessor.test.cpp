// TilePostProcessor's final surfaces (terrain-polygons-architecture.md D11,
// D12): mud in three distance bands off the water, only on ground that can hold
// it, and never on a point bar, which is Sand whatever it was.

#include "world/chunk/TilePostProcessor.h"

#include "world/chunk/Chunk.h"
#include "world/chunk/TerrainPolygonQuery.h"
#include "world/chunk/TerrainPolygonTestSupport.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>

using namespace engine::world;
using namespace engine::world::terrain_test;

namespace {

	constexpr uint64_t kSeed = 0x4D554431ULL;

	// A round lake 80 m across in the middle of chunk (0, 0).
	Biome roundLake(int64_t tx, int64_t ty) {
		const double dx = static_cast<double>(tx) + 0.5 - 256.0;
		const double dy = static_cast<double>(ty) + 0.5 - 256.0;
		return dx * dx + dy * dy < 40.0 * 40.0 ? Biome::Lake : Biome::TemperateGrassland;
	}

	Surface finalOf(const TerrainPolygonQuery& terrain, Surface raw, int64_t tx, int64_t ty) {
		return TilePostProcessor::finalSurface({.raw = raw, .tileX = tx, .tileY = ty, .terrain = &terrain, .worldSeed = kSeed});
	}

	Surface rawOf(const HandTiles& tiles, int64_t tx, int64_t ty) {
		return tiles.at(static_cast<int32_t>(tx - tiles.originX()), static_cast<int32_t>(ty - tiles.originY())).surface;
	}

	Vec2i64 tileCenter(int64_t tx, int64_t ty) {
		return {tx * 1000 + 500, ty * 1000 + 500};
	}

	// The D11 band a distance falls in; kMudBands.size() past the last.
	size_t bandOf(double distanceMm) {
		for (size_t b = 0; b < TilePostProcessor::kMudBands.size(); ++b) {
			if (distanceMm <= TilePostProcessor::kMudBands[b].reachMm) {
				return b;
			}
		}
		return TilePostProcessor::kMudBands.size();
	}

} // namespace

// Around a lake, the share of grass that turns to mud in each band is that
// band's probability, and none turns past the last band.
TEST(TilePostProcessor, MudFollowsDistanceToWaterInBands) {
	const HandTiles			   tiles({0, 0}, roundLake);
	const ChunkTerrainPolygons polys = buildHand(tiles, kSeed);
	const TerrainPolygonQuery  terrain(polys);

	std::array<int, 4> total{};
	std::array<int, 4> mud{};
	for (int64_t ty = 200; ty < 312; ++ty) {
		for (int64_t tx = 200; tx < 312; ++tx) {
			const Surface raw = rawOf(tiles, tx, ty);
			const Surface out = finalOf(terrain, raw, tx, ty);
			if (raw == Surface::Water) {
				EXPECT_EQ(out, Surface::Water);
				continue;
			}
			const size_t band = bandOf(terrain.distanceToWaterMm(tileCenter(tx, ty)));
			++total[band];
			mud[band] += out == Surface::Mud ? 1 : 0;
		}
	}
	for (size_t b = 0; b < TilePostProcessor::kMudBands.size(); ++b) {
		ASSERT_GT(total[b], 150) << "band " << b;
		EXPECT_NEAR(static_cast<double>(mud[b]) / static_cast<double>(total[b]), TilePostProcessor::kMudBands[b].probability, 0.07)
			<< "band " << b;
	}
	EXPECT_GT(total[3], 1000);
	EXPECT_EQ(mud[3], 0) << "no mud past the last band";
}

// Grass variants, Dirt, and Sand take the same roll (a pond or oasis in a
// desert sits on Sand, and its bank must still mud over for the riparian flora
// keyed on near="Mud"); Rock and Snow never turn.
TEST(TilePostProcessor, OnlyGrassDirtAndSandTurnToMud) {
	const HandTiles			   tiles({0, 0}, roundLake);
	const ChunkTerrainPolygons polys = buildHand(tiles, kSeed);
	const TerrainPolygonQuery  terrain(polys);

	int sandToMud = 0;
	for (int64_t ty = 200; ty < 312; ++ty) {
		for (int64_t tx = 200; tx < 312; ++tx) {
			if (rawOf(tiles, tx, ty) == Surface::Water || terrain.distanceToWaterMm(tileCenter(tx, ty)) > 1000.0) {
				continue;
			}
			const bool mud = finalOf(terrain, Surface::Grass, tx, ty) == Surface::Mud;
			for (const Surface raw : {Surface::Dirt, Surface::Sand, Surface::GrassTall, Surface::GrassShort, Surface::GrassMeadow}) {
				EXPECT_EQ(finalOf(terrain, raw, tx, ty), mud ? Surface::Mud : raw);
			}
			for (const Surface raw : {Surface::Rock, Surface::Snow}) {
				EXPECT_EQ(finalOf(terrain, raw, tx, ty), raw);
			}
			sandToMud += mud ? 1 : 0;
		}
	}
	EXPECT_GT(sandToMud, 150);
}

// A point bar's tiles are Sand whatever they were, even within a meter of the
// river, where Sand would otherwise mud over: the bar exemption (D12).
TEST(TilePostProcessor, PointBarsAreSandAndStayOffTheMud) {
	const HandTiles			   tiles({0, 0}, Biome::TemperateGrassland);
	const auto				   segments = riverSegmentsThrough(uBend(256.0, 256.0, 14.0, 60.0, 2.0), 4.0F);
	const ChunkTerrainPolygons polys	= buildHand(tiles, kSeed, segments);
	const TerrainPolygonQuery  terrain(polys);

	int bars		= 0;
	int barsByWater = 0;
	for (int64_t ty = 200; ty < 320; ++ty) {
		for (int64_t tx = 180; tx < 300; ++tx) {
			if (!terrain.isPointBar(tx, ty)) {
				continue;
			}
			++bars;
			for (const Surface raw : {Surface::Grass, Surface::Sand, Surface::Dirt, Surface::Rock}) {
				EXPECT_EQ(finalOf(terrain, raw, tx, ty), Surface::Sand);
			}
			barsByWater += terrain.distanceToWaterMm(tileCenter(tx, ty)) <= 1000.0 ? 1 : 0;
		}
	}
	EXPECT_GT(bars, 10);
	EXPECT_GT(barsByWater, 0) << "the bar hugs the inner bank, inside the surest mud band";
}
