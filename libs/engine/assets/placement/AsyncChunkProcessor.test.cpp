#include "AsyncChunkProcessor.h"

#include "PlacementExecutor.h"

#include <assets/AssetRegistry.h>

#include <world/chunk/Chunk.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/ChunkSampleResult.h>
#include <world/chunk/IWorldSampler.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_set>
#include <vector>

using namespace engine::assets;

namespace {

	// The seed of the shipped quickstart planet, so the world under test is the one a
	// player lands on.
	constexpr uint64_t kQuickstartSeed = 424242;
	constexpr uint64_t kOtherWorldSeed = 987654321;

	constexpr auto kTaskTimeout = std::chrono::seconds(60);

	// Every tile is grassland, so what a chunk grows is decided by the world seed alone.
	class SeededGrasslandSampler : public engine::world::IWorldSampler {
	  public:
		explicit SeededGrasslandSampler(uint64_t worldSeed)
			: seed(worldSeed) {}

		[[nodiscard]] engine::world::ChunkSampleResult sampleChunk(engine::world::ChunkCoordinate /*coord*/) const override {
			return engine::world::makeUniformChunkSampleResult(
				engine::world::BiomeWeights::single(engine::world::Biome::TemperateGrassland), 1.0F
			);
		}
		[[nodiscard]] float	   sampleElevation(engine::world::WorldPosition /*pos*/) const override { return 1.0F; }
		[[nodiscard]] uint64_t getWorldSeed() const override { return seed; }

	  private:
		uint64_t seed;
	};

	// One ready chunk at the origin of a world with the given seed.
	std::unique_ptr<engine::world::ChunkManager> loadOriginChunk(uint64_t worldSeed) {
		auto chunks = std::make_unique<engine::world::ChunkManager>(std::make_unique<SeededGrasslandSampler>(worldSeed));
		chunks->setLoadRadius(0);
		chunks->update({0.0F, 0.0F});
		chunks->finishPendingGeneration();
		return chunks;
	}

	// Spaced like a tree stand, so both uses of the seed in placement show up: the
	// per-chunk RNG and the grove field that thins the stand into glades. Marked
	// groundcover so the worker's bake skips it; nothing generates a template for this
	// def, and the bake would log an error for every instance it looked up.
	AssetDefinition makeStandDef() {
		AssetDefinition def;
		def.defName = "Groundcover_TestStand";
		def.role = AssetRole::Groundcover;

		BiomePlacement placement;
		placement.biomeName = engine::world::biomeToString(engine::world::Biome::TemperateGrassland);
		placement.spawnChance = 0.05F;
		placement.distribution = Distribution::Spaced;
		placement.spacing.minDistance = 8.0F;
		def.placement.biomes.push_back(placement);
		return def;
	}

	// x, y, defName, rotation, scale, brightness: the fields placement rolls, in a
	// sortable form so two layouts compare regardless of the index's iteration order.
	using Placement = std::tuple<float, float, std::string, float, float, float>;

	std::vector<Placement> layoutOf(const std::vector<PlacedEntity>& entities) {
		std::vector<Placement> layout;
		layout.reserve(entities.size());
		for (const PlacedEntity& entity : entities) {
			layout.emplace_back(entity.position.x, entity.position.y, entity.defName, entity.rotation, entity.scale, entity.colorTint.r);
		}
		std::sort(layout.begin(), layout.end());
		return layout;
	}

	::testing::AssertionResult sameLayout(const std::vector<Placement>& actual, const std::vector<Placement>& expected) {
		if (actual == expected) {
			return ::testing::AssertionSuccess();
		}
		std::vector<Placement> shared;
		std::set_intersection(actual.begin(), actual.end(), expected.begin(), expected.end(), std::back_inserter(shared));
		return ::testing::AssertionFailure() << actual.size() << " entities placed against " << expected.size() << " expected, "
											 << shared.size() << " in common";
	}

	// The reference: placement computed straight from the executor with `seed`, no processor involved.
	std::vector<Placement> placeDirectly(const PlacementExecutor& executor, const engine::world::Chunk& chunk, uint64_t seed) {
		ChunkPlacementContext ctx;
		ctx.coord = chunk.coordinate();
		ctx.worldSeed = seed;
		ctx.getBiome = [&chunk](uint16_t x, uint16_t y) {
			return chunk.getTile(x, y).primaryBiome;
		};
		// The sampled world is all land, so the polygon water test the processor uses is false everywhere too.
		ctx.isWater = [](glm::vec2 /*pos*/) {
			return false;
		};
		ctx.landSurface = [](glm::vec2 /*pos*/) {
			return engine::world::Surface::Grass;
		};
		return layoutOf(executor.computeChunkEntities(ctx).entities);
	}

	// Launch the chunk and pump the processor the way the game loop does, until it is integrated.
	::testing::AssertionResult placeAndWait(AsyncChunkProcessor& processor, const engine::world::Chunk& chunk) {
		processor.launchTask(&chunk);
		if (!processor.isProcessing(chunk.coordinate())) {
			return ::testing::AssertionFailure() << "launchTask did not start the chunk";
		}
		const auto deadline = std::chrono::steady_clock::now() + kTaskTimeout;
		while (processor.hasPending()) {
			processor.pollCompleted();
			if (std::chrono::steady_clock::now() > deadline) {
				return ::testing::AssertionFailure() << "placement task never finished";
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
		return ::testing::AssertionSuccess();
	}

	class AsyncChunkProcessorTests : public ::testing::Test {
	  protected:
		// Generating a chunk is the slow part, so each world is built once for the suite.
		static void SetUpTestSuite() {
			quickstartWorld = loadOriginChunk(kQuickstartSeed);
			otherWorld = loadOriginChunk(kOtherWorldSeed);
		}
		static void TearDownTestSuite() {
			quickstartWorld.reset();
			otherWorld.reset();
		}

		void SetUp() override {
			AssetRegistry& registry = AssetRegistry::Get();
			registry.clear();
			registry.registerTestDefinition(makeStandDef());

			executor = std::make_unique<PlacementExecutor>(registry);
			executor->initialize();
			processedChunks.clear();
		}
		void TearDown() override { AssetRegistry::Get().clear(); }

		static const engine::world::Chunk* quickstartChunk() { return quickstartWorld->getChunk({0, 0}); }
		static const engine::world::Chunk* otherChunk() { return otherWorld->getChunk({0, 0}); }

		// What the executor holds for the chunk once a processor has integrated it.
		[[nodiscard]] std::vector<Placement> storedLayout(const engine::world::Chunk& chunk) const {
			const SpatialIndex* index = executor->getChunkIndex(chunk.coordinate());
			return index != nullptr ? layoutOf(index->allEntities()) : std::vector<Placement>{};
		}

		// GameScene drops the placement data of a chunk that scrolls out of the load radius.
		void evict(const engine::world::Chunk& chunk) {
			executor->unloadChunk(chunk.coordinate());
			processedChunks.erase(chunk.coordinate());
		}

		inline static std::unique_ptr<engine::world::ChunkManager> quickstartWorld;
		inline static std::unique_ptr<engine::world::ChunkManager> otherWorld;

		std::unique_ptr<PlacementExecutor>				   executor;
		std::unordered_set<engine::world::ChunkCoordinate> processedChunks;
	};

} // namespace

// A chunk is placed from the seed its own world carries.
TEST_F(AsyncChunkProcessorTests, PlacesWithTheChunksOwnSeed) {
	const engine::world::Chunk* chunk = quickstartChunk();
	ASSERT_NE(chunk, nullptr);
	ASSERT_EQ(chunk->worldSeed(), kQuickstartSeed);

	AsyncChunkProcessor processor(*executor, processedChunks);
	ASSERT_TRUE(placeAndWait(processor, *chunk));

	const std::vector<Placement> expected = placeDirectly(*executor, *chunk, kQuickstartSeed);
	ASSERT_FALSE(expected.empty()) << "the test world should grow something";
	EXPECT_TRUE(sameLayout(storedLayout(*chunk), expected));
	EXPECT_EQ(processedChunks.count(chunk->coordinate()), 1u);
}

// One processor serves every chunk of a session, so the seed has to come from the chunk:
// two worlds put through the same processor come out as two layouts.
TEST_F(AsyncChunkProcessorTests, EachChunkKeepsItsOwnWorldsLayout) {
	const engine::world::Chunk* quickstart = quickstartChunk();
	const engine::world::Chunk* other = otherChunk();
	ASSERT_NE(quickstart, nullptr);
	ASSERT_NE(other, nullptr);
	ASSERT_EQ(quickstart->coordinate(), other->coordinate());

	AsyncChunkProcessor processor(*executor, processedChunks);

	ASSERT_TRUE(placeAndWait(processor, *quickstart));
	const std::vector<Placement> quickstartLayout = storedLayout(*quickstart);
	evict(*quickstart);

	ASSERT_TRUE(placeAndWait(processor, *other));
	const std::vector<Placement> otherLayout = storedLayout(*other);

	EXPECT_TRUE(sameLayout(quickstartLayout, placeDirectly(*executor, *quickstart, kQuickstartSeed)));
	EXPECT_TRUE(sameLayout(otherLayout, placeDirectly(*executor, *other, kOtherWorldSeed)));
	EXPECT_TRUE(quickstartLayout != otherLayout) << "different seeds should not grow the same chunk";
}

// The reported symptom: a chunk that scrolls out and is loaded again comes back different.
// GameLoadingScene places the first chunks and GameScene every one after, each with a processor of its own.
TEST_F(AsyncChunkProcessorTests, ReloadedChunkKeepsItsLayout) {
	const engine::world::Chunk* chunk = quickstartChunk();
	ASSERT_NE(chunk, nullptr);

	AsyncChunkProcessor loading(*executor, processedChunks);
	ASSERT_TRUE(placeAndWait(loading, *chunk));
	const std::vector<Placement> atLoad = storedLayout(*chunk);
	ASSERT_FALSE(atLoad.empty());
	evict(*chunk);

	AsyncChunkProcessor streaming(*executor, processedChunks);
	ASSERT_TRUE(placeAndWait(streaming, *chunk));
	EXPECT_TRUE(sameLayout(storedLayout(*chunk), atLoad));
}
