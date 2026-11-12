#include "world/chunk/SyntheticWorldSampler.h"

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/TerrainPolygonBuilder.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>

namespace world {

	namespace {
		constexpr uint64_t kSeed = 7;

		std::unique_ptr<ChunkManager> loadOriginChunk(SyntheticMap map) {
			auto chunks = std::make_unique<ChunkManager>(std::make_unique<SyntheticWorldSampler>(kSeed, std::move(map)));
			chunks->setLoadRadius(0);
			chunks->update({0.0F, 0.0F});
			chunks->finishPendingGeneration();
			return chunks;
		}
	} // namespace

	TEST(SyntheticWorldSamplerTest, WithoutAPondTheMapIsDryLevelGround) {
		const auto	 chunks = loadOriginChunk({});
		const Chunk* chunk = chunks->getChunk({0, 0});
		ASSERT_NE(chunk, nullptr);
		EXPECT_TRUE(chunk->terrainPolygons().rings.empty()) << "no water ring anywhere";
		EXPECT_TRUE(chunk->terrainPolygons().shorePoints.empty());

		const uint16_t elevation = chunk->getTile(0, 0).elevation;
		for (uint16_t y = 0; y < kChunkSize; y += 37) {
			for (uint16_t x = 0; x < kChunkSize; x += 37) {
				const TileData& tile = chunk->getTile(x, y);
				EXPECT_EQ(tile.primaryBiome, Biome::TemperateGrassland);
				EXPECT_EQ(tile.waterDepth, 0);
				EXPECT_EQ(tile.elevation, elevation);
			}
		}
	}

	// Shore points are where VisionSystem remembers drinkable water: on land, just off the blob's rim.
	TEST(SyntheticWorldSamplerTest, PondShorePointsFollowTheBlobRim) {
		const worldgen::PondNetwork2D::Pond pond{.cx = 200.0, .cy = 200.0, .radius = 15.0F, .phaseA = 0.0F, .phaseB = 0.0F, .depth = 120};
		const auto							chunks = loadOriginChunk({.pond = pond});
		const Chunk*						chunk = chunks->getChunk({0, 0});
		ASSERT_NE(chunk, nullptr);

		const auto& shorePoints = chunk->terrainPolygons().shorePoints;
		// A point about every meter around a rim at least 10 m out.
		ASSERT_GT(shorePoints.size(), 60U);
		const double kOffsetM = static_cast<double>(TerrainPolygonBuilder::kShoreOffsetMm) / 1000.0;
		for (const auto& p : shorePoints) {
			const double dx = static_cast<double>(p.x) / 1000.0 - pond.cx;
			const double dy = static_cast<double>(p.y) / 1000.0 - pond.cy;
			const double rim = worldgen::PondNetwork2D::rimRadiusAt(pond, std::atan2(dy, dx));
			EXPECT_NEAR(std::hypot(dx, dy), rim + kOffsetM, 0.5) << p.x << ", " << p.y;
		}
	}

	// Chunks gather the pond out to kRiverGatherMarginM past their square, as GeneratedWorldSampler does.
	TEST(SyntheticWorldSamplerTest, PondIsGatheredWithinTheWorldSamplerMargin) {
		const worldgen::PondNetwork2D::Pond pond{.cx = 60.0, .cy = 0.0, .radius = 15.0F, .depth = 120};
		const SyntheticWorldSampler			sampler(kSeed, {.pond = pond});
		EXPECT_EQ(sampler.sampleChunk({0, 0}).pondBlobs.size(), 1U);
		EXPECT_EQ(sampler.sampleChunk({0, -1}).pondBlobs.size(), 1U) << "the pond straddles y = 0";
		EXPECT_EQ(sampler.sampleChunk({-1, 0}).pondBlobs.size(), 1U) << "60 m off its square, inside the margin";
		EXPECT_TRUE(sampler.sampleChunk({-2, 0}).pondBlobs.empty()) << "572 m off its square";
		EXPECT_TRUE(sampler.sampleChunk({3, 3}).pondBlobs.empty());
	}

} // namespace world
