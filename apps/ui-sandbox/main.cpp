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

#include <scene/scene_manager.h>
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

// Global state for menu interaction
static struct {
	bool showMenu = false;
	std::vector<std::string> sceneNames;
	int selectedIndex = 0;
	double mouseX = 0;
	double mouseY = 0;
} g_menuState;

// GLFW callbacks
void ErrorCallback(int error, const char* description) {
	LOG_ERROR(UI, "GLFW Error (%d): %s", error, description);
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
	Renderer::Primitives::SetViewport(width, height);
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if (!g_menuState.showMenu) return;
	if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return;

	// Menu bounds
	const float menuX = 10;
	const float menuY = 10;
	const float menuWidth = 150;
	const float lineHeight = 25;
	const float headerHeight = 30;

	// Check if click is within menu bounds
	float clickX = static_cast<float>(g_menuState.mouseX);
	float clickY = static_cast<float>(g_menuState.mouseY);

	if (clickX >= menuX && clickX <= menuX + menuWidth &&
		clickY >= menuY && clickY <= menuY + headerHeight + g_menuState.sceneNames.size() * lineHeight) {

		// Check which scene was clicked
		if (clickY >= menuY + headerHeight) {
			int clickedIndex = static_cast<int>((clickY - menuY - headerHeight) / lineHeight);
			if (clickedIndex >= 0 && clickedIndex < static_cast<int>(g_menuState.sceneNames.size())) {
				// Switch to selected scene
				engine::SceneManager::Get().SwitchTo(g_menuState.sceneNames[clickedIndex]);
				g_menuState.selectedIndex = clickedIndex;
			}
		}
	}
}

void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
	g_menuState.mouseX = xpos;
	g_menuState.mouseY = ypos;
}

// Render navigation menu
void RenderNavigationMenu() {
	if (!g_menuState.showMenu) return;

	using namespace Foundation;

	const float menuX = 10;
	const float menuY = 10;
	const float menuWidth = 150;
	const float lineHeight = 25;
	const float headerHeight = 30;
	const float totalHeight = headerHeight + g_menuState.sceneNames.size() * lineHeight;

	// Draw menu background
	Renderer::Primitives::DrawRect({
		.bounds = {menuX, menuY, menuWidth, totalHeight},
		.style = {
			.fill = Color(0.15f, 0.15f, 0.2f, 0.95f),
			.border = BorderStyle{.color = Color(0.4f, 0.4f, 0.5f, 1.0f), .width = 1.0f}
		},
		.id = "menu_background"
	});

	// Draw header background
	Renderer::Primitives::DrawRect({
		.bounds = {menuX, menuY, menuWidth, headerHeight},
		.style = {.fill = Color(0.2f, 0.2f, 0.3f, 1.0f)},
		.id = "menu_header"
	});

	// Draw scene items
	for (size_t i = 0; i < g_menuState.sceneNames.size(); i++) {
		float itemY = menuY + headerHeight + i * lineHeight;

		// Highlight selected scene
		if (static_cast<int>(i) == g_menuState.selectedIndex) {
			Renderer::Primitives::DrawRect({
				.bounds = {menuX + 2, itemY + 2, menuWidth - 4, lineHeight - 4},
				.style = {.fill = Color(0.3f, 0.4f, 0.6f, 0.8f)},
				.id = ("menu_item_" + std::to_string(i)).c_str()
			});
		}

		// Highlight hovered scene
		float mouseX = static_cast<float>(g_menuState.mouseX);
		float mouseY = static_cast<float>(g_menuState.mouseY);
		if (mouseX >= menuX && mouseX <= menuX + menuWidth &&
			mouseY >= itemY && mouseY <= itemY + lineHeight) {
			Renderer::Primitives::DrawRect({
				.bounds = {menuX + 2, itemY + 2, menuWidth - 4, lineHeight - 4},
				.style = {.fill = Color(0.4f, 0.5f, 0.7f, 0.5f)},
				.id = ("menu_hover_" + std::to_string(i)).c_str()
			});
		}
	}
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
	glfwSetMouseButtonCallback(window, MouseButtonCallback);
	glfwSetCursorPosCallback(window, CursorPosCallback);

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
	// Parse command line arguments FIRST (before any logging)
	int httpPort = 8081; // Default port for ui-sandbox
	bool hasSceneArg = false;

	for (int i = 1; i < argc; i++) {
		if (std::strncmp(argv[i], "--scene=", 8) == 0) {
			hasSceneArg = true;
		} else if (std::strcmp(argv[i], "--http-port") == 0 && i + 1 < argc) {
			httpPort = std::stoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--help") == 0) {
			// Can't log yet, just print to stdout
			printf("Usage: ui-sandbox [options]\n");
			printf("Options:\n");
			printf("  --scene=<name>       Load specific scene (shapes, arena, handles)\n");
			printf("  --http-port <port>   Enable HTTP debug server on port\n");
			printf("  --help               Show this help message\n");
			return 0;
		}
	}

	// Initialize logging system
	foundation::Logger::Initialize();

	// Start debug server IMMEDIATELY (before any logs)
	// This ensures ALL logs go to the ring buffer
	Foundation::DebugServer debugServer;
	foundation::Logger::SetDebugServer(&debugServer);
	if (httpPort > 0) {
		debugServer.Start(httpPort);
		LOG_INFO(Foundation, "Debug server: http://localhost:%d", httpPort);
		LOG_INFO(Foundation, "Logger connected to debug server");
		LOG_DEBUG(Foundation, "Debug server connection test - this DEBUG log should appear in browser");
	}

	// NOW all subsequent logs go to the ring buffer
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
	LOG_DEBUG(Renderer, "Viewport size: %dx%d", windowWidth, windowHeight);
	Renderer::Primitives::Init(nullptr); // TODO: Pass renderer instance
	Renderer::Primitives::SetViewport(windowWidth, windowHeight);
	LOG_DEBUG(Renderer, "Primitive rendering system initialized");

	// Initialize scene system
	LOG_INFO(Engine, "Initializing scene system");

	// Try to load scene from command-line args
	if (!engine::SceneManager::Get().SetInitialSceneFromArgs(argc, argv)) {
		// No --scene arg provided, load default scene
		LOG_INFO(Engine, "No scene specified, loading default: shapes");
		engine::SceneManager::Get().SwitchTo("shapes");
	}

	// Setup navigation menu
	g_menuState.showMenu = !hasSceneArg;

	if (g_menuState.showMenu) {
		g_menuState.sceneNames = engine::SceneManager::Get().GetAllSceneNames();
		// Find current scene index
		auto* currentScene = engine::SceneManager::Get().GetCurrentScene();
		if (currentScene) {
			const char* currentName = currentScene->GetName();
			for (size_t i = 0; i < g_menuState.sceneNames.size(); i++) {
				if (g_menuState.sceneNames[i] == currentName) {
					g_menuState.selectedIndex = static_cast<int>(i);
					break;
				}
			}
		}
		LOG_INFO(UI, "Navigation menu enabled (%zu scenes available)", g_menuState.sceneNames.size());
	}

	// Initialize metrics collection
	Renderer::MetricsCollector metrics;

	// Main loop
	LOG_INFO(UI, "Entering main loop...");
	LOG_DEBUG(UI, "Main loop started - rendering at 60 FPS (vsync)");

	int frameCount = 0;
	double lastTime = glfwGetTime();

	while (!glfwWindowShouldClose(window)) {
		// Calculate delta time
		double currentTime = glfwGetTime();
		float dt = static_cast<float>(currentTime - lastTime);
		lastTime = currentTime;

		// Begin frame timing
		metrics.BeginFrame();

		// Log every 60 frames (once per second at 60 FPS)
		if (frameCount++ % 60 == 0) {
			//LOG_DEBUG(UI, "Frame %d - main loop running", frameCount);
		}

		// Poll events
		glfwPollEvents();

		// Update and render current scene
		engine::SceneManager::Get().Update(dt);
		engine::SceneManager::Get().Render();

		// Render navigation menu on top (if enabled)
		RenderNavigationMenu();

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

	// Scene manager will automatically call OnExit on current scene
	// when it goes out of scope (destructor)

	Renderer::Primitives::Shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();

	foundation::Logger::Shutdown();

	return 0;
}
