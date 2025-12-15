#include "resources/TileTextureAtlas.h"
#include <gtest/gtest.h>

using namespace Renderer;

// ============================================================================
// AtlasRegion Tests (no GL context required)
// ============================================================================

TEST(AtlasRegionTest, DefaultConstructor) {
	AtlasRegion region;
	EXPECT_EQ(region.x, 0);
	EXPECT_EQ(region.y, 0);
	EXPECT_EQ(region.width, 0);
	EXPECT_EQ(region.height, 0);
	EXPECT_FALSE(region.valid);
}

// ============================================================================
// TileTextureAtlas Tests
// Note: Tests requiring OpenGL context may be skipped in CI environments
// ============================================================================

class TileTextureAtlasTest : public ::testing::Test {
  protected:
	static bool hasGLContext() {
		// Simple heuristic: check if we can create an atlas without crash
		// In headless CI, the constructor will fail gracefully
		return false; // Conservative - always skip GL tests in unit tests
	}
};

TEST_F(TileTextureAtlasTest, ConstructorDefaultSize) {
	// This test verifies the atlas can be constructed.
	// Actual GL texture creation is tested in integration tests.
	TileTextureAtlas atlas(1024);
	EXPECT_EQ(atlas.size(), 1024);
	// texture() may be 0 in headless environments
}

TEST_F(TileTextureAtlasTest, AllocationLogic) {
	// Test the allocation algorithm without GL context
	// The allocate() function uses shelf-packing algorithm
	TileTextureAtlas atlas(256);

	// First allocation should succeed (top-left corner)
	AtlasRegion r1 = atlas.allocate(64, 64);
	EXPECT_TRUE(r1.valid);
	EXPECT_EQ(r1.x, 0);
	EXPECT_EQ(r1.y, 0);
	EXPECT_EQ(r1.width, 64);
	EXPECT_EQ(r1.height, 64);

	// Second allocation should be to the right
	AtlasRegion r2 = atlas.allocate(64, 64);
	EXPECT_TRUE(r2.valid);
	EXPECT_EQ(r2.x, 64);
	EXPECT_EQ(r2.y, 0);

	// Third allocation fills first row
	AtlasRegion r3 = atlas.allocate(64, 64);
	EXPECT_TRUE(r3.valid);
	EXPECT_EQ(r3.x, 128);
	EXPECT_EQ(r3.y, 0);

	// Fourth allocation fills first row
	AtlasRegion r4 = atlas.allocate(64, 64);
	EXPECT_TRUE(r4.valid);
	EXPECT_EQ(r4.x, 192);
	EXPECT_EQ(r4.y, 0);

	// Fifth allocation should start new row
	AtlasRegion r5 = atlas.allocate(64, 64);
	EXPECT_TRUE(r5.valid);
	EXPECT_EQ(r5.x, 0);
	EXPECT_EQ(r5.y, 64);
}

TEST_F(TileTextureAtlasTest, AllocationOverflow) {
	TileTextureAtlas atlas(128);

	// Fill the atlas (128x128 = 4 tiles of 64x64)
	AtlasRegion r1 = atlas.allocate(64, 64);
	AtlasRegion r2 = atlas.allocate(64, 64);
	AtlasRegion r3 = atlas.allocate(64, 64);
	AtlasRegion r4 = atlas.allocate(64, 64);

	EXPECT_TRUE(r1.valid);
	EXPECT_TRUE(r2.valid);
	EXPECT_TRUE(r3.valid);
	EXPECT_TRUE(r4.valid);

	// Fifth allocation should fail (no space)
	AtlasRegion r5 = atlas.allocate(64, 64);
	EXPECT_FALSE(r5.valid);
}

TEST_F(TileTextureAtlasTest, AllocationTooLarge) {
	TileTextureAtlas atlas(128);

	// Allocation larger than atlas should fail
	AtlasRegion r = atlas.allocate(256, 256);
	EXPECT_FALSE(r.valid);
}

TEST_F(TileTextureAtlasTest, VariableSizeAllocation) {
	TileTextureAtlas atlas(256);

	// First row: 128-wide item
	AtlasRegion r1 = atlas.allocate(128, 64);
	EXPECT_TRUE(r1.valid);
	EXPECT_EQ(r1.x, 0);
	EXPECT_EQ(r1.y, 0);
	EXPECT_EQ(r1.height, 64);

	// Second item in first row
	AtlasRegion r2 = atlas.allocate(64, 32);
	EXPECT_TRUE(r2.valid);
	EXPECT_EQ(r2.x, 128);
	EXPECT_EQ(r2.y, 0);

	// Row height is determined by tallest item (64)
	// Third item should go to second row since it won't fit
	AtlasRegion r3 = atlas.allocate(128, 128);
	EXPECT_TRUE(r3.valid);
	EXPECT_EQ(r3.x, 0);
	EXPECT_EQ(r3.y, 64); // Below first row (height 64)
}

TEST_F(TileTextureAtlasTest, MoveConstructor) {
	TileTextureAtlas atlas1(512);
	atlas1.allocate(64, 64);

	TileTextureAtlas atlas2(std::move(atlas1));
	EXPECT_EQ(atlas2.size(), 512);
	// Original should be in moved-from state
	EXPECT_EQ(atlas1.size(), 0);
}

TEST_F(TileTextureAtlasTest, MoveAssignment) {
	TileTextureAtlas atlas1(512);
	atlas1.allocate(64, 64);

	TileTextureAtlas atlas2(256);
	atlas2 = std::move(atlas1);

	EXPECT_EQ(atlas2.size(), 512);
	EXPECT_EQ(atlas1.size(), 0);
}

TEST_F(TileTextureAtlasTest, UploadInvalidRegion) {
	TileTextureAtlas atlas(256);

	// Create invalid region
	AtlasRegion invalid;
	invalid.valid = false;

	// Upload should return false for invalid region
	std::vector<uint8_t> data(64 * 64 * 4, 255);
	EXPECT_FALSE(atlas.upload(invalid, data.data()));
}

TEST_F(TileTextureAtlasTest, UploadNullData) {
	TileTextureAtlas atlas(256);
	AtlasRegion region = atlas.allocate(64, 64);
	EXPECT_TRUE(region.valid);

	// Upload should return false for null data
	EXPECT_FALSE(atlas.upload(region, nullptr));
}
