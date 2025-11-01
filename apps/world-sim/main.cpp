// World-Sim - Main Game Application
// Production game using unified Application-based game loop

#include <application/application.h>
#include <primitives/primitives.h>
#include <scene/scene_manager.h>
#include <utils/log.h>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <iostream>

// GLFW error callback
void ErrorCallback(int error, const char* description) {
	LOG_ERROR(Game, "GLFW Error (%d): %s", error, description);
}

// GLFW framebuffer size callback
void FramebufferSizeCallback(GLFWwindow* window, int width, int height) {
	glViewport(0, 0, width, height);
	Renderer::Primitives::SetViewport(width, height);
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

int main(int argc, char* argv[]) {
	// Initialize logging
	foundation::Logger::Initialize();

	LOG_INFO(Game, "World-Sim - Main Game");
	LOG_INFO(Game, "Version 0.1.0");

	// Initialize window and OpenGL
	GLFWwindow* window = InitializeWindow();
	if (window == nullptr) {
		foundation::Logger::Shutdown();
		return 1;
	}

	// Get window size
	int windowWidth = 0;
	int windowHeight = 0;
	glfwGetFramebufferSize(window, &windowWidth, &windowHeight);

	// Initialize renderer
	LOG_INFO(Renderer, "Initializing primitive rendering system");
	Renderer::Primitives::Init(nullptr);
	Renderer::Primitives::SetViewport(windowWidth, windowHeight);

	// Initialize scene system
	LOG_INFO(Engine, "Initializing scene system");
	// TODO: Register game scenes (splash, main_menu, world_creator, gameplay)
	// For now, we don't have any scenes registered, so the game will just show blank screen

	// Create application
	LOG_INFO(Engine, "Creating application");
	engine::Application app(window);

	// Set up pre-frame callback (primitives begin frame)
	app.SetPreFrameCallback([]() -> bool {
		Renderer::Primitives::BeginFrame();
		return true; // Continue running
	});

	// Set up overlay renderer (primitives end frame)
	app.SetOverlayRenderer([]() { Renderer::Primitives::EndFrame(); });

	// Run application
	LOG_INFO(Engine, "Starting application main loop");
	LOG_INFO(Game, "Game running... (No scenes registered yet - will show blank screen)");
	app.Run();

	// Cleanup
	LOG_INFO(Game, "Shutting down...");
	Renderer::Primitives::Shutdown();
	glfwDestroyWindow(window);
	glfwTerminate();
	foundation::Logger::Shutdown();

	return 0;
}
