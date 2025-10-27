// UI Sandbox - Component Testing & Demo Environment
//
// This application is used to develop and test UI components in isolation
// before integrating them into the main game.
//
// Features:
// - Window creation with OpenGL context
// - Primitive rendering API testing
// - RmlUI integration testing (future)
// - HTTP debug server for UI inspection (future)

#include "primitives/primitives.h"
#include "primitives/batch_renderer.h"
#include "metrics/metrics_collector.h"
#include "debug/debug_server.h"
#include "graphics/color.h"
#include "graphics/rect.h"
#include "math/types.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <string>
#include <cstring>

// Window dimensions
static constexpr int kWindowWidth = 800;
static constexpr int kWindowHeight = 600;

// GLFW callbacks
void ErrorCallback(int error, const char* description) {
	std::cerr << "GLFW Error (" << error << "): " << description << std::endl;
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
}

// Initialize GLFW and create window
GLFWwindow* InitializeWindow() {
	// Set error callback
	glfwSetErrorCallback(ErrorCallback);

	// Initialize GLFW
	if (!glfwInit()) {
		std::cerr << "Failed to initialize GLFW" << std::endl;
		return nullptr;
	}

	// Configure GLFW
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on macOS

	// Create window
	GLFWwindow* window = glfwCreateWindow(kWindowWidth, kWindowHeight, "UI Sandbox", nullptr, nullptr);

	if (!window) {
		std::cerr << "Failed to create GLFW window" << std::endl;
		glfwTerminate();
		return nullptr;
	}

	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

	// Enable vsync
	glfwSwapInterval(1);

	// Initialize GLEW
	glewExperimental = GL_TRUE; // Required for core profile
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		std::cerr << "Failed to initialize GLEW: " << glewGetErrorString(err) << std::endl;
		glfwDestroyWindow(window);
		glfwTerminate();
		return nullptr;
	}

	// Print OpenGL version
	std::cout << "OpenGL Version: " << glGetString(GL_VERSION) << std::endl;
	std::cout << "GLSL Version: " << glGetString(GL_SHADING_LANGUAGE_VERSION) << std::endl;

	return window;
}

// Render test content using Primitive API
void RenderTestScene() {
	using namespace Foundation;

	// Clear background
	glClearColor(0.1f, 0.1f, 0.15f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	// Begin primitive rendering
	Renderer::Primitives::BeginFrame();

	// Draw test rectangles
	Renderer::Primitives::DrawRect(Rect(50, 50, 200, 100), Color::Red());
	Renderer::Primitives::DrawRect(Rect(300, 50, 200, 100), Color::Green());
	Renderer::Primitives::DrawRect(Rect(550, 50, 200, 100), Color::Blue());

	// Draw some bordered rectangles
	Renderer::Primitives::DrawRectBorder(Rect(50, 200, 200, 100), Color::Yellow(), 3.0f);
	Renderer::Primitives::DrawRectBorder(Rect(300, 200, 200, 100), Color::Cyan(), 3.0f);

	// Draw a grid of small rectangles (batching test)
	for (int y = 0; y < 10; y++) {
		for (int x = 0; x < 10; x++) {
			float hue = (x * 10 + y) / 100.0f;
			Color color(hue, 1.0f - hue, 0.5f, 1.0f);
			Renderer::Primitives::DrawRect(Rect(50 + x * 25, 350 + y * 20, 20, 15), color);
		}
	}

	// End primitive rendering (flushes batches)
	Renderer::Primitives::EndFrame();
}

int main(int argc, char* argv[]) {
	std::cout << "UI Sandbox - Component Testing & Demo Environment" << std::endl;

	// Parse command line arguments
	std::string demo = "primitives";
	int httpPort = 8081; // Default port for ui-sandbox

	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--component") == 0 && i + 1 < argc) {
			demo = argv[++i];
		} else if (std::strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) {
			httpPort = std::stoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--help") == 0) {
			std::cout << "Usage: ui-sandbox [options]" << std::endl;
			std::cout << "Options:" << std::endl;
			std::cout << "  --component <name>   Show specific component demo" << std::endl;
			std::cout << "  --http-port <port>   Enable HTTP debug server on port" << std::endl;
			std::cout << "  --help               Show this help message" << std::endl;
			return 0;
		}
	}

	std::cout << "Demo: " << demo << std::endl;

	// Initialize window and OpenGL
	GLFWwindow* window = InitializeWindow();
	if (!window) {
		return 1;
	}

	// Initialize primitive rendering system
	Renderer::Primitives::Init(nullptr); // TODO: Pass renderer instance

	// Initialize metrics collection
	Renderer::MetricsCollector metrics;

	// Start debug server
	Foundation::DebugServer debugServer;
	if (httpPort > 0) {
		debugServer.Start(httpPort);
		std::cout << "Debug server: http://localhost:" << httpPort << std::endl;
	}

	// Main loop
	std::cout << "Entering main loop..." << std::endl;

	while (!glfwWindowShouldClose(window)) {
		// Begin frame timing
		metrics.BeginFrame();

		// Poll events
		glfwPollEvents();

		// Render frame
		RenderTestScene();

		// Get rendering stats
		auto renderStats = Renderer::Primitives::GetStats();
		metrics.SetRenderStats(renderStats.drawCalls, renderStats.vertexCount, renderStats.triangleCount);

		// End frame timing
		metrics.EndFrame();

		// Update debug server with latest metrics
		if (httpPort > 0) {
			debugServer.UpdateMetrics(metrics.GetCurrentMetrics());
		}

		// Swap buffers
		glfwSwapBuffers(window);
	}

	// Cleanup
	if (httpPort > 0) {
		debugServer.Stop();
	}

	Renderer::Primitives::Shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();

	std::cout << "Shutdown complete" << std::endl;
	return 0;
}
