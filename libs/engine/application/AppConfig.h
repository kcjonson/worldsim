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

	// ========== Asset System (optional) ==========

	/// Root folder for asset discovery (relative to executable)
	/// Asset definitions are loaded from all FolderName/FolderName.xml files recursively.
	/// Example: "assets/world" scans for assets/world/flora/GrassBlade/GrassBlade.xml etc.
	std::string assetsRootPath = "assets/world";
};

} // namespace engine
