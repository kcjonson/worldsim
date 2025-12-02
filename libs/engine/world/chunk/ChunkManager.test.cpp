#include "ChunkManager.h"
#include "MockWorldSampler.h"

#include <gtest/gtest.h>

using namespace engine::world;

// ============================================================================
// ChunkManager Basic Tests
// ============================================================================

class ChunkManagerTest : public ::testing::Test {
  protected:
	void SetUp() override {
		auto sampler = std::make_unique<MockWorldSampler>(kTestSeed);
		manager = std::make_unique<ChunkManager>(std::move(sampler));
	}

	static constexpr uint64_t kTestSeed = 12345;
	std::unique_ptr<ChunkManager> manager;
};

TEST_F(ChunkManagerTest, InitiallyEmpty) {
	EXPECT_EQ(manager->loadedChunkCount(), 0);
}

TEST_F(ChunkManagerTest, InitialCenterChunk) {
	ChunkCoordinate center = manager->centerChunk();

	EXPECT_EQ(center.x, 0);
	EXPECT_EQ(center.y, 0);
}

TEST_F(ChunkManagerTest, DefaultLoadRadius) {
	EXPECT_EQ(manager->loadRadius(), 2);
}

TEST_F(ChunkManagerTest, DefaultUnloadRadius) {
	EXPECT_EQ(manager->unloadRadius(), 4);
}

// ============================================================================
// Loading Tests
// ============================================================================

TEST_F(ChunkManagerTest, UpdateLoadsChunksAtOrigin) {
	manager->update(WorldPosition(0.0F, 0.0F));

	// With radius 2, should have 5x5 = 25 chunks
	EXPECT_GT(manager->loadedChunkCount(), 0);
}

TEST_F(ChunkManagerTest, UpdateLoadsCenterChunk) {
	manager->update(WorldPosition(256.0F, 256.0F));

	// Center chunk (0, 0) should be loaded
	const Chunk* chunk = manager->getChunk(ChunkCoordinate(0, 0));
	EXPECT_NE(chunk, nullptr);
}

TEST_F(ChunkManagerTest, LoadsCorrectNumberOfChunks) {
	manager->setLoadRadius(1);
	manager->update(WorldPosition(256.0F, 256.0F));

	// With radius 1: 3x3 = 9 chunks
	EXPECT_EQ(manager->loadedChunkCount(), 9);
}

TEST_F(ChunkManagerTest, LoadRadiusAffectsChunkCount) {
	manager->setLoadRadius(2);
	manager->update(WorldPosition(0.0F, 0.0F));

	size_t countRadius2 = manager->loadedChunkCount();

	// Reset
	manager = std::make_unique<ChunkManager>(std::make_unique<MockWorldSampler>(kTestSeed));
	manager->setLoadRadius(1);
	manager->update(WorldPosition(0.0F, 0.0F));

	size_t countRadius1 = manager->loadedChunkCount();

	EXPECT_GT(countRadius2, countRadius1);
}

TEST_F(ChunkManagerTest, GetChunkReturnsNullForUnloaded) {
	// Don't call update
	const Chunk* chunk = manager->getChunk(ChunkCoordinate(0, 0));

	EXPECT_EQ(chunk, nullptr);
}

TEST_F(ChunkManagerTest, GetChunkReturnsLoadedChunk) {
	manager->update(WorldPosition(256.0F, 256.0F));

	const Chunk* chunk = manager->getChunk(ChunkCoordinate(0, 0));
	EXPECT_NE(chunk, nullptr);
}

TEST_F(ChunkManagerTest, LoadedChunksHaveCorrectCoordinate) {
	manager->update(WorldPosition(256.0F, 256.0F));

	const Chunk* chunk = manager->getChunk(ChunkCoordinate(0, 0));
	ASSERT_NE(chunk, nullptr);
	EXPECT_EQ(chunk->coordinate().x, 0);
	EXPECT_EQ(chunk->coordinate().y, 0);
}

// ============================================================================
// Unloading Tests
// ============================================================================

TEST_F(ChunkManagerTest, MovingCameraUnloadsDistantChunks) {
	// First load at origin
	manager->setLoadRadius(1);
	manager->setUnloadRadius(2);
	manager->update(WorldPosition(0.0F, 0.0F));

	// Chunk at origin should be loaded
	EXPECT_NE(manager->getChunk(ChunkCoordinate(0, 0)), nullptr);

	// Move camera far away (beyond unload radius)
	manager->update(WorldPosition(5000.0F, 5000.0F));

	// Chunk at origin should now be unloaded (distance > unload radius)
	EXPECT_EQ(manager->getChunk(ChunkCoordinate(0, 0)), nullptr);
}

TEST_F(ChunkManagerTest, ChunksWithinUnloadRadiusRemain) {
	manager->setLoadRadius(1);
	manager->setUnloadRadius(3);
	manager->update(WorldPosition(256.0F, 256.0F));

	// Move camera slightly (chunk 0,0 to chunk 1,1)
	manager->update(WorldPosition(768.0F, 768.0F));

	// Chunk (0, 0) is 1.41 chunks away (chebyshev = 1), should still be loaded
	EXPECT_NE(manager->getChunk(ChunkCoordinate(0, 0)), nullptr);
}

// ============================================================================
// Center Chunk Tracking Tests
// ============================================================================

TEST_F(ChunkManagerTest, CenterChunkUpdatesWithCamera) {
	manager->update(WorldPosition(0.0F, 0.0F));
	EXPECT_EQ(manager->centerChunk(), ChunkCoordinate(0, 0));

	manager->update(WorldPosition(600.0F, 0.0F));
	EXPECT_EQ(manager->centerChunk(), ChunkCoordinate(1, 0));

	manager->update(WorldPosition(-600.0F, -600.0F));
	EXPECT_EQ(manager->centerChunk(), ChunkCoordinate(-2, -2));
}

// ============================================================================
// GetLoadedChunks Tests
// ============================================================================

TEST_F(ChunkManagerTest, GetLoadedChunksReturnsAllChunks) {
	manager->setLoadRadius(1);
	manager->update(WorldPosition(256.0F, 256.0F));

	auto chunks = manager->getLoadedChunks();

	EXPECT_EQ(chunks.size(), manager->loadedChunkCount());
}

TEST_F(ChunkManagerTest, GetLoadedChunksConstVersion) {
	manager->setLoadRadius(1);
	manager->update(WorldPosition(256.0F, 256.0F));

	const ChunkManager& constManager = *manager;
	auto chunks = constManager.getLoadedChunks();

	EXPECT_EQ(chunks.size(), manager->loadedChunkCount());
}

// ============================================================================
// GetVisibleChunks Tests
// ============================================================================

TEST_F(ChunkManagerTest, GetVisibleChunksBasic) {
	manager->setLoadRadius(2);
	manager->update(WorldPosition(256.0F, 256.0F));

	// Query a rectangle that covers chunk (0, 0)
	auto visibleChunks = manager->getVisibleChunks(WorldPosition(0.0F, 0.0F), WorldPosition(512.0F, 512.0F));

	// Should include at least chunk (0, 0)
	EXPECT_GE(visibleChunks.size(), 1);
}

TEST_F(ChunkManagerTest, GetVisibleChunksReturnsOnlyLoadedChunks) {
	manager->setLoadRadius(1);
	manager->update(WorldPosition(256.0F, 256.0F));

	// Query a very large area
	auto visibleChunks = manager->getVisibleChunks(WorldPosition(-10000.0F, -10000.0F), WorldPosition(10000.0F, 10000.0F));

	// Should return all loaded chunks (none outside the loaded area)
	EXPECT_EQ(visibleChunks.size(), manager->loadedChunkCount());
}

// ============================================================================
// Configuration Tests
// ============================================================================

TEST_F(ChunkManagerTest, SetLoadRadius) {
	manager->setLoadRadius(5);
	EXPECT_EQ(manager->loadRadius(), 5);
}

TEST_F(ChunkManagerTest, SetUnloadRadius) {
	manager->setUnloadRadius(7);
	EXPECT_EQ(manager->unloadRadius(), 7);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ChunkManagerTest, LoadAtNegativeCoordinates) {
	manager->setLoadRadius(1);
	manager->update(WorldPosition(-256.0F, -256.0F));

	// Chunk (-1, -1) should be loaded
	EXPECT_NE(manager->getChunk(ChunkCoordinate(-1, -1)), nullptr);
}

TEST_F(ChunkManagerTest, MultipleUpdatesIdempotent) {
	manager->setLoadRadius(1);

	manager->update(WorldPosition(256.0F, 256.0F));
	size_t count1 = manager->loadedChunkCount();

	manager->update(WorldPosition(256.0F, 256.0F));
	size_t count2 = manager->loadedChunkCount();

	// Same position should not change loaded count
	EXPECT_EQ(count1, count2);
}

TEST_F(ChunkManagerTest, RapidCameraMovement) {
	manager->setLoadRadius(1);
	manager->setUnloadRadius(2);

	// Simulate rapid camera movement
	for (int i = 0; i < 10; i++) {
		float x = static_cast<float>(i * 600);
		manager->update(WorldPosition(x, 0.0F));
	}

	// Should still have a reasonable number of chunks
	EXPECT_GT(manager->loadedChunkCount(), 0);
	EXPECT_LE(manager->loadedChunkCount(), 25);  // Shouldn't accumulate too many
}
