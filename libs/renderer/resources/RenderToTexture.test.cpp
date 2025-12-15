#include "resources/RenderToTexture.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <gtest/gtest.h>

#include <vector>

// Basic integration test: clear into an FBO and verify texture contents.
TEST(RenderToTextureTest, ClearsToColor) {
	ASSERT_EQ(glfwInit(), GLFW_TRUE);
	glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
	GLFWwindow* window = glfwCreateWindow(32, 32, "offscreen", nullptr, nullptr);
	ASSERT_NE(window, nullptr);
	glfwMakeContextCurrent(window);
	ASSERT_EQ(glewInit(), GLEW_OK);

	Renderer::RenderToTexture rtt(4, 4);
	std::vector<uint8_t> pixels(static_cast<size_t>(4 * 4 * 4), 0);

	rtt.begin();
	glClearColor(1.0F, 0.0F, 0.0F, 1.0F);
	glClear(GL_COLOR_BUFFER_BIT);
	rtt.end();

	glBindTexture(GL_TEXTURE_2D, rtt.texture());
	glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
	glBindTexture(GL_TEXTURE_2D, 0);

	for (size_t i = 0; i < pixels.size(); i += 4) {
		EXPECT_EQ(pixels[i + 0], 255); // R
		EXPECT_EQ(pixels[i + 1], 0);   // G
		EXPECT_EQ(pixels[i + 2], 0);   // B
		EXPECT_EQ(pixels[i + 3], 255); // A
	}

	glfwDestroyWindow(window);
	glfwTerminate();
}
