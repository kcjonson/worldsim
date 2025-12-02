#pragma once

#include "AppConfig.h"
#include <memory>

// Forward declarations
struct GLFWwindow;
namespace Renderer {
class CoordinateSystem;
}
namespace Foundation {
class DebugServer;
}
namespace Renderer {
class MetricsCollector;
}

namespace engine {

class Application;

/// @brief Context returned by AppLauncher::initialize()
/// Contains all systems an app might need access to for custom setup
struct AppContext {
	GLFWwindow* window = nullptr;
	Application* app = nullptr;
	Renderer::CoordinateSystem* coordinateSystem = nullptr;
	Foundation::DebugServer* debugServer = nullptr;
	Renderer::MetricsCollector* metrics = nullptr;
	bool hasSceneArg = false; // True if --scene was specified on command line

	/// @brief Check if context is valid (initialization succeeded)
	explicit operator bool() const { return window != nullptr && app != nullptr; }
};

/// @brief Application launcher that handles all bootstrap boilerplate
///
/// This class encapsulates all the common initialization code that was
/// duplicated between ui-sandbox and world-sim Main.cpp files:
/// - GLFW window creation
/// - OpenGL/GLEW initialization
/// - CoordinateSystem setup
/// - Primitives rendering system
/// - FontRenderer
/// - Asset system
/// - Debug server (optional)
///
/// Usage (simple, no custom setup):
///   int main(int argc, char* argv[]) {
///       engine::AppConfig config{...};
///       return engine::AppLauncher::launch(argc, argv, config);
///   }
///
/// Usage (with custom callbacks, e.g., navigation menu):
///   int main(int argc, char* argv[]) {
///       engine::AppConfig config{...};
///       auto ctx = engine::AppLauncher::initialize(argc, argv, config);
///       if (!ctx) return 1;
///
///       // Set up app-specific callbacks
///       ctx.app->setOverlayRenderer([&]() { myOverlay.render(); });
///
///       return engine::AppLauncher::run(ctx);
///   }
class AppLauncher {
  public:
	/// @brief Initialize application systems without running main loop
	/// @param argc Command line argument count
	/// @param argv Command line arguments
	/// @param config Application configuration
	/// @return AppContext with initialized systems, or invalid context on failure
	static AppContext initialize(int argc, char* argv[], const AppConfig& config);

	/// @brief Run the main loop (does NOT cleanup - call shutdown() after)
	/// @param ctx Context from initialize()
	static void run(AppContext& ctx);

	/// @brief Cleanup all systems (call after run() and after app-specific cleanup)
	/// @param ctx Context from initialize()
	/// @return Exit code (0 for success)
	static int shutdown(AppContext& ctx);

	/// @brief Convenience method: initialize + run in one call
	/// @param argc Command line argument count
	/// @param argv Command line arguments
	/// @param config Application configuration
	/// @return Exit code (0 for success)
	static int launch(int argc, char* argv[], const AppConfig& config);

	/// @brief Set callback for window resize events
	/// Use this to handle app-specific resize logic (e.g., repositioning overlays)
	/// @param callback Function to call on window resize
	static void setWindowResizeCallback(std::function<void()> callback);

  private:
	AppLauncher() = delete; // Static-only class
};

} // namespace engine
