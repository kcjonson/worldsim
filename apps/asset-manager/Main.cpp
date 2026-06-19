// Asset Manager - designer GUI for browsing, previewing, and inspecting the
// asset library. Sibling to ui-sandbox; renders every asset through the game's
// own pipeline.

#include "SceneTypes.h"
#include <application/AppLauncher.h>

int main(int argc, char* argv[]) {
	engine::AppConfig config{
		.windowTitle = "Asset Manager",
		.windowSizePercent = 0.8F,
		.enableDebugServer = true,
		.debugServerPort = 8081,
		.initializeScenes = asset_manager::initializeSceneManager,
		.getDefaultSceneKey = []() { return asset_manager::toKey(asset_manager::SceneType::Browser); },
		// Synchronous asset load (default): the browser reads the registry on entry.
	};

	return engine::AppLauncher::launch(argc, argv, config);
}
