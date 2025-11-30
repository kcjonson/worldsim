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

#include "coordinate_system/coordinate_system.h"
#include "debug/debug_server.h"
#include "font/font_renderer.h"
#include "graphics/color.h"
#include "graphics/rect.h"
#include "math/types.h"
#include "metrics/metrics_collector.h"
#include "navigation_menu.h"
#include "primitives/primitives.h"
#include "utils/log.h"
#include "utils/string_hash.h"
#include <application/application.h>
#include <components/button/button.h>
#include <scene/scene_manager.h>
#include <shapes/shapes.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <string>

// Global navigation menu (only created when no --scene argument)
static std::optional<UI::NavigationMenu> g_navigationMenu;

// GLFW callbacks
// Global coordinate system (accessed by callbacks)
static Renderer::CoordinateSystem*		 g_coordinateSystem = nullptr;
static std::unique_ptr<ui::FontRenderer> g_fontRenderer = nullptr;

void ErrorCallback(int error, const char* description) {
	LOG_ERROR(UI, "GLFW Error (%d): %s", error, description);
}

void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
	// width and height are framebuffer dimensions (physical pixels)
	glViewport(0, 0, width, height);

	// Update coordinate system (for percentage helpers)
	int windowWidth = 0;
	int windowHeight = 0;
	glfwGetWindowSize(window, &windowWidth, &windowHeight);
	if (g_coordinateSystem != nullptr) {
		g_coordinateSystem->updateWindowSize(windowWidth, windowHeight);
	}

	// Update primitives viewport with framebuffer size (for 1:1 pixel mapping)
	Renderer::Primitives::setViewport(width, height);

	// Update navigation menu positions if it exists
	if (g_navigationMenu) {
		g_navigationMenu->onWindowResize();
	}
}

// Initialize GLFW and create window
GLFWwindow* InitializeWindow() {
	// Set error callback
	glfwSetErrorCallback(ErrorCallback);

	// Initialize GLFW
	if (glfwInit() == 0) {
		LOG_ERROR(UI, "Failed to initialize GLFW");
		return nullptr;
	}

	// Get primary monitor to calculate window size (80% of screen)
	GLFWmonitor*	   primaryMonitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
	int				   windowWidth = static_cast<int>(static_cast<float>(videoMode->width) * 0.8F);
	int				   windowHeight = static_cast<int>(static_cast<float>(videoMode->height) * 0.8F);

	LOG_INFO(UI, "Screen: %dx%d", videoMode->width, videoMode->height);
	LOG_INFO(UI, "Window: %dx%d (80%% of screen)", windowWidth, windowHeight);

	// Configure GLFW
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on macOS

	// Create window
	GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "UI Sandbox", nullptr, nullptr);

	if (window == nullptr) {
		LOG_ERROR(UI, "Failed to create GLFW window");
		glfwTerminate();
		return nullptr;
	}

	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);

	// NOTE: Mouse input is handled by InputManager for menu buttons
	// No need for separate GLFW callbacks

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
	std::span<char*> args(argv, static_cast<size_t>(argc));
	int				 httpPort = 8081; // Default port for ui-sandbox
	bool			 hasSceneArg = false;

	for (size_t i = 1; i < args.size(); i++) {
		if (std::strncmp(args[i], "--scene=", 8) == 0) {
			hasSceneArg = true;
		} else if (std::strcmp(args[i], "--http-port") == 0 && i + 1 < args.size()) {
			++i;
			httpPort = std::stoi(args[i]);
		} else if (std::strcmp(args[i], "--help") == 0) {
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
	foundation::Logger::initialize();

	// Start debug server IMMEDIATELY (before any logs)
	// This ensures ALL logs go to the ring buffer
	Foundation::DebugServer debugServer;
	foundation::Logger::setDebugServer(&debugServer);
	if (httpPort > 0) {
		debugServer.start(httpPort);
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
	const char*			   runtimeString = "DynamicComponent";
	foundation::StringHash runtimeHash = foundation::hashString(runtimeString);
	LOG_INFO(Foundation, "  Runtime: '%s' -> 0x%llx", runtimeString, runtimeHash);

#ifdef DEBUG
	// Debug collision detection (only in debug builds)
	foundation::hashStringDebug("Transform");
	foundation::hashStringDebug("Position");
	foundation::hashStringDebug("TestComponent");

	LOG_INFO(Foundation, "  Debug lookup: 0x%llx -> '%s'", kTransformHash, foundation::GetStringForHash(kTransformHash));
#endif

	// Initialize window and OpenGL
	GLFWwindow* window = InitializeWindow();
	if (window == nullptr) {
		return 1;
	}

	// Initialize coordinate system (for percentage helpers and future DPI-aware UI)
	LOG_INFO(Renderer, "Initializing coordinate system");
	Renderer::CoordinateSystem coordinateSystem;
	g_coordinateSystem = &coordinateSystem;
	if (!coordinateSystem.Initialize(window)) {
		LOG_ERROR(Renderer, "Failed to initialize coordinate system");
		return 1;
	}

	// Get window size (logical pixels) for UI coordinate space
	// This ensures UI elements appear at the correct perceived size on high-DPI displays
	int windowWidth = 0;
	int windowHeight = 0;
	glfwGetWindowSize(window, &windowWidth, &windowHeight);
	LOG_DEBUG(Renderer, "Window size (logical pixels): %dx%d", windowWidth, windowHeight);

	// Get framebuffer size (physical pixels) for OpenGL viewport
	int framebufferWidth = 0;
	int framebufferHeight = 0;
	glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
	LOG_DEBUG(Renderer, "Framebuffer size (physical pixels): %dx%d", framebufferWidth, framebufferHeight);

	// Log pixel ratio for information
	float pixelRatio = coordinateSystem.getPixelRatio();
	LOG_DEBUG(Renderer, "Pixel ratio: %.2f (framebuffer is %dx window)", pixelRatio, static_cast<int>(pixelRatio));

	// Initialize primitive rendering system
	LOG_INFO(Renderer, "Initializing primitive rendering system");
	Renderer::Primitives::init(nullptr); // TODO: Pass renderer instance
	// NOTE: BatchRenderer uses the CoordinateSystem for DPI-aware projection (logical pixels)
	// and for percentage-based layout helpers
	Renderer::Primitives::setCoordinateSystem(&coordinateSystem);
	// Use framebuffer size for GL viewport (high-res rendering)
	// but coordinate system projection uses logical pixels (window size)
	Renderer::Primitives::setViewport(framebufferWidth, framebufferHeight);

	LOG_DEBUG(Renderer, "Primitive rendering system initialized");

	// Initialize font renderer for navigation menu
	LOG_INFO(UI, "Initializing font renderer for navigation menu");
	g_fontRenderer = std::make_unique<ui::FontRenderer>();
	if (!g_fontRenderer->Initialize()) {
		LOG_ERROR(UI, "Failed to initialize FontRenderer for menu!");
		// Continue anyway - menu will just not have text labels
	} else {
		// CRITICAL: Use CoordinateSystem projection for text rendering to match shapes
		// CoordinateSystem uses LOGICAL pixels (window size), not physical pixels (framebuffer size)
		// This ensures x=20,y=20 means the same thing for text and shapes
		glm::mat4 projection = coordinateSystem.CreateScreenSpaceProjection();
		g_fontRenderer->setProjectionMatrix(projection);

		// Set font renderer in Primitives API for Text shapes (provides font metrics)
		Renderer::Primitives::setFontRenderer(g_fontRenderer.get());

		// Configure the unified BatchRenderer with the font atlas for text rendering
		// The uber shader handles both SDF shapes and MSDF text in a single draw call
		Renderer::Primitives::setFontAtlas(g_fontRenderer->getAtlasTexture(), 4.0F);

		// Register frame update callback for LRU cache tracking
		Renderer::Primitives::setFrameUpdateCallback([]() {
			if (g_fontRenderer) {
				g_fontRenderer->updateFrame();
			}
		});

		LOG_INFO(UI, "Font renderer initialized with unified uber shader");
	}

	// Initialize metrics collection
	Renderer::MetricsCollector metrics;

	// Create application BEFORE scene loading (FocusManager, InputManager, etc. need to exist)
	LOG_INFO(UI, "Creating application");
	engine::Application app(window);

	// Initialize scene system
	LOG_INFO(Engine, "Initializing scene system");

	// Try to load scene from command-line args
	if (!engine::SceneManager::Get().setInitialSceneFromArgs(argc, argv)) {
		// No --scene arg provided, load default scene
		LOG_INFO(Engine, "No scene specified, loading default: shapes");
		engine::SceneManager::Get().switchTo("shapes");
	}

	// Setup navigation menu (only when no --scene argument for zero perf impact on scene tests)
	if (!hasSceneArg) {
		auto sceneNames = engine::SceneManager::Get().getAllSceneNames();
		g_navigationMenu.emplace(
			UI::NavigationMenu::Args{
				.sceneNames = sceneNames,
				.onSceneSelected =
					[](const std::string& sceneName) {
						engine::SceneManager::Get().switchTo(sceneName);
						LOG_INFO(UI, "Switched to scene: %s", sceneName.c_str());
					},
				.coordinateSystem = g_coordinateSystem
			}
		);
		LOG_INFO(UI, "Navigation menu enabled (%zu scenes available)", sceneNames.size());
	}

	// Set up pre-frame callback (primitives begin frame + debug server control)
	app.setPreFrameCallback([&debugServer, &app, &metrics, httpPort]() -> bool {
		// Begin frame timing
		metrics.beginFrame();

		// Begin frame for all rendering (scene + UI)
		Renderer::Primitives::beginFrame();

		// Debug server control (if enabled)
		if (httpPort > 0) {
			Foundation::ControlAction action = debugServer.getControlAction();
			if (action != Foundation::ControlAction::None) {
				switch (action) {
					case Foundation::ControlAction::Exit:
						LOG_INFO(UI, "Exit requested via control endpoint");
						app.stop();
						debugServer.clearControlAction();
						return false; // Request exit

					case Foundation::ControlAction::SceneChange: {
						std::string sceneName = debugServer.getTargetSceneName();
						LOG_INFO(UI, "Scene change requested: %s", sceneName.c_str());
						if (engine::SceneManager::Get().switchTo(sceneName)) {
							LOG_INFO(UI, "Switched to scene: %s", sceneName.c_str());
						} else {
							LOG_ERROR(UI, "Failed to switch to scene: %s", sceneName.c_str());
						}
						debugServer.clearControlAction();
						break;
					}

					case Foundation::ControlAction::Pause:
						LOG_INFO(UI, "Pause requested via control endpoint");
						app.pause();
						debugServer.clearControlAction();
						break;

					case Foundation::ControlAction::Resume:
						LOG_INFO(UI, "Resume requested via control endpoint");
						app.resume();
						debugServer.clearControlAction();
						break;

					case Foundation::ControlAction::ReloadScene: {
						LOG_INFO(UI, "Reload scene requested via control endpoint");
						std::string currentScene = engine::SceneManager::Get().getCurrentSceneName();
						if (!currentScene.empty()) {
							if (engine::SceneManager::Get().switchTo(currentScene)) {
								LOG_INFO(UI, "Reloaded scene: %s", currentScene.c_str());
							} else {
								LOG_ERROR(UI, "Failed to reload scene: %s", currentScene.c_str());
							}
						}
						debugServer.clearControlAction();
						break;
					}

					default:
						break;
				}
			}
		}
		return true; // Continue running
	});

	// Set up overlay renderer (navigation menu)
	app.setOverlayRenderer([]() {
		// Handle navigation menu if it exists (zero overhead when not created)
		if (g_navigationMenu) {
			g_navigationMenu->handleInput();
			g_navigationMenu->update(0.0F);
			g_navigationMenu->render();
		}

		// End frame - flush all queued primitives
		Renderer::Primitives::endFrame();
	});

	// Set up post-frame callback (metrics + screenshot)
	app.setPostFrameCallback([&metrics, &debugServer, httpPort]() {
		// Get rendering stats
		auto renderStats = Renderer::Primitives::getStats();
		metrics.setRenderStats(renderStats.drawCalls, renderStats.vertexCount, renderStats.triangleCount);

		// End frame timing
		metrics.endFrame();

		// Update debug server with latest metrics
		if (httpPort > 0) {
			debugServer.updateMetrics(metrics.getCurrentMetrics());

			// Check if screenshot was requested and capture if so
			// This must happen BEFORE glfwSwapBuffers() to capture the current frame
			debugServer.captureScreenshotIfRequested();
		}
	});

	// Run application
	LOG_INFO(UI, "Starting application main loop");
	app.run();

	// Cleanup
	LOG_INFO(UI, "Shutting down...");

	// Shutdown scene system first - scene components (Button, TextInput) need FocusManager
	// SceneManager is a static singleton that outlives Application, so we must explicitly
	// destroy scenes while FocusManager is still valid
	engine::SceneManager::Get().shutdown();

	// Destroy navigation menu - its Button components also need FocusManager
	g_navigationMenu.reset();

	if (httpPort > 0) {
		// Disconnect logger from debug server before stopping it
		foundation::Logger::setDebugServer(nullptr);

		// Signal that cleanup is complete - this unblocks the exit HTTP handler
		// which will stop the server and send the response before we continue
		debugServer.signalShutdownComplete();

		// Stop the server - waits for exit handler to complete if HTTP exit was triggered,
		// then actually stops the server. Safe to call regardless of how shutdown was initiated.
		debugServer.stop();
	}

	// Scene cleanup is handled above via SceneManager::shutdown()

	// Cleanup font renderer
	Renderer::Primitives::setFontRenderer(nullptr);
	g_fontRenderer.reset();

	Renderer::Primitives::shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();

	foundation::Logger::shutdown();

	return 0;
}
