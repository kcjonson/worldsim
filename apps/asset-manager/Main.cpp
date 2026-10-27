// Asset Manager - designer GUI for browsing, previewing, and inspecting the
// asset library. Sibling to ui-sandbox; renders every asset through the game's
// own pipeline.

#include "SceneTypes.h"
#include <application/AppLauncher.h>
#include <assets/AssetBootstrap.h>
#include <theme/Fonts.h>

int main(int argc, char* argv[]) {
	engine::AppConfig config{
		.windowTitle = "Asset Manager",
		.windowSizePercent = 0.8F,
		.fontAtlases = UI::kFontAtlases,
		.defaultFont = UI::fontDefault,
		.enableDebugServer = true,
		.debugServerPort = 8070,
		.initializeScenes = asset_manager::initializeSceneManager,
		.getDefaultSceneKey = []() { return asset_manager::toKey(asset_manager::SceneType::Browser); },
		// BrowserScene reads the registry on entry, so load synchronously.
		.initializeResources = [](bool /*hasSceneArg*/) { engine::assets::initializeAssetSystem(false); },
	};

	return engine::AppLauncher::launch(argc, argv, config);
}
