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

	// ========== Asset Definitions (optional) ==========

	/// List of asset definition paths to load (relative to assets/definitions/)
	/// Example: {"flora/grass.xml", "flora/trees.xml"}
	std::vector<std::string> assetDefinitionPaths = {"flora/grass.xml"};
};

} // namespace engine
