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
		if (!glfwInit()) {
			GTEST_SKIP() << "GLFW initialization failed - skipping CoordinateSystem tests";
		}

		// Create a hidden window for testing (no OpenGL context needed for coordinate math)
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // No OpenGL context needed

		m_window = glfwCreateWindow(800, 600, "Test", nullptr, nullptr);
		if (!m_window) {
			glfwTerminate();
			GTEST_SKIP() << "GLFW window creation failed - skipping CoordinateSystem tests";
		}
	}

	void TearDown() override {
		if (m_window) {
			glfwDestroyWindow(m_window);
		}
		glfwTerminate();
	}

	GLFWwindow* m_window = nullptr;
};

// ----------------------------------------------------------------------------
// Initialization Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, Initialization) {
	CoordinateSystem coordSys;
	EXPECT_TRUE(coordSys.Initialize(m_window));
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
	coordSys.Initialize(m_window);

	glm::vec2 size = coordSys.GetWindowSize();
	EXPECT_EQ(size.x, 800.0f);
	EXPECT_EQ(size.y, 600.0f);
}

TEST_F(CoordinateSystemTest, GetWindowSizeWithoutInitialization) {
	CoordinateSystem coordSys;
	// Should return default fallback values
	glm::vec2 size = coordSys.GetWindowSize();
	EXPECT_EQ(size.x, 1920.0f); // Default fallback
	EXPECT_EQ(size.y, 1080.0f);
}

// ----------------------------------------------------------------------------
// Percentage Helper Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, PercentWidth) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window); // 800x600 window

	EXPECT_FLOAT_EQ(coordSys.PercentWidth(50.0f), 400.0f);	// 50% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(100.0f), 800.0f); // 100% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(25.0f), 200.0f);	// 25% of 800
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(0.0f), 0.0f);		// 0% of 800
}

TEST_F(CoordinateSystemTest, PercentHeight) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window); // 800x600 window

	EXPECT_FLOAT_EQ(coordSys.PercentHeight(50.0f), 300.0f);	 // 50% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(100.0f), 600.0f); // 100% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(25.0f), 150.0f);	 // 25% of 600
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(0.0f), 0.0f);	 // 0% of 600
}

TEST_F(CoordinateSystemTest, PercentSize) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window); // 800x600 window

	glm::vec2 size = coordSys.PercentSize(50.0f, 75.0f);
	EXPECT_FLOAT_EQ(size.x, 400.0f); // 50% of 800
	EXPECT_FLOAT_EQ(size.y, 450.0f); // 75% of 600
}

TEST_F(CoordinateSystemTest, PercentPosition) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window); // 800x600 window

	glm::vec2 pos = coordSys.PercentPosition(25.0f, 50.0f);
	EXPECT_FLOAT_EQ(pos.x, 200.0f); // 25% of 800
	EXPECT_FLOAT_EQ(pos.y, 300.0f); // 50% of 600
}

// ----------------------------------------------------------------------------
// Pixel Ratio Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, GetPixelRatio) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	float ratio = coordSys.GetPixelRatio();
	// Ratio should be positive and reasonable (1.0 for non-retina, 2.0 for retina)
	EXPECT_GT(ratio, 0.0f);
	EXPECT_LE(ratio, 4.0f); // Most displays have ratio <= 4.0
}

TEST_F(CoordinateSystemTest, PixelRatioCaching) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	float ratio1 = coordSys.GetPixelRatio();
	float ratio2 = coordSys.GetPixelRatio();

	// Should return same value (cached)
	EXPECT_FLOAT_EQ(ratio1, ratio2);
}

TEST_F(CoordinateSystemTest, PixelRatioInvalidatesOnWindowResize) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	float ratio1 = coordSys.GetPixelRatio();

	// Simulate window resize
	coordSys.UpdateWindowSize(1024, 768);

	// This should have invalidated the cache (though ratio might be the same)
	float ratio2 = coordSys.GetPixelRatio();

	// Both ratios should be valid
	EXPECT_GT(ratio1, 0.0f);
	EXPECT_GT(ratio2, 0.0f);
}

// ----------------------------------------------------------------------------
// Coordinate Conversion Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, WindowToFramebufferConversion) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	float	  ratio = coordSys.GetPixelRatio();
	glm::vec2 windowCoords(100.0f, 200.0f);
	glm::vec2 framebufferCoords = coordSys.WindowToFramebuffer(windowCoords);

	EXPECT_FLOAT_EQ(framebufferCoords.x, 100.0f * ratio);
	EXPECT_FLOAT_EQ(framebufferCoords.y, 200.0f * ratio);
}

TEST_F(CoordinateSystemTest, FramebufferToWindowConversion) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	float	  ratio = coordSys.GetPixelRatio();
	glm::vec2 framebufferCoords(200.0f, 400.0f);
	glm::vec2 windowCoords = coordSys.FramebufferToWindow(framebufferCoords);

	EXPECT_FLOAT_EQ(windowCoords.x, 200.0f / ratio);
	EXPECT_FLOAT_EQ(windowCoords.y, 400.0f / ratio);
}

TEST_F(CoordinateSystemTest, RoundTripConversion) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	glm::vec2 original(123.0f, 456.0f);
	glm::vec2 framebuffer = coordSys.WindowToFramebuffer(original);
	glm::vec2 roundtrip = coordSys.FramebufferToWindow(framebuffer);

	EXPECT_NEAR(roundtrip.x, original.x, 0.001f);
	EXPECT_NEAR(roundtrip.y, original.y, 0.001f);
}

// ----------------------------------------------------------------------------
// Projection Matrix Tests
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, CreateScreenSpaceProjection) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	glm::mat4 projection = coordSys.CreateScreenSpaceProjection();

	// Verify it's not identity matrix
	EXPECT_NE(projection, glm::mat4(1.0f));

	// Screen space should have Y increasing downward
	// Top-left corner (0,0) in screen space should map to (-1,1) in clip space
	glm::vec4 topLeft = projection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	EXPECT_NEAR(topLeft.x / topLeft.w, -1.0f, 0.01f);
	EXPECT_NEAR(topLeft.y / topLeft.w, 1.0f, 0.01f);

	// Bottom-right corner (800,600) should map to (1,-1) in clip space
	glm::vec4 bottomRight = projection * glm::vec4(800.0f, 600.0f, 0.0f, 1.0f);
	EXPECT_NEAR(bottomRight.x / bottomRight.w, 1.0f, 0.01f);
	EXPECT_NEAR(bottomRight.y / bottomRight.w, -1.0f, 0.01f);
}

TEST_F(CoordinateSystemTest, CreateWorldSpaceProjection) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	glm::mat4 projection = coordSys.CreateWorldSpaceProjection();

	// Verify it's not identity matrix
	EXPECT_NE(projection, glm::mat4(1.0f));

	// World space should have (0,0) at center, Y increasing upward
	// Center (0,0) should map to (0,0) in clip space
	glm::vec4 center = projection * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
	EXPECT_NEAR(center.x / center.w, 0.0f, 0.01f);
	EXPECT_NEAR(center.y / center.w, 0.0f, 0.01f);

	// Right edge (400,0) should map to (1,0) in clip space (half of 800)
	glm::vec4 rightEdge = projection * glm::vec4(400.0f, 0.0f, 0.0f, 1.0f);
	EXPECT_NEAR(rightEdge.x / rightEdge.w, 1.0f, 0.01f);
	EXPECT_NEAR(rightEdge.y / rightEdge.w, 0.0f, 0.01f);
}

// ----------------------------------------------------------------------------
// Edge Cases
// ----------------------------------------------------------------------------

TEST_F(CoordinateSystemTest, PercentHelpersWithoutWindow) {
	CoordinateSystem coordSys;
	// Don't initialize - should use fallback values

	// Should use default 1920x1080
	EXPECT_FLOAT_EQ(coordSys.PercentWidth(50.0f), 960.0f);	// 50% of 1920
	EXPECT_FLOAT_EQ(coordSys.PercentHeight(50.0f), 540.0f); // 50% of 1080
}

TEST_F(CoordinateSystemTest, UpdateWindowSizeInvalidatesCache) {
	CoordinateSystem coordSys;
	coordSys.Initialize(m_window);

	// Get initial ratio (forces calculation)
	coordSys.GetPixelRatio();

	// Update window size should mark cache as dirty
	coordSys.UpdateWindowSize(1024, 768);

	// Next call should recalculate (we can't directly test the dirty flag,
	// but we can verify the function completes without error)
	float ratio = coordSys.GetPixelRatio();
	EXPECT_GT(ratio, 0.0f);
}
