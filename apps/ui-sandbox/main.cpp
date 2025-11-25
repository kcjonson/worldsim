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
#include "font/text_batch_renderer.h"
#include "graphics/color.h"
#include "graphics/rect.h"
#include "math/types.h"
#include "metrics/metrics_collector.h"
#include "primitives/batch_renderer.h"
#include "primitives/primitives.h"
#include "utils/log.h"
#include "utils/string_hash.h"
#include <application/application.h>
#include <scene/scene_manager.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cstring>
#include <memory>
#include <span>
#include <string>

// Global state for menu interaction
static struct {
	bool					 showMenu = false;
	std::vector<std::string> sceneNames;
	size_t					 selectedIndex = 0;
	double					 mouseX = 0;
	double					 mouseY = 0;
} g_menuState;

// GLFW callbacks
// Global coordinate system (accessed by callbacks)
static Renderer::CoordinateSystem*			  g_coordinateSystem = nullptr;
static std::unique_ptr<ui::FontRenderer>	  g_fontRenderer = nullptr;
static std::unique_ptr<ui::TextBatchRenderer> g_textBatchRenderer = nullptr;

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
		g_coordinateSystem->UpdateWindowSize(windowWidth, windowHeight);
	}

	// Update primitives viewport with framebuffer size (for 1:1 pixel mapping)
	Renderer::Primitives::SetViewport(width, height);
}

void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
	if (!g_menuState.showMenu) {
		return;
	}
	if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) {
		return;
	}

	// Menu bounds
	const float kMenuX = 10;
	const float kMenuY = 10;
	const float kMenuWidth = 150;
	const float kLineHeight = 25;
	const float kHeaderHeight = 30;

	// Check if click is within menu bounds
	auto clickX = static_cast<float>(g_menuState.mouseX);
	auto clickY = static_cast<float>(g_menuState.mouseY);

	if (clickX >= kMenuX && clickX <= kMenuX + kMenuWidth && clickY >= kMenuY &&
		clickY <= kMenuY + kHeaderHeight + (static_cast<float>(g_menuState.sceneNames.size()) * kLineHeight)) {

		// Check which scene was clicked
		if (clickY >= kMenuY + kHeaderHeight) {
			int clickedIndex = static_cast<int>((clickY - kMenuY - kHeaderHeight) / kLineHeight);
			if (clickedIndex >= 0 && clickedIndex < static_cast<int>(g_menuState.sceneNames.size())) {
				// Switch to selected scene
				engine::SceneManager::Get().SwitchTo(g_menuState.sceneNames[clickedIndex]);
				g_menuState.selectedIndex = static_cast<size_t>(clickedIndex);
			}
		}
	}
}

void CursorPosCallback(GLFWwindow* window, double xpos, double ypos) {
	// Mouse coordinates from GLFW are in window space (logical pixels)
	// Rendering now uses logical pixels (via CoordinateSystem)
	// Therefore, no scaling needed - coordinates match directly
	g_menuState.mouseX = xpos;
	g_menuState.mouseY = ypos;
}

// Render navigation menu
void RenderNavigationMenu() {
	if (!g_menuState.showMenu) {
		return;
	}

	using namespace Foundation;

	const float kMenuX = 10;
	const float kMenuY = 10;
	const float kMenuWidth = 150;
	const float kLineHeight = 25;
	const float kHeaderHeight = 30;
	const float kTotalHeight = kHeaderHeight + (static_cast<float>(g_menuState.sceneNames.size()) * kLineHeight);

	// Draw menu background
	Renderer::Primitives::DrawRect(
		{.bounds = {kMenuX, kMenuY, kMenuWidth, kTotalHeight},
		 .style = {.fill = Color(0.15F, 0.15F, 0.2F, 0.95F), .border = BorderStyle{.color = Color(0.4F, 0.4F, 0.5F, 1.0F), .width = 1.0F}},
		 .id = "menu_background"}
	);

	// Draw header background
	Renderer::Primitives::DrawRect(
		{.bounds = {kMenuX, kMenuY, kMenuWidth, kHeaderHeight}, .style = {.fill = Color(0.2F, 0.2F, 0.3F, 1.0F)}, .id = "menu_header"}
	);

	// Draw scene item rectangles (highlights)
	for (size_t i = 0; i < g_menuState.sceneNames.size(); i++) {
		float itemY = kMenuY + kHeaderHeight + (static_cast<float>(i) * kLineHeight);

		// Highlight selected scene
		if (i == g_menuState.selectedIndex) {
			Renderer::Primitives::DrawRect(
				{.bounds = {kMenuX + 2, itemY + 2, kMenuWidth - 4, kLineHeight - 4},
				 .style = {.fill = Color(0.3F, 0.4F, 0.6F, 0.8F)},
				 .id = ("menu_item_" + std::to_string(i)).c_str()}
			);
		}

		// Highlight hovered scene
		auto mouseX = static_cast<float>(g_menuState.mouseX);
		auto mouseY = static_cast<float>(g_menuState.mouseY);
		if (mouseX >= kMenuX && mouseX <= kMenuX + kMenuWidth && mouseY >= itemY && mouseY <= itemY + kLineHeight) {
			Renderer::Primitives::DrawRect(
				{.bounds = {kMenuX + 2, itemY + 2, kMenuWidth - 4, kLineHeight - 4},
				 .style = {.fill = Color(0.4F, 0.5F, 0.7F, 0.5F)},
				 .id = ("menu_hover_" + std::to_string(i)).c_str()}
			);
		}
	}

	// Flush batched rectangles before rendering text
	Renderer::Primitives::EndFrame();
	Renderer::Primitives::BeginFrame();

	// Draw header title
	if (g_fontRenderer) {
		glm::vec3 headerColor(0.9F, 0.9F, 0.9F);
		g_fontRenderer->RenderText("Scenes", glm::vec2(kMenuX + 10, kMenuY + 8), 1.0F, headerColor);
	}

	// Draw scene names
	for (size_t i = 0; i < g_menuState.sceneNames.size(); i++) {
		float itemY = kMenuY + kHeaderHeight + (static_cast<float>(i) * kLineHeight);

		if (g_fontRenderer) {
			glm::vec3 textColor = (i == g_menuState.selectedIndex) ? glm::vec3(1.0F, 1.0F, 1.0F) : glm::vec3(0.8F, 0.8F, 0.8F);
			g_fontRenderer->RenderText(g_menuState.sceneNames[i], glm::vec2(kMenuX + 10, itemY + 5), 0.8F, textColor);
		}
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
	const char*			   runtimeString = "DynamicComponent";
	foundation::StringHash runtimeHash = foundation::HashString(runtimeString);
	LOG_INFO(Foundation, "  Runtime: '%s' -> 0x%llx", runtimeString, runtimeHash);

#ifdef DEBUG
	// Debug collision detection (only in debug builds)
	foundation::HashStringDebug("Transform");
	foundation::HashStringDebug("Position");
	foundation::HashStringDebug("TestComponent");

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
	float pixelRatio = coordinateSystem.GetPixelRatio();
	LOG_DEBUG(Renderer, "Pixel ratio: %.2f (framebuffer is %dx window)", pixelRatio, static_cast<int>(pixelRatio));

	// Initialize primitive rendering system
	LOG_INFO(Renderer, "Initializing primitive rendering system");
	Renderer::Primitives::Init(nullptr); // TODO: Pass renderer instance
	// NOTE: BatchRenderer uses the CoordinateSystem for DPI-aware projection (logical pixels)
	// and for percentage-based layout helpers
	Renderer::Primitives::SetCoordinateSystem(&coordinateSystem);
	// Use framebuffer size for GL viewport (high-res rendering)
	// but coordinate system projection uses logical pixels (window size)
	Renderer::Primitives::SetViewport(framebufferWidth, framebufferHeight);

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
		g_fontRenderer->SetProjectionMatrix(projection);
		// Set font renderer in Primitives API for Text shapes
		Renderer::Primitives::SetFontRenderer(g_fontRenderer.get());

		// Initialize text batch renderer for batched SDF text rendering
		g_textBatchRenderer = std::make_unique<ui::TextBatchRenderer>();
		g_textBatchRenderer->Initialize(g_fontRenderer.get());
		g_textBatchRenderer->SetProjectionMatrix(projection);
		Renderer::Primitives::SetTextBatchRenderer(g_textBatchRenderer.get());

		// Register flush callback so Primitives::EndFrame() automatically flushes text batches
		Renderer::Primitives::SetTextFlushCallback([]() {
			if (g_textBatchRenderer) {
				g_textBatchRenderer->Flush();
			}
		});

		// Register frame update callback for LRU cache tracking
		Renderer::Primitives::SetFrameUpdateCallback([]() {
			if (g_fontRenderer) {
				g_fontRenderer->UpdateFrame();
			}
		});

		LOG_INFO(UI, "Font renderer and text batch renderer initialized successfully");
	}

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
		if (currentScene != nullptr) {
			const char* currentName = currentScene->GetName();
			for (size_t i = 0; i < g_menuState.sceneNames.size(); i++) {
				if (g_menuState.sceneNames[i] == currentName) {
					g_menuState.selectedIndex = i;
					break;
				}
			}
		}
		LOG_INFO(UI, "Navigation menu enabled (%zu scenes available)", g_menuState.sceneNames.size());
	}

	// Initialize metrics collection
	Renderer::MetricsCollector metrics;

	// Create application and set up game loop
	LOG_INFO(UI, "Creating application");
	engine::Application app(window);

	// Set up pre-frame callback (primitives begin frame + debug server control)
	app.SetPreFrameCallback([&debugServer, &app, &metrics, httpPort]() -> bool {
		// Begin frame timing
		metrics.BeginFrame();

		// Begin frame for all rendering (scene + UI)
		Renderer::Primitives::BeginFrame();

		// Debug server control (if enabled)
		if (httpPort > 0) {
			Foundation::ControlAction action = debugServer.GetControlAction();
			if (action != Foundation::ControlAction::None) {
				switch (action) {
					case Foundation::ControlAction::Exit:
						LOG_INFO(UI, "Exit requested via control endpoint");
						app.Stop();
						debugServer.ClearControlAction();
						return false; // Request exit

					case Foundation::ControlAction::SceneChange: {
						std::string sceneName = debugServer.GetTargetSceneName();
						LOG_INFO(UI, "Scene change requested: %s", sceneName.c_str());
						if (engine::SceneManager::Get().SwitchTo(sceneName)) {
							LOG_INFO(UI, "Switched to scene: %s", sceneName.c_str());
						} else {
							LOG_ERROR(UI, "Failed to switch to scene: %s", sceneName.c_str());
						}
						debugServer.ClearControlAction();
						break;
					}

					case Foundation::ControlAction::Pause:
						LOG_INFO(UI, "Pause requested via control endpoint");
						app.Pause();
						debugServer.ClearControlAction();
						break;

					case Foundation::ControlAction::Resume:
						LOG_INFO(UI, "Resume requested via control endpoint");
						app.Resume();
						debugServer.ClearControlAction();
						break;

					case Foundation::ControlAction::ReloadScene: {
						LOG_INFO(UI, "Reload scene requested via control endpoint");
						std::string currentScene = engine::SceneManager::Get().GetCurrentSceneName();
						if (!currentScene.empty()) {
							if (engine::SceneManager::Get().SwitchTo(currentScene)) {
								LOG_INFO(UI, "Reloaded scene: %s", currentScene.c_str());
							} else {
								LOG_ERROR(UI, "Failed to reload scene: %s", currentScene.c_str());
							}
						}
						debugServer.ClearControlAction();
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
	app.SetOverlayRenderer([]() {
		// Render navigation menu on top
		RenderNavigationMenu();

		// End frame - flush all queued primitives
		Renderer::Primitives::EndFrame();
	});

	// Set up post-frame callback (metrics + screenshot)
	app.SetPostFrameCallback([&metrics, &debugServer, httpPort]() {
		// Get rendering stats
		auto renderStats = Renderer::Primitives::GetStats();
		metrics.SetRenderStats(renderStats.drawCalls, renderStats.vertexCount, renderStats.triangleCount);

		// End frame timing
		metrics.EndFrame();

		// Update debug server with latest metrics
		if (httpPort > 0) {
			debugServer.UpdateMetrics(metrics.GetCurrentMetrics());

			// Check if screenshot was requested and capture if so
			// This must happen BEFORE glfwSwapBuffers() to capture the current frame
			debugServer.CaptureScreenshotIfRequested();
		}
	});

	// Run application
	LOG_INFO(UI, "Starting application main loop");
	app.Run();

	// Cleanup
	LOG_INFO(UI, "Shutting down...");

	if (httpPort > 0) {
		// Disconnect logger from debug server before stopping it
		foundation::Logger::SetDebugServer(nullptr);
		debugServer.Stop();
	}

	// Scene manager will automatically call OnExit on current scene
	// when it goes out of scope (destructor)

	// Cleanup text batch renderer and font renderer
	Renderer::Primitives::SetTextFlushCallback(nullptr);
	Renderer::Primitives::SetTextBatchRenderer(nullptr);
	g_textBatchRenderer.reset();

	Renderer::Primitives::SetFontRenderer(nullptr);
	g_fontRenderer.reset();

	Renderer::Primitives::Shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();

	foundation::Logger::Shutdown();

	return 0;
}
