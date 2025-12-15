#include "resources/TilePatternBaker.h"
#include <gtest/gtest.h>

using namespace Renderer;

// ============================================================================
// TilePatternBaker Tests
// ============================================================================

TEST(TilePatternBakerTest, InvalidDimensions) {
	std::vector<uint8_t> pixels;

	// Zero width should fail
	EXPECT_FALSE(bakeSvgToRgba("nonexistent.svg", 0, 64, pixels));
	EXPECT_TRUE(pixels.empty());

	// Zero height should fail
	EXPECT_FALSE(bakeSvgToRgba("nonexistent.svg", 64, 0, pixels));
	EXPECT_TRUE(pixels.empty());

	// Negative dimensions should fail
	EXPECT_FALSE(bakeSvgToRgba("nonexistent.svg", -1, 64, pixels));
	EXPECT_FALSE(bakeSvgToRgba("nonexistent.svg", 64, -1, pixels));
}

TEST(TilePatternBakerTest, NonexistentFile) {
	std::vector<uint8_t> pixels;

	// Non-existent file should fail
	EXPECT_FALSE(bakeSvgToRgba("/path/to/nonexistent.svg", 64, 64, pixels));
	EXPECT_TRUE(pixels.empty());
}

TEST(TilePatternBakerTest, EmptyFilePath) {
	std::vector<uint8_t> pixels;

	// Empty file path should fail
	EXPECT_FALSE(bakeSvgToRgba("", 64, 64, pixels));
	EXPECT_TRUE(pixels.empty());
}

TEST(TilePatternBakerTest, OutputPixelsClearedOnFailure) {
	std::vector<uint8_t> pixels;
	pixels.resize(100, 0xFF); // Pre-fill with data

	// Failed parse should clear the output buffer
	EXPECT_FALSE(bakeSvgToRgba("nonexistent.svg", 64, 64, pixels));
	EXPECT_TRUE(pixels.empty());
}

// Note: Tests for successful SVG parsing require valid SVG files
// Those are better suited for integration tests with actual assets
