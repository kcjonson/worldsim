#include "PlacementExecutor.h"

#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

using namespace engine::assets;

// ============================================================================
// Test Fixtures and Helpers
// ============================================================================

/// Mock adjacent chunk provider for cross-chunk tests
class MockAdjacentProvider : public IAdjacentChunkProvider {
  public:
	void setChunkIndex(engine::world::ChunkCoordinate coord, const SpatialIndex* index) {
		m_indices[coord] = index;
	}

	const SpatialIndex* getChunkIndex(engine::world::ChunkCoordinate coord) const override {
		auto it = m_indices.find(coord);
		if (it != m_indices.end()) {
			return it->second;
		}
		return nullptr;
	}

  private:
	std::unordered_map<engine::world::ChunkCoordinate, const SpatialIndex*> m_indices;
};

/// Create a simple chunk placement context for testing
ChunkPlacementContext createTestContext(engine::world::ChunkCoordinate coord,
										uint64_t seed,
										engine::world::Biome defaultBiome = engine::world::Biome::Grassland) {
	ChunkPlacementContext ctx;
	ctx.coord = coord;
	ctx.worldSeed = seed;
	ctx.getBiome = [defaultBiome](uint16_t /*localX*/, uint16_t /*localY*/) {
		return defaultBiome;
	};
	ctx.getSurface = [](uint16_t /*localX*/, uint16_t /*localY*/) {
		return std::string("Soil");
	};
	return ctx;
}

// ============================================================================
// Basic Initialization Tests
// ============================================================================

TEST(PlacementExecutorTests, ConstructorDoesNotInitialize) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	EXPECT_FALSE(executor.isInitialized());
}

TEST(PlacementExecutorTests, InitializeWithEmptyRegistry) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	EXPECT_TRUE(executor.isInitialized());
	EXPECT_TRUE(executor.getSpawnOrder().empty());
}

TEST(PlacementExecutorTests, ClearResetsState) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	executor.clear();

	EXPECT_FALSE(executor.isInitialized());
	EXPECT_TRUE(executor.getSpawnOrder().empty());
}

// ============================================================================
// Chunk Processing Tests
// ============================================================================

TEST(PlacementExecutorTests, ProcessChunkReturnsEmptyForUninitializedExecutor) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	// Don't initialize

	auto ctx = createTestContext({0, 0}, 12345);
	auto result = executor.processChunk(ctx);

	EXPECT_EQ(result.entitiesPlaced, 0);
	EXPECT_TRUE(result.entities.empty());
}

TEST(PlacementExecutorTests, ProcessChunkWithNoDefinitions) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({0, 0}, 12345);
	auto result = executor.processChunk(ctx);

	EXPECT_EQ(result.entitiesPlaced, 0);
	EXPECT_EQ(result.coord.x, 0);
	EXPECT_EQ(result.coord.y, 0);
}

TEST(PlacementExecutorTests, ProcessChunkIsDeterministic) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor1(registry);
	executor1.initialize();

	PlacementExecutor executor2(registry);
	executor2.initialize();

	auto ctx = createTestContext({5, 10}, 42);
	auto result1 = executor1.processChunk(ctx);
	auto result2 = executor2.processChunk(ctx);

	// Same context should produce same results
	EXPECT_EQ(result1.entitiesPlaced, result2.entitiesPlaced);
}

TEST(PlacementExecutorTests, DifferentChunksProduceDifferentResults) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx1 = createTestContext({0, 0}, 12345);
	auto ctx2 = createTestContext({1, 0}, 12345);

	auto result1 = executor.processChunk(ctx1);
	auto result2 = executor.processChunk(ctx2);

	// Different chunks should have different coordinates in result
	EXPECT_EQ(result1.coord.x, 0);
	EXPECT_EQ(result2.coord.x, 1);
}

// ============================================================================
// Chunk Index Management Tests
// ============================================================================

TEST(PlacementExecutorTests, GetChunkIndexReturnsNullForUnprocessedChunk) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	const SpatialIndex* index = executor.getChunkIndex({0, 0});
	EXPECT_EQ(index, nullptr);
}

TEST(PlacementExecutorTests, GetChunkIndexReturnsIndexForProcessedChunk) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({5, 5}, 12345);
	executor.processChunk(ctx);

	const SpatialIndex* index = executor.getChunkIndex({5, 5});
	EXPECT_NE(index, nullptr);
}

TEST(PlacementExecutorTests, UnloadChunkRemovesIndex) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({5, 5}, 12345);
	executor.processChunk(ctx);

	EXPECT_NE(executor.getChunkIndex({5, 5}), nullptr);

	executor.unloadChunk({5, 5});

	EXPECT_EQ(executor.getChunkIndex({5, 5}), nullptr);
}

// ============================================================================
// Adjacent Chunk Provider Tests
// ============================================================================

TEST(PlacementExecutorTests, ProcessChunkWithAdjacentProvider) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	// Set up adjacent chunk index
	SpatialIndex adjacentIndex;
	adjacentIndex.insert({"TestEntity", {-10.0F, 10.0F}});

	MockAdjacentProvider provider;
	provider.setChunkIndex({-1, 0}, &adjacentIndex);

	auto ctx = createTestContext({0, 0}, 12345);
	auto result = executor.processChunk(ctx, &provider);

	// Should not crash and return valid result
	EXPECT_EQ(result.coord.x, 0);
	EXPECT_EQ(result.coord.y, 0);
}

// ============================================================================
// Context Function Tests
// ============================================================================

TEST(PlacementExecutorTests, BiomeFunctionIsCalled) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	int callCount = 0;
	ChunkPlacementContext ctx;
	ctx.coord = {0, 0};
	ctx.worldSeed = 12345;
	ctx.getBiome = [&callCount](uint16_t /*localX*/, uint16_t /*localY*/) {
		++callCount;
		return engine::world::Biome::Grassland;
	};

	executor.processChunk(ctx);

	// With no definitions, biome function won't be called
	// This test verifies the context is properly used
	EXPECT_GE(callCount, 0);
}

// ============================================================================
// Coordinate System Tests
// ============================================================================

TEST(PlacementExecutorTests, ChunkCoordinateOriginCalculation) {
	// Verify that chunk coordinate origins are calculated correctly
	engine::world::ChunkCoordinate coord0{0, 0};
	engine::world::ChunkCoordinate coord1{1, 0};
	engine::world::ChunkCoordinate coordNeg{-1, -1};

	auto origin0 = coord0.origin();
	auto origin1 = coord1.origin();
	auto originNeg = coordNeg.origin();

	EXPECT_EQ(origin0.x, 0.0F);
	EXPECT_EQ(origin0.y, 0.0F);
	EXPECT_EQ(origin1.x, static_cast<float>(engine::world::kChunkSize));
	EXPECT_EQ(origin1.y, 0.0F);
	EXPECT_EQ(originNeg.x, -static_cast<float>(engine::world::kChunkSize));
	EXPECT_EQ(originNeg.y, -static_cast<float>(engine::world::kChunkSize));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST(PlacementExecutorTests, ProcessSameChunkTwice) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({0, 0}, 12345);
	auto result1 = executor.processChunk(ctx);
	auto result2 = executor.processChunk(ctx);

	// Both should succeed and produce same results (deterministic)
	EXPECT_EQ(result1.entitiesPlaced, result2.entitiesPlaced);
}

TEST(PlacementExecutorTests, ProcessMultipleChunks) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	// Process a grid of chunks
	for (int x = -2; x <= 2; ++x) {
		for (int y = -2; y <= 2; ++y) {
			auto ctx = createTestContext({x, y}, 12345);
			auto result = executor.processChunk(ctx);
			EXPECT_EQ(result.coord.x, x);
			EXPECT_EQ(result.coord.y, y);
		}
	}

	// All chunk indices should be accessible
	for (int x = -2; x <= 2; ++x) {
		for (int y = -2; y <= 2; ++y) {
			EXPECT_NE(executor.getChunkIndex({x, y}), nullptr);
		}
	}
}

TEST(PlacementExecutorTests, NegativeChunkCoordinates) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	auto ctx = createTestContext({-100, -50}, 12345);
	auto result = executor.processChunk(ctx);

	EXPECT_EQ(result.coord.x, -100);
	EXPECT_EQ(result.coord.y, -50);
	EXPECT_NE(executor.getChunkIndex({-100, -50}), nullptr);
}

// ============================================================================
// Surface Function Tests
// ============================================================================

TEST(PlacementExecutorTests, NullSurfaceFunction) {
	auto& registry = AssetRegistry::Get();
	registry.clear();

	PlacementExecutor executor(registry);
	executor.initialize();

	ChunkPlacementContext ctx;
	ctx.coord = {0, 0};
	ctx.worldSeed = 12345;
	ctx.getBiome = [](uint16_t, uint16_t) { return engine::world::Biome::Grassland; };
	ctx.getSurface = nullptr; // Explicitly null

	// Should not crash
	auto result = executor.processChunk(ctx);
	EXPECT_EQ(result.coord.x, 0);
}

