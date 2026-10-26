#pragma once

#include <functional>
#include <string>
#include <vector>

namespace engine {

/// @brief Configuration for application bootstrap
/// Each app provides this config to AppLauncher::initialize()
struct AppConfig {
	// ========== Window Settings ==========
	const char* windowTitle = "WorldSim Application";
	float windowSizePercent = 0.8F; // Percent of screen size

	// ========== Debug Server (optional) ==========
	bool enableDebugServer = false;
	int debugServerPort = 8081;

	// ========== Metrics Collection (optional) ==========
	bool enableMetrics = false;

	// ========== Scene System Callbacks (required) ==========

	/// Called to register all scenes with SceneManager
	/// Example: ui_sandbox::initializeSceneManager
	std::function<void()> initializeScenes;

	/// Returns the default scene key when no --scene argument provided
	/// Example: []() { return ui_sandbox::toKey(ui_sandbox::SceneType::Shapes); }
	std::function<std::size_t()> getDefaultSceneKey;

	/// Optional: Remap scene names from CLI (e.g., "game" -> "gameloading")
	/// Called before scene lookup. Return empty string to use original name.
	/// Example: [](const std::string& name) { return name == "game" ? "gameloading" : ""; }
	std::function<std::string(const std::string&)> remapSceneName;

	// ========== Resource Bootstrap (optional) ==========

	/// Called once after the window, GL context, and renderer systems exist and before
	/// Application is constructed, scenes are registered, or the initial scene's onEnter
	/// runs. Load game resources here (asset registry, tile atlas). `hasSceneArg` is true
	/// when --scene= was passed: the jumped-to scene reads resources in onEnter, so load
	/// anything it needs synchronously in that case.
	std::function<void(bool hasSceneArg)> initializeResources;

	/// Called once during shutdown, after scenes and the Application are gone and while the
	/// GL context is still alive. Release anything initializeResources created.
	std::function<void()> shutdownResources;
};

} // namespace engine
