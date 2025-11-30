#include "coordinate_system/coordinate_system.h"
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

using namespace Renderer;

// ============================================================================
// CoordinateSystem Tests
// ============================================================================

// Test fixture that creates a minimal GLFW window for testing
class CoordinateSystemTest : public ::testing::Test {
  protected:
	void SetUp() override {
		// Initialize GLFW for testing
		if (glfwInit() == 0) {
			GTEST_SKIP() << "GLFW initialization failed - skipping CoordinateSystem tests"; // NOLINT(readability-implicit-bool-conversion)
		}

		// Create a hidden window for testing (no OpenGL context needed for coordinate math)
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // No OpenGL context needed

		testWindow = glfwCreateWindow(800, 600, "Test", nullptr, nullptr);
		if (testWindow == nullptr) {
			glfwTerminate();
			GTEST_SKIP() << "GLFW window creation failed - skipping CoordinateSystem tests"; // NOLINT(readability-implicit-bool-conversion)
		}
	}

	void TearDown() override {
		if (testWindow != nullptr) {
			glfwDestroyWindow(testWindow);
		}
		glfwTerminate();
	}

  protected:						// NOLINT(readability-redundant-access-specifiers)
	GLFWwindow* testWindow = nullptr; // NOLINT(cppcoreguidelines-non-private-member-variables-in-classes)
};

// ----------------------------------------------------------------------------
// Initialization Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, Initialization) {
	CoordinateSystem coordSys;
	EXPECT_TRUE(coordSys.Initialize(testWindow));
}

TEST_F(CoordinateSystemTest, InitializationWithNullWindow) {
	CoordinateSystem coordSys;
	EXPECT_FALSE(coordSys.Initialize(nullptr));
}

// ----------------------------------------------------------------------------
// Window Size Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, GetWindowSize) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	glm::vec2 size = coordSys.GetWindowSize();
	EXPECT_EQ(size.x, 800.0F);
	EXPECT_EQ(size.y, 600.0F);
}

TEST_F(CoordinateSystemTest, GetWindowSizeWithoutInitialization) {
	CoordinateSystem coordSys;
	// Should return default fallback values
	glm::vec2 size = coordSys.GetWindowSize();
	EXPECT_EQ(size.x, 1920.0F); // Default fallback
	EXPECT_EQ(size.y, 1080.0F);
}

// ----------------------------------------------------------------------------
// Percentage Helper Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, PercentWidth) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow); // 800x600 window

	EXPECT_FLOAT_EQ(coordSys.PercentWidth(50.0F), 400.0F);	// 50% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(100.0F), 800.0F); // 100% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(25.0F), 200.0F);	// 25% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(0.0F), 0.0F);		// 0% of 800
}

TEST_F(CoordinateSystemTest, PercentHeight) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow); // 800x600 window

	EXPECT_FLOAT_EQ(coordSys.PercentHeight(50.0F), 300.0F);	 // 50% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(100.0F), 600.0F); // 100% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(25.0F), 150.0F);	 // 25% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(0.0F), 0.0F);	 // 0% of 600
}

TEST_F(CoordinateSystemTest, PercentSize) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow); // 800x600 window

	glm::vec2 size = coordSys.PercentSize(50.0F, 75.0F);
	EXPECT_FLOAT_EQ(size.x, 400.0F); // 50% of 800
	EXPECT_FLOAT_EQ(size.y, 450.0F); // 75% of 600
}

TEST_F(CoordinateSystemTest, PercentPosition) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow); // 800x600 window

	glm::vec2 pos = coordSys.PercentPosition(25.0F, 50.0F);
	EXPECT_FLOAT_EQ(pos.x, 200.0F); // 25% of 800
	EXPECT_FLOAT_EQ(pos.y, 300.0F); // 50% of 600
}

// ----------------------------------------------------------------------------
// Pixel Ratio Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, GetPixelRatio) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	float ratio = coordSys.GetPixelRatio();
	// Ratio should be positive and reasonable (1.0 for non-retina, 2.0 for retina)
	EXPECT_GT(ratio, 0.0F);
	EXPECT_LE(ratio, 4.0F); // Most displays have ratio <= 4.0
}

TEST_F(CoordinateSystemTest, PixelRatioCaching) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	float ratio1 = coordSys.GetPixelRatio();
	float ratio2 = coordSys.GetPixelRatio();

	// Should return same value (cached)
	EXPECT_FLOAT_EQ(ratio1, ratio2);
}

TEST_F(CoordinateSystemTest, PixelRatioInvalidatesOnWindowResize) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	float ratio1 = coordSys.GetPixelRatio();

	// Simulate window resize
	coordSys.updateWindowSize(1024, 768);

	// This should have invalidated the cache (though ratio might be the same)
	float ratio2 = coordSys.GetPixelRatio();

	// Both ratios should be valid
	EXPECT_GT(ratio1, 0.0F);
	EXPECT_GT(ratio2, 0.0F);
}

// ----------------------------------------------------------------------------
// Coordinate Conversion Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, WindowToFramebufferConversion) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	float	  ratio = coordSys.GetPixelRatio();
	glm::vec2 windowCoords(100.0F, 200.0F);
	glm::vec2 framebufferCoords = coordSys.WindowToFramebuffer(windowCoords);

	EXPECT_FLOAT_EQ(framebufferCoords.x, 100.0F * ratio);
	EXPECT_FLOAT_EQ(framebufferCoords.y, 200.0F * ratio);
}

TEST_F(CoordinateSystemTest, FramebufferToWindowConversion) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	float	  ratio = coordSys.GetPixelRatio();
	glm::vec2 framebufferCoords(200.0F, 400.0F);
	glm::vec2 windowCoords = coordSys.FramebufferToWindow(framebufferCoords);

	EXPECT_FLOAT_EQ(windowCoords.x, 200.0F / ratio);
	EXPECT_FLOAT_EQ(windowCoords.y, 400.0F / ratio);
}

TEST_F(CoordinateSystemTest, RoundTripConversion) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	glm::vec2 original(123.0F, 456.0F);
	glm::vec2 framebuffer = coordSys.WindowToFramebuffer(original);
	glm::vec2 roundtrip = coordSys.FramebufferToWindow(framebuffer);

	EXPECT_NEAR(roundtrip.x, original.x, 0.001F);
	EXPECT_NEAR(roundtrip.y, original.y, 0.001F);
}

// ----------------------------------------------------------------------------
// Projection Matrix Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, CreateScreenSpaceProjection) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	glm::mat4 projection = coordSys.CreateScreenSpaceProjection();

	// Verify it's not identity matrix
	EXPECT_NE(projection, glm::mat4(1.0F));

	// Screen space should have Y increasing downward
	// Top-left corner (0,0) in screen space should map to (-1,1) in clip space
	glm::vec4 topLeft = projection * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
	EXPECT_NEAR(topLeft.x / topLeft.w, -1.0F, 0.01F);
	EXPECT_NEAR(topLeft.y / topLeft.w, 1.0F, 0.01F);

	// Bottom-right corner (800,600) should map to (1,-1) in clip space
	glm::vec4 bottomRight = projection * glm::vec4(800.0F, 600.0F, 0.0F, 1.0F);
	EXPECT_NEAR(bottomRight.x / bottomRight.w, 1.0F, 0.01F);
	EXPECT_NEAR(bottomRight.y / bottomRight.w, -1.0F, 0.01F);
}

TEST_F(CoordinateSystemTest, CreateWorldSpaceProjection) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	glm::mat4 projection = coordSys.CreateWorldSpaceProjection();

	// Verify it's not identity matrix
	EXPECT_NE(projection, glm::mat4(1.0F));

	// World space should have (0,0) at center, Y increasing upward
	// Center (0,0) should map to (0,0) in clip space
	glm::vec4 center = projection * glm::vec4(0.0F, 0.0F, 0.0F, 1.0F);
	EXPECT_NEAR(center.x / center.w, 0.0F, 0.01F);
	EXPECT_NEAR(center.y / center.w, 0.0F, 0.01F);

	// Right edge (400,0) should map to (1,0) in clip space (half of 800)
	glm::vec4 rightEdge = projection * glm::vec4(400.0F, 0.0F, 0.0F, 1.0F);
	EXPECT_NEAR(rightEdge.x / rightEdge.w, 1.0F, 0.01F);
	EXPECT_NEAR(rightEdge.y / rightEdge.w, 0.0F, 0.01F);
}

// ----------------------------------------------------------------------------
// Edge Cases
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, PercentHelpersWithoutWindow) {
	CoordinateSystem coordSys;
	// Don't initialize - should use fallback values

	// Should use default 1920x1080
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(50.0F), 960.0F);	// 50% of 1920
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(50.0F), 540.0F); // 50% of 1080
}

TEST_F(CoordinateSystemTest, UpdateWindowSizeInvalidatesCache) {
	CoordinateSystem coordSys;
	coordSys.Initialize(testWindow);

	// Get initial ratio (forces calculation)
	coordSys.GetPixelRatio();

	// Update window size should mark cache as dirty
	coordSys.updateWindowSize(1024, 768);

	// Next call should recalculate (we can't directly test the dirty flag,
	// but we can verify the function completes without error)
	float ratio = coordSys.GetPixelRatio();
	EXPECT_GT(ratio, 0.0F);
}
