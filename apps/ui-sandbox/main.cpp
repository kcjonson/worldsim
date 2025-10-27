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

#include "demos/demo.h"
#include "primitives/primitives.h"
#include "primitives/batch_renderer.h"
#include "metrics/metrics_collector.h"
#include "debug/debug_server.h"
#include "graphics/color.h"
#include "graphics/rect.h"
#include "math/types.h"
#include "utils/log.h"
#include "utils/string_hash.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <string>
#include <cstring>

// GLFW callbacks
void ErrorCallback(int error, const char* description) {
	LOG_ERROR(UI, "GLFW Error (%d): %s", error, description);
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
	Renderer::Primitives::SetViewport(width, height);
}

// Initialize GLFW and create window
GLFWwindow* InitializeWindow() {
	// Set error callback
	glfwSetErrorCallback(ErrorCallback);

	// Initialize GLFW
	if (!glfwInit()) {
		LOG_ERROR(UI, "Failed to initialize GLFW");
		return nullptr;
	}

	// Get primary monitor to calculate window size (80% of screen)
	GLFWmonitor* primaryMonitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
	int windowWidth = static_cast<int>(videoMode->width * 0.8f);
	int windowHeight = static_cast<int>(videoMode->height * 0.8f);

	LOG_INFO(UI, "Screen: %dx%d", videoMode->width, videoMode->height);
	LOG_INFO(UI, "Window: %dx%d (80%% of screen)", windowWidth, windowHeight);

	// Configure GLFW
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on macOS

	// Create window
	GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "UI Sandbox", nullptr, nullptr);

	if (!window) {
		LOG_ERROR(UI, "Failed to create GLFW window");
		glfwTerminate();
		return nullptr;
	}

	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

	// Enable vsync
	glfwSwapInterval(1);
	LOG_DEBUG(UI, "VSync enabled");

	// Initialize GLEW
	glewExperimental = GL_TRUE; // Required for core profile
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		LOG_ERROR(UI, "Failed to initialize GLEW: %s", glewGetErrorString(err));
		glfwDestroyWindow(window);
		glfwTerminate();
		return nullptr;
	}

	// Print OpenGL version
	LOG_INFO(Renderer, "OpenGL Version: %s", glGetString(GL_VERSION));
	LOG_INFO(Renderer, "GLSL Version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

	return window;
}

int main(int argc, char* argv[]) {
	// Initialize logging system
	foundation::Logger::Initialize();

	LOG_INFO(UI, "UI Sandbox - Component Testing & Demo Environment");

	// Demonstrate string hashing system
	LOG_INFO(Foundation, "String Hashing System Demo:");

	// Compile-time hashing (happens at compile-time, zero runtime cost)
	constexpr foundation::StringHash kTransformHash = HASH("Transform");
	constexpr foundation::StringHash kPositionHash = foundation::hashes::kPosition;

	LOG_INFO(Foundation, "  Compile-time: 'Transform' -> 0x%llx", kTransformHash);
	LOG_INFO(Foundation, "  Compile-time: 'Position' -> 0x%llx", kPositionHash);

	// Runtime hashing (computed at runtime)
	const char* runtimeString = "DynamicComponent";
	foundation::StringHash runtimeHash = foundation::HashString(runtimeString);
	LOG_INFO(Foundation, "  Runtime: '%s' -> 0x%llx", runtimeString, runtimeHash);

#ifdef DEBUG
	// Debug collision detection (only in debug builds)
	foundation::HashStringDebug("Transform");
	foundation::HashStringDebug("Position");
	foundation::HashStringDebug("TestComponent");

	LOG_INFO(Foundation, "  Debug lookup: 0x%llx -> '%s'",
		kTransformHash, foundation::GetStringForHash(kTransformHash));
#endif

	// Parse command line arguments
	std::string demo = "primitives";
	int httpPort = 8081; // Default port for ui-sandbox

	for (int i = 1; i < argc; i++) {
		if (std::strcmp(argv[i], "--component") == 0 && i + 1 < argc) {
			demo = argv[++i];
		} else if (std::strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) {
			httpPort = std::stoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--help") == 0) {
			LOG_INFO(UI, "Usage: ui-sandbox [options]");
			LOG_INFO(UI, "Options:");
			LOG_INFO(UI, "  --component <name>   Show specific component demo");
			LOG_INFO(UI, "  --http-port <port>   Enable HTTP debug server on port");
			LOG_INFO(UI, "  --help               Show this help message");
			return 0;
		}
	}

	LOG_INFO(UI, "Demo: %s", demo.c_str());

	// Initialize window and OpenGL
	GLFWwindow* window = InitializeWindow();
	if (!window) {
		return 1;
	}

	// Get actual window size
	int windowWidth, windowHeight;
	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	// Initialize primitive rendering system
	LOG_INFO(Renderer, "Initializing primitive rendering system");
	Renderer::Primitives::Init(nullptr); // TODO: Pass renderer instance
	Renderer::Primitives::SetViewport(windowWidth, windowHeight);

	// Initialize demo
	LOG_INFO(UI, "Initializing demo");
	Demo::Init();

	// Initialize metrics collection
	Renderer::MetricsCollector metrics;

	// Start debug server
	Foundation::DebugServer debugServer;
	if (httpPort > 0) {
		debugServer.Start(httpPort);
		LOG_INFO(Foundation, "Debug server: http://localhost:%d", httpPort);

		// Connect logger to debug server for HTTP log streaming
		foundation::Logger::SetDebugServer(&debugServer);
		LOG_INFO(Foundation, "Logger connected to debug server");
	}

	// Main loop
	LOG_INFO(UI, "Entering main loop...");

	while (!glfwWindowShouldClose(window)) {
		// Begin frame timing
		metrics.BeginFrame();

		// Poll events
		glfwPollEvents();

		// Render frame
		Demo::Render();

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
	LOG_INFO(UI, "Shutting down...");

	if (httpPort > 0) {
		// Disconnect logger from debug server before stopping it
		foundation::Logger::SetDebugServer(nullptr);
		debugServer.Stop();
	}

	Demo::Shutdown();
	Renderer::Primitives::Shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();

	foundation::Logger::Shutdown();

	return 0;
}
