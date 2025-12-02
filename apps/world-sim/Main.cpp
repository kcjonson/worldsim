// World-Sim - Main Game Application
// Production game using unified Application-based game loop

#include "CoordinateSystem/CoordinateSystem.h"
#include "SceneTypes.h"
#include "font/FontRenderer.h"
#include "primitives/Primitives.h"
#include "utils/Log.h"
#include "utils/ResourcePath.h"
#include <application/Application.h>
#include <assets/AssetRegistry.h>
#include <assets/generators/GrassBladeGenerator.h>
#include <scene/SceneManager.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <memory>

// Global systems (accessed by callbacks)
static Renderer::CoordinateSystem*		 g_coordinateSystem = nullptr;
static std::unique_ptr<ui::FontRenderer> g_fontRenderer = nullptr;

// GLFW error callback
void ErrorCallback(int error, const char* description) {
	LOG_ERROR(Game, "GLFW Error (%d): %s", error, description);
}

// GLFW framebuffer size callback
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
}

// Initialize GLFW and create window
GLFWwindow* InitializeWindow() {
	glfwSetErrorCallback(ErrorCallback);

	if (glfwInit() == 0) {
		LOG_ERROR(Game, "Failed to initialize GLFW");
		return nullptr;
	}

	// Get primary monitor for window sizing
	GLFWmonitor*	   primaryMonitor = glfwGetPrimaryMonitor();
	const GLFWvidmode* videoMode = glfwGetVideoMode(primaryMonitor);
	int				   windowWidth = static_cast<int>(static_cast<float>(videoMode->width) * 0.8F);
	int				   windowHeight = static_cast<int>(static_cast<float>(videoMode->height) * 0.8F);

	LOG_INFO(Game, "Screen: %dx%d", videoMode->width, videoMode->height);
	LOG_INFO(Game, "Window: %dx%d (80%% of screen)", windowWidth, windowHeight);

	// Configure GLFW for OpenGL 3.3 core profile
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE); // Required on macOS

	// Create window
	GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "World-Sim", nullptr, nullptr);
	if (window == nullptr) {
		LOG_ERROR(Game, "Failed to create GLFW window");
		glfwTerminate();
		return nullptr;
	}

	glfwMakeContextCurrent(window);
	glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
	glfwSwapInterval(1); // Enable vsync

	// Initialize GLEW
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (err != GLEW_OK) {
		LOG_ERROR(Game, "Failed to initialize GLEW: %s", glewGetErrorString(err));
		glfwDestroyWindow(window);
		glfwTerminate();
		return nullptr;
	}

	LOG_INFO(Renderer, "OpenGL Version: %s", glGetString(GL_VERSION));
	LOG_INFO(Renderer, "GLSL Version: %s", glGetString(GL_SHADING_LANGUAGE_VERSION));

	return window;
}

int main(int /*argc*/, char** /*argv*/) {
	// Initialize logging
	foundation::Logger::initialize();

	LOG_INFO(Game, "World-Sim - Main Game");
	LOG_INFO(Game, "Version 0.1.0");

	// Initialize window and OpenGL
	GLFWwindow* window = InitializeWindow();
	if (window == nullptr) {
		foundation::Logger::shutdown();
		return 1;
	}

	// Initialize coordinate system (for DPI-aware rendering and percentage helpers)
	LOG_INFO(Renderer, "Initializing coordinate system");
	Renderer::CoordinateSystem coordinateSystem;
	g_coordinateSystem = &coordinateSystem;
	if (!coordinateSystem.Initialize(window)) {
		LOG_ERROR(Renderer, "Failed to initialize coordinate system");
		glfwDestroyWindow(window);
		glfwTerminate();
		foundation::Logger::shutdown();
		return 1;
	}

	// Get window and framebuffer sizes
	int windowWidth = 0;
	int windowHeight = 0;
	glfwGetWindowSize(window, &windowWidth, &windowHeight);
	LOG_DEBUG(Renderer, "Window size (logical pixels): %dx%d", windowWidth, windowHeight);

	int framebufferWidth = 0;
	int framebufferHeight = 0;
	glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
	LOG_DEBUG(Renderer, "Framebuffer size (physical pixels): %dx%d", framebufferWidth, framebufferHeight);

	// Initialize primitive rendering system
	LOG_INFO(Renderer, "Initializing primitive rendering system");
	Renderer::Primitives::init(nullptr);
	Renderer::Primitives::setCoordinateSystem(&coordinateSystem);
	Renderer::Primitives::setViewport(framebufferWidth, framebufferHeight);

	// Initialize font renderer for UI text
	LOG_INFO(UI, "Initializing font renderer");
	g_fontRenderer = std::make_unique<ui::FontRenderer>();
	if (!g_fontRenderer->Initialize()) {
		LOG_ERROR(UI, "Failed to initialize FontRenderer!");
		// Continue anyway - text won't render but app will run
	} else {
		// Set font renderer in Primitives API for Text shapes
		Renderer::Primitives::setFontRenderer(g_fontRenderer.get());

		// Configure the unified BatchRenderer with the font atlas for text rendering
		Renderer::Primitives::setFontAtlas(g_fontRenderer->getAtlasTexture(), 4.0F);

		// Register frame update callback for LRU cache tracking
		Renderer::Primitives::setFrameUpdateCallback([]() {
			if (g_fontRenderer) {
				g_fontRenderer->updateFrame();
			}
		});

		LOG_INFO(UI, "Font renderer initialized");
	}

	// Initialize asset system
	LOG_INFO(Engine, "Initializing asset system");
	engine::assets::registerGrassBladeGenerator();
	std::string grassDefPath = Foundation::findResourceString("assets/definitions/flora/grass.xml");
	if (!grassDefPath.empty()) {
		if (engine::assets::AssetRegistry::Get().loadDefinitions(grassDefPath)) {
			LOG_INFO(Engine, "Loaded asset definitions from %s", grassDefPath.c_str());
		} else {
			LOG_WARNING(Engine, "Failed to load asset definitions from %s", grassDefPath.c_str());
		}
	} else {
		LOG_WARNING(Engine, "Asset definitions file not found: assets/definitions/flora/grass.xml");
	}

	// Create application (sets up InputManager, FocusManager, etc.)
	LOG_INFO(Engine, "Creating application");
	engine::Application app(window);

	// Initialize scene system with app-specific registry
	LOG_INFO(Engine, "Initializing scene system");
	world_sim::initializeSceneManager();

	// Set initial scene
	if (!engine::SceneManager::Get().switchTo(world_sim::toKey(world_sim::SceneType::Splash))) {
		LOG_WARNING(Engine, "Failed to load splash scene, trying MainMenu");
		if (!engine::SceneManager::Get().switchTo(world_sim::toKey(world_sim::SceneType::MainMenu))) {
			LOG_ERROR(Engine, "No scenes available - will show blank screen");
		}
	}

	// Set up pre-frame callback (primitives begin frame)
	app.setPreFrameCallback([]() -> bool {
		Renderer::Primitives::beginFrame();
		return true; // Continue running
	});

	// Set up overlay renderer (primitives end frame)
	app.setOverlayRenderer([]() { Renderer::Primitives::endFrame(); });

	// Run application
	LOG_INFO(Engine, "Starting application main loop");
	app.run();

	// Cleanup
	LOG_INFO(Game, "Shutting down...");

	// Shutdown scene system first - scenes may reference FocusManager, etc.
	engine::SceneManager::Get().shutdown();

	// Cleanup font renderer
	Renderer::Primitives::setFontRenderer(nullptr);
	g_fontRenderer.reset();
	g_coordinateSystem = nullptr;

	Renderer::Primitives::shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();
	foundation::Logger::shutdown();

	return 0;
}
