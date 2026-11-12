#include "ChunkManager.h"
#include "MockWorldSampler.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <vector>

using namespace world;

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
	// Each loaded chunk generates its 262k tiles on a worker and the manager's
	// destructor waits for them, so a test loads only as many chunks as it checks.
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

TEST_F(ChunkManagerTest, AnchorRangesFollowTheAnchorsOwnRange) {
	EXPECT_EQ(ChunkManager::anchorLoadRange(46.0F), 128.0F);
	EXPECT_EQ(ChunkManager::anchorKeepRange(46.0F), 256.0F);
	EXPECT_EQ(ChunkManager::anchorLoadRange(300.0F), 300.0F);
	EXPECT_EQ(ChunkManager::anchorKeepRange(300.0F), 600.0F);
}

// ============================================================================
// Loading Tests
// ============================================================================

TEST_F(ChunkManagerTest, UpdateLoadsChunksAtOrigin) {
	manager->setLoadRadius(0);
	manager->update(WorldPosition(0.0F, 0.0F));

	EXPECT_GT(manager->loadedChunkCount(), 0);
}

TEST_F(ChunkManagerTest, LoadsCorrectNumberOfChunks) {
	manager->setLoadRadius(1);
	manager->update(WorldPosition(256.0F, 256.0F));

	// With radius 1: 3x3 = 9 chunks
	EXPECT_EQ(manager->loadedChunkCount(), 9);
}

TEST_F(ChunkManagerTest, LoadRadiusAffectsChunkCount) {
	manager->setLoadRadius(1);
	manager->update(WorldPosition(0.0F, 0.0F));

	size_t countRadius1 = manager->loadedChunkCount();

	// Reset
	manager = std::make_unique<ChunkManager>(std::make_unique<MockWorldSampler>(kTestSeed));
	manager->setLoadRadius(0);
	manager->update(WorldPosition(0.0F, 0.0F));

	size_t countRadius0 = manager->loadedChunkCount();

	EXPECT_GT(countRadius1, countRadius0);
}

TEST_F(ChunkManagerTest, GetChunkReturnsNullForUnloaded) {
	// Don't call update
	const Chunk* chunk = manager->getChunk(ChunkCoordinate(0, 0));

	EXPECT_EQ(chunk, nullptr);
}

TEST_F(ChunkManagerTest, LoadedChunksHaveCorrectCoordinate) {
	manager->setLoadRadius(0);
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
	manager->setLoadRadius(0);
	manager->setUnloadRadius(1);
	manager->update(WorldPosition(0.0F, 0.0F));

	// Chunk at origin should be loaded
	EXPECT_NE(manager->getChunk(ChunkCoordinate(0, 0)), nullptr);

	// Chunks mid-generation are never unloaded; drain the workers first
	manager->finishPendingGeneration();

	// Move camera far away (beyond unload radius)
	manager->update(WorldPosition(5000.0F, 5000.0F));

	// Chunk at origin should now be unloaded (distance > unload radius)
	EXPECT_EQ(manager->getChunk(ChunkCoordinate(0, 0)), nullptr);
}

TEST_F(ChunkManagerTest, ChunksWithinUnloadRadiusRemain) {
	manager->setLoadRadius(0);
	manager->setUnloadRadius(3);
	manager->update(WorldPosition(256.0F, 256.0F));

	// Chunks mid-generation are never unloaded; drain the workers first
	manager->finishPendingGeneration();

	// Move camera slightly (chunk 0,0 to chunk 1,1)
	manager->update(WorldPosition(768.0F, 768.0F));

	// Chunk (0, 0) is 1.41 chunks away (chebyshev = 1), should still be loaded
	EXPECT_NE(manager->getChunk(ChunkCoordinate(0, 0)), nullptr);
}

// ============================================================================
// Center Chunk Tracking Tests
// ============================================================================

TEST_F(ChunkManagerTest, CenterChunkUpdatesWithCamera) {
	manager->setLoadRadius(0);
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
	manager->setLoadRadius(0);
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
// Simulation Anchor Tests
// ============================================================================
//
// The camera sits far away with load radius 0, so it holds exactly one chunk of its
// own and every other loaded chunk is the anchors' doing. Anchors are colonists
// (R = 30 m sight + 16 m buffer) unless a test says otherwise.

class ChunkManagerAnchorTest : public ChunkManagerTest {
  protected:
	void SetUp() override {
		ChunkManagerTest::SetUp();
		manager->setLoadRadius(0);
		manager->setUnloadRadius(0);
	}

	void update(std::initializer_list<WorldPosition> anchors, float rangeMeters = kColonistRange) {
		std::vector<ChunkAnchor> list;
		for (const WorldPosition& p : anchors) {
			list.push_back({p, rangeMeters});
		}
		manager->update(kFarCamera, list);
	}

	[[nodiscard]] bool loaded(int32_t x, int32_t y) const {
		return manager->getChunk(ChunkCoordinate(x, y)) != nullptr;
	}

	[[nodiscard]] size_t anchorChunkCount() const { return manager->loadedChunkCount() - 1; }

	inline static const WorldPosition kFarCamera{-100000.0F, -100000.0F};
	static constexpr float			  kColonistRange = 46.0F;
};

TEST_F(ChunkManagerTest, AnchoredChunkSurvivesCameraLeaving) {
	manager->setLoadRadius(1);
	manager->setUnloadRadius(2);
	const std::vector<ChunkAnchor> colony{{WorldPosition(256.0F, 256.0F), 46.0F}};

	manager->update(WorldPosition(256.0F, 256.0F), colony);
	manager->finishPendingGeneration();

	// Pan far away: the camera's ring unloads, the colony's chunk stays
	manager->update(WorldPosition(5000.0F, 5000.0F), colony);
	EXPECT_NE(manager->getChunk(ChunkCoordinate(0, 0)), nullptr);
	EXPECT_EQ(manager->getChunk(ChunkCoordinate(1, 0)), nullptr);
	EXPECT_EQ(manager->getChunk(ChunkCoordinate(-1, -1)), nullptr);
	EXPECT_EQ(manager->centerChunk(), ChunkCoordinate(9, 9));
}

TEST_F(ChunkManagerAnchorTest, ChunkLoadsOnlyWithinLoadRange) {
	// 129 m short of the (0,0)/(1,0) border: out of the 128 m load range
	update({WorldPosition(383.0F, 256.0F)});
	EXPECT_TRUE(loaded(0, 0));
	EXPECT_FALSE(loaded(1, 0));
	EXPECT_EQ(anchorChunkCount(), 1U);

	// 127 m short: in range
	update({WorldPosition(385.0F, 256.0F)});
	EXPECT_TRUE(loaded(1, 0));
	EXPECT_EQ(anchorChunkCount(), 2U);
}

// A mover that sees farther than a colonist (R = 250 m) loads out to R and keeps out to 2R.
TEST_F(ChunkManagerAnchorTest, LongSightedMoverHoldsChunksOutToItsRange) {
	constexpr float kRange = 250.0F;
	// 212 m short of the (0,0)/(1,0) border: past a colonist's 128 m, inside R
	update({WorldPosition(300.0F, 256.0F)}, kColonistRange);
	EXPECT_FALSE(loaded(1, 0));
	update({WorldPosition(300.0F, 256.0F)}, kRange);
	EXPECT_TRUE(loaded(1, 0));
	EXPECT_FALSE(loaded(-1, 0)) << "300 m from the west border is outside R";
	EXPECT_EQ(anchorChunkCount(), 2U);
	manager->finishPendingGeneration();
	const Chunk* east = manager->getChunk(ChunkCoordinate(1, 0));

	// 412 m from the border: a colonist would drop it (keep 256 m), this mover keeps it (2R = 500 m)
	update({WorldPosition(100.0F, 256.0F)}, kRange);
	manager->finishPendingGeneration();
	EXPECT_EQ(manager->getChunk(ChunkCoordinate(1, 0)), east);
	update({WorldPosition(100.0F, 256.0F)}, kColonistRange);
	EXPECT_FALSE(loaded(1, 0));

	// 507 m: past 2R
	update({WorldPosition(300.0F, 256.0F)}, kRange);
	manager->finishPendingGeneration();
	update({WorldPosition(5.0F, 256.0F)}, kRange);
	EXPECT_FALSE(loaded(1, 0));
}

TEST_F(ChunkManagerAnchorTest, PacingAcrossLoadBoundaryDoesNotThrash) {
	update({WorldPosition(385.0F, 256.0F)});
	manager->finishPendingGeneration();
	ASSERT_TRUE(loaded(1, 0));
	const Chunk* east = manager->getChunk(ChunkCoordinate(1, 0));

	// Back and forth across the load boundary, and out to the edge of the keep range:
	// the chunk loaded once and is held throughout
	for (const float x : {383.0F, 385.0F, 383.0F, 300.0F, 257.0F, 385.0F, 257.0F}) {
		update({WorldPosition(x, 256.0F)});
		EXPECT_EQ(manager->getChunk(ChunkCoordinate(1, 0)), east) << "anchor at x=" << x;
	}

	// 256 m from the border is outside the keep range
	update({WorldPosition(256.0F, 256.0F)});
	EXPECT_FALSE(loaded(1, 0));
	EXPECT_TRUE(loaded(0, 0));
}

TEST_F(ChunkManagerAnchorTest, AnchorNearCornerHoldsFourChunks) {
	update({WorldPosition(500.0F, 500.0F)});
	manager->finishPendingGeneration();
	EXPECT_TRUE(loaded(0, 0));
	EXPECT_TRUE(loaded(1, 0));
	EXPECT_TRUE(loaded(0, 1));
	EXPECT_TRUE(loaded(1, 1));
	EXPECT_EQ(anchorChunkCount(), 4U);

	// Halfway between the corner and the chunk center: all four are still kept
	update({WorldPosition(378.0F, 378.0F)});
	EXPECT_EQ(anchorChunkCount(), 4U);
}

TEST_F(ChunkManagerAnchorTest, RemovingAnchorLetsChunkUnload) {
	update({WorldPosition(256.0F, 256.0F)});
	manager->finishPendingGeneration();
	ASSERT_TRUE(loaded(0, 0));

	update({});
	EXPECT_FALSE(loaded(0, 0));
	EXPECT_EQ(anchorChunkCount(), 0U);
}

TEST_F(ChunkManagerAnchorTest, ManyAnchorsInOneChunkCostOneChunk) {
	// 1000 anchors spread over the middle of chunk (0,0), each over 128 m from every border
	std::vector<ChunkAnchor> crowd;
	for (int i = 0; i < 1000; ++i) {
		crowd.push_back({WorldPosition(200.0F + static_cast<float>(i % 40) * 2.8F, 200.0F + static_cast<float>(i / 40) * 4.4F), kColonistRange});
	}
	manager->update(kFarCamera, crowd);
	EXPECT_TRUE(loaded(0, 0));
	EXPECT_EQ(anchorChunkCount(), 1U);
}

TEST_F(ChunkManagerAnchorTest, GeneratingChunksAreNotUnloaded) {
	update({WorldPosition(256.0F, 256.0F)});
	const Chunk* chunk = manager->getChunk(ChunkCoordinate(0, 0));
	ASSERT_NE(chunk, nullptr);
	if (chunk->isReady()) {
		GTEST_SKIP() << "generation finished before the anchor could be dropped";
	}

	// Nothing holds the chunk any more, but its worker still references it
	update({});
	EXPECT_TRUE(loaded(0, 0));

	// Once the worker is done the next update releases it
	manager->finishPendingGeneration();
	update({});
	EXPECT_FALSE(loaded(0, 0));
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(ChunkManagerTest, LoadAtNegativeCoordinates) {
	manager->setLoadRadius(0);
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

	// Drain generation workers (in-flight chunks are exempt from unloading),
	// then move once more so the unload pass runs without that exemption
	manager->finishPendingGeneration();
	manager->update(WorldPosition(6000.0F, 0.0F));

	// Should still have a reasonable number of chunks
	EXPECT_GT(manager->loadedChunkCount(), 0);
	EXPECT_LE(manager->loadedChunkCount(), 25);  // Shouldn't accumulate too many
}
