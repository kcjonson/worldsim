// AppLauncher.cpp - Shared application bootstrap implementation
// Eliminates duplication between ui-sandbox and world-sim Main.cpp

#include "AppLauncher.h"
#include "Application.h"

#include <CoordinateSystem/CoordinateSystem.h>
#include <assets/AssetRegistry.h>
#include <debug/DebugServer.h>
#include <font/FontRenderer.h>
#include <metrics/MetricsCollector.h>
#include <primitives/Primitives.h>
#include <resources/TilePatternBaker.h>
#include <resources/TileTextureAtlas.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>
#include <utils/ResourcePath.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "world/chunk/Chunk.h"

#include <algorithm>
#include <cstring>
#include <glm/vec4.hpp>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

namespace engine {

	namespace {

		// Global systems (accessed by GLFW callbacks)
		Renderer::CoordinateSystem*					g_coordinateSystem = nullptr;
		std::unique_ptr<ui::FontRenderer>			g_fontRenderer = nullptr;
		std::unique_ptr<Renderer::TileTextureAtlas> g_tileAtlas = nullptr;
		std::function<void()>						g_windowResizeCallback; // App-specific resize handler

		// Owned resources that persist between initialize() and run()
		std::unique_ptr<Foundation::DebugServer>	g_debugServer;
		std::unique_ptr<Renderer::MetricsCollector> g_metrics;
		std::unique_ptr<Application>				g_app;

		std::vector<glm::vec4> buildDefaultTileAtlas() {
			constexpr int kPatternSize = 512;
			const int	  surfaceCount = static_cast<int>(engine::world::Surface::Mud) + 1; // enum is sequential

			g_tileAtlas = std::make_unique<Renderer::TileTextureAtlas>(2048);
			std::vector<glm::vec4> rects;
			rects.reserve(surfaceCount);

			for (int i = 0; i < surfaceCount; ++i) {
				auto region = g_tileAtlas->allocate(kPatternSize, kPatternSize);
				if (!region.valid) {
					LOG_WARNING(Renderer, "Tile atlas ran out of space at surface %d", i);
					break;
				}

				std::vector<uint8_t> pixels;
				bool				 baked = false;

				// Attempt to bake SVG if present (named by Surface string)
				std::string surfaceName = engine::world::surfaceToString(static_cast<engine::world::Surface>(i));
				std::string svgPath = Foundation::findResourceString("assets/tiles/surfaces/" + surfaceName + "/pattern.svg");
				if (!svgPath.empty()) {
					baked = Renderer::bakeSvgToRgba(svgPath, kPatternSize, kPatternSize, pixels);
				}

				if (!baked) {
					// Fallback checker if SVG missing or bake failed
					pixels.assign(static_cast<size_t>(region.width * region.height * 4), 255);
					Foundation::Color c = engine::world::Chunk::getSurfaceColor(static_cast<engine::world::Surface>(i));
					uint8_t			  r = static_cast<uint8_t>(std::clamp(c.r, 0.0F, 1.0F) * 255.0F);
					uint8_t			  g = static_cast<uint8_t>(std::clamp(c.g, 0.0F, 1.0F) * 255.0F);
					uint8_t			  b = static_cast<uint8_t>(std::clamp(c.b, 0.0F, 1.0F) * 255.0F);
					uint8_t			  a = static_cast<uint8_t>(std::clamp(c.a, 0.0F, 1.0F) * 255.0F);
					for (int y = 0; y < region.height; ++y) {
						for (int x = 0; x < region.width; ++x) {
							size_t idx = static_cast<size_t>((y * region.width + x) * 4);
							bool   checker = ((x / 8) + (y / 8)) % 2 == 0;
							float  shade = checker ? 1.05F : 0.85F;
							pixels[idx + 0] = static_cast<uint8_t>(std::clamp(r * shade, 0.0F, 255.0F));
							pixels[idx + 1] = static_cast<uint8_t>(std::clamp(g * shade, 0.0F, 255.0F));
							pixels[idx + 2] = static_cast<uint8_t>(std::clamp(b * shade, 0.0F, 255.0F));
							pixels[idx + 3] = a;
						}
					}
				}

				g_tileAtlas->upload(region, pixels.data());

				float invSize = 1.0F / static_cast<float>(g_tileAtlas->size());
				float u0 = static_cast<float>(region.x) * invSize;
				float v0 = static_cast<float>(region.y) * invSize;
				float u1 = static_cast<float>(region.x + region.width) * invSize;
				float v1 = static_cast<float>(region.y + region.height) * invSize;
				rects.emplace_back(u0, v0, u1, v1);
			}

			return rects;
		}

		void errorCallback(int error, const char* description) {
			LOG_ERROR(Engine, "GLFW Error (%d): %s", error, description);
		}

		void framebufferSizeCallback(GLFWwindow* window, int width, int height) {
			glViewport(0, 0, width, height);

			int windowWidth = 0;
			int windowHeight = 0;
			glfwGetWindowSize(window, &windowWidth, &windowHeight);
			if (g_coordinateSystem != nullptr) {
				g_coordinateSystem->updateWindowSize(windowWidth, windowHeight);
			}

			Renderer::Primitives::setViewport(width, height);

			// Call app-specific resize handler if provided
			if (g_windowResizeCallback) {
				g_windowResizeCallback();
			}
		}

		GLFWwindow* initializeWindow(const char* title, float sizePercent) {
			glfwSetErrorCallback(errorCallback);

			if (glfwInit() == 0) {
				LOG_ERROR(Engine, "Failed to initialize GLFW");
				return nullptr;
			}

			GLFWmonitor*	   primaryMonitor = glfwGetPrimaryMonitor();
			const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
			int				   windowWidth = static_cast<int>(static_cast<float>(videoMode->width) * sizePercent);
			int				   windowHeight = static_cast<int>(static_cast<float>(videoMode->height) * sizePercent);

			LOG_INFO(Engine, "Screen: %dx%d", videoMode->width, videoMode->height);
			LOG_INFO(Engine, "Window: %dx%d (%.0f%% of screen)", windowWidth, windowHeight, sizePercent * 100.0F);

			glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
			glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
			glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
			glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);

			GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, title, nullptr, nullptr);
			if (window == nullptr) {
				LOG_ERROR(Engine, "Failed to create GLFW window");
				glfwTerminate();
				return nullptr;
			}

			glfwMakeContextCurrent(window);
			glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
			glfwSwapInterval(1);

			glewExperimental = GL_TRUE;
			GLenum err = glewInit();
			if (err != GLEW_OK) {
				LOG_ERROR(Engine, "Failed to initialize GLEW: %s", glewGetErrorString(err));
				glfwDestroyWindow(window);
				glfwTerminate();
				return nullptr;
			}

			LOG_INFO(Renderer, "OpenGL Version: %s", glGetString(GL_VERSION));
			LOG_INFO(Renderer, "GLSL Version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

			return window;
		}

		bool initializeRenderingSystems(GLFWwindow* window) {
			// Initialize coordinate system
			LOG_INFO(Renderer, "Initializing coordinate system");
			static Renderer::CoordinateSystem coordinateSystem;
			g_coordinateSystem = &coordinateSystem;
			if (!coordinateSystem.Initialize(window)) {
				LOG_ERROR(Renderer, "Failed to initialize coordinate system");
				return false;
			}

			// Get window and framebuffer sizes
			int windowWidth = 0;
			int windowHeight = 0;
			glfwGetWindowSize(window, &windowWidth, &windowHeight);

			int framebufferWidth = 0;
			int framebufferHeight = 0;
			glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);

			// Initialize primitive rendering
			LOG_INFO(Renderer, "Initializing primitive rendering system");
			Renderer::Primitives::init(nullptr);
			Renderer::Primitives::setCoordinateSystem(&coordinateSystem);
			Renderer::Primitives::setViewport(framebufferWidth, framebufferHeight);

			// Initialize font renderer
			LOG_INFO(UI, "Initializing font renderer");
			g_fontRenderer = std::make_unique<ui::FontRenderer>();
			if (!g_fontRenderer->Initialize()) {
				LOG_ERROR(UI, "Failed to initialize FontRenderer!");
			} else {
				Renderer::Primitives::setFontRenderer(g_fontRenderer.get());
				Renderer::Primitives::setFontAtlas(g_fontRenderer->getAtlasTexture(), 4.0F);
				Renderer::Primitives::setFrameUpdateCallback([]() {
					if (g_fontRenderer) {
						g_fontRenderer->updateFrame();
					}
				});
				LOG_INFO(UI, "Font renderer initialized");
			}

			// Bootstrap a default tile atlas (solid colors per surface) to exercise tile mode.
			{
				auto rects = buildDefaultTileAtlas();
				if (!rects.empty() && g_tileAtlas != nullptr) {
					Renderer::Primitives::setTileAtlas(g_tileAtlas->texture(), rects);
					LOG_INFO(Renderer, "Initialized default tile atlas with %zu entries", rects.size());
				} else {
					LOG_WARNING(Renderer, "Tile atlas not initialized (no rects)");
				}
			}

			return true;
		}

		void initializeAssetSystem(const std::string& assetsRootPath) {
			LOG_INFO(Engine, "Initializing asset system from %s", assetsRootPath.c_str());

			// Find the assets root folder
			std::string fullPath = Foundation::findResourceString(assetsRootPath);
			if (fullPath.empty()) {
				LOG_WARNING(Engine, "Assets root not found: %s", assetsRootPath.c_str());
				return;
			}

			// Set shared scripts path (for @shared/ prefix resolution)
			std::string sharedPath = Foundation::findResourceString("assets/shared/scripts");
			if (!sharedPath.empty()) {
				engine::assets::AssetRegistry::Get().setSharedScriptsPath(sharedPath);
				LOG_INFO(Engine, "Set shared scripts path: %s", sharedPath.c_str());
			}

			// Load all asset definitions from the root folder
			size_t loaded = engine::assets::AssetRegistry::Get().loadDefinitionsFromFolder(fullPath);
			LOG_INFO(Engine, "Loaded %zu asset definitions from %s", loaded, fullPath.c_str());
		}

		void cleanup(GLFWwindow* window) {
			LOG_INFO(Engine, "Shutting down...");

			engine::SceneManager::Get().shutdown();

			// Clear app-specific callback
			g_windowResizeCallback = nullptr;

			// Clear global resources
			g_app.reset();
			g_debugServer.reset();
			g_metrics.reset();

			Renderer::Primitives::setFontRenderer(nullptr);
			g_fontRenderer.reset();
			g_coordinateSystem = nullptr;

			Renderer::Primitives::shutdown();
			glfwDestroyWindow(window);
			glfwTerminate();
			foundation::Logger::shutdown();
		}

	} // anonymous namespace

	AppContext AppLauncher::initialize(int argc, char* argv[], const AppConfig& config) {
		std::span<char*> args(argv, static_cast<size_t>(argc));

		// Parse command line arguments
		int			httpPort = config.enableDebugServer ? config.debugServerPort : 0;
		std::string sceneArg;
		bool		hasSceneArg = false;

		for (size_t i = 1; i < args.size(); i++) {
			if (std::strncmp(args[i], "--scene=", 8) == 0) {
				sceneArg = args[i] + 8;
				hasSceneArg = true;
			} else if (std::strcmp(args[i], "--http-port") == 0 && i + 1 < args.size()) {
				++i;
				try {
					httpPort = std::stoi(args[i]);
				} catch (const std::exception& e) {
					LOG_ERROR(Engine, "Invalid port number '%s': %s", args[i], e.what());
					return {};
				}
			} else if (std::strcmp(args[i], "--help") == 0) {
				printf("Usage: %s [options]\n", config.windowTitle);
				printf("Options:\n");
				printf("  --scene=<name>       Load specific scene\n");
				printf("  --http-port <port>   Enable HTTP debug server on port\n");
				printf("  --help               Show this help message\n");
				return {}; // Return invalid context
			}
		}

		// Initialize logging
		foundation::Logger::initialize();

		// Start debug server if enabled
		if (httpPort > 0) {
			g_debugServer = std::make_unique<Foundation::DebugServer>();
			foundation::Logger::setDebugServer(g_debugServer.get());
			g_debugServer->start(httpPort);
			LOG_INFO(Foundation, "Debug server: http://localhost:%d", httpPort);
		}

		if (config.enableMetrics) {
			g_metrics = std::make_unique<Renderer::MetricsCollector>();
		}

		LOG_INFO(Engine, "%s", config.windowTitle);

		// Initialize window
		GLFWwindow* window = initializeWindow(config.windowTitle, config.windowSizePercent);
		if (window == nullptr) {
			foundation::Logger::shutdown();
			return {};
		}

		// Initialize rendering systems
		if (!initializeRenderingSystems(window)) {
			glfwDestroyWindow(window);
			glfwTerminate();
			foundation::Logger::shutdown();
			return {};
		}

		// Initialize asset system
		initializeAssetSystem(config.assetsRootPath);

		// Create application
		LOG_INFO(Engine, "Creating application");
		g_app = std::make_unique<Application>(window);

		// Initialize scene system
		LOG_INFO(Engine, "Initializing scene system");
		if (config.initializeScenes) {
			config.initializeScenes();
		}

		// Load initial scene
		std::size_t initialSceneKey = SIZE_MAX;
		if (hasSceneArg) {
			// Apply scene name remapping if configured
			std::string effectiveSceneName = sceneArg;
			if (config.remapSceneName) {
				std::string remapped = config.remapSceneName(sceneArg);
				if (!remapped.empty()) {
					LOG_INFO(Engine, "Remapping scene '%s' -> '%s'", sceneArg.c_str(), remapped.c_str());
					effectiveSceneName = remapped;
				}
			}
			initialSceneKey = SceneManager::Get().getKeyForName(effectiveSceneName);
			if (initialSceneKey == SIZE_MAX) {
				LOG_ERROR(Engine, "Unknown scene: %s", effectiveSceneName.c_str());
			}
		}
		if (initialSceneKey == SIZE_MAX && config.getDefaultSceneKey) {
			initialSceneKey = config.getDefaultSceneKey();
		}
		if (initialSceneKey != SIZE_MAX) {
			LOG_INFO(Engine, "Loading initial scene");
			SceneManager::Get().switchTo(initialSceneKey);
		}

		// Set up default pre-frame callback (debug server control handling)
		g_app->setPreFrameCallback([]() -> bool {
			if (g_metrics) {
				g_metrics->beginFrame();
			}

			Renderer::Primitives::beginFrame();

			// Handle debug server control actions
			if (g_debugServer) {
				Foundation::ControlAction action = g_debugServer->getControlAction();
				if (action != Foundation::ControlAction::None) {
					switch (action) {
						case Foundation::ControlAction::Exit:
							LOG_INFO(Engine, "Exit requested via control endpoint");
							g_app->stop();
							g_debugServer->clearControlAction();
							return false;

						case Foundation::ControlAction::SceneChange: {
							std::string sceneName = g_debugServer->getTargetSceneName();
							LOG_INFO(Engine, "Scene change requested: %s", sceneName.c_str());
							std::size_t key = SceneManager::Get().getKeyForName(sceneName);
							if (key != SIZE_MAX && SceneManager::Get().switchTo(key)) {
								LOG_INFO(Engine, "Switched to scene: %s", sceneName.c_str());
							} else {
								LOG_ERROR(Engine, "Failed to switch to scene: %s", sceneName.c_str());
							}
							g_debugServer->clearControlAction();
							break;
						}

						case Foundation::ControlAction::Pause:
							g_app->pause();
							g_debugServer->clearControlAction();
							break;

						case Foundation::ControlAction::Resume:
							g_app->resume();
							g_debugServer->clearControlAction();
							break;

						case Foundation::ControlAction::ReloadScene: {
							std::size_t currentKey = SceneManager::Get().getCurrentSceneKey();
							SceneManager::Get().switchTo(currentKey);
							g_debugServer->clearControlAction();
							break;
						}

						default:
							break;
					}
				}
			}
			return true;
		});

		// Set up default overlay renderer
		g_app->setOverlayRenderer([]() { Renderer::Primitives::endFrame(); });

		// Set up default post-frame callback
		g_app->setPostFrameCallback([]() {
			if (g_metrics) {
				auto renderStats = Renderer::Primitives::getStats();
				g_metrics->setRenderStats(renderStats.drawCalls, renderStats.vertexCount, renderStats.triangleCount);
				g_metrics->endFrame();
			}

			if (g_debugServer) {
				g_debugServer->setCurrentSceneName(SceneManager::Get().getCurrentSceneName());
				if (g_metrics) {
					g_debugServer->updateMetrics(g_metrics->getCurrentMetrics());
				}
				g_debugServer->captureScreenshotIfRequested();
			}
		});

		// Return context with pointers to initialized systems
		return AppContext{
			.window = window,
			.app = g_app.get(),
			.coordinateSystem = g_coordinateSystem,
			.debugServer = g_debugServer.get(),
			.metrics = g_metrics.get(),
			.hasSceneArg = hasSceneArg
		};
	}

	void AppLauncher::run(AppContext& ctx) {
		if (!ctx) {
			return;
		}

		// Run application
		LOG_INFO(Engine, "Starting application main loop");
		ctx.app->run();
	}

	int AppLauncher::shutdown(AppContext& ctx) {
		if (!ctx) {
			return 1;
		}

		// Cleanup debug server
		if (g_debugServer) {
			foundation::Logger::setDebugServer(nullptr);
			g_debugServer->signalShutdownComplete();
			g_debugServer->stop();
		}

		cleanup(ctx.window);

		return 0;
	}

	int AppLauncher::launch(int argc, char* argv[], const AppConfig& config) {
		auto ctx = initialize(argc, argv, config);
		if (!ctx) {
			return 1;
		}
		run(ctx);
		return shutdown(ctx);
	}

	void AppLauncher::setWindowResizeCallback(std::function<void()> callback) {
		g_windowResizeCallback = std::move(callback);
	}

	Renderer::MetricsCollector* AppLauncher::getMetrics() {
		return g_metrics.get();
	}

} // namespace engine
