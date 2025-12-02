// World-Sim - Main Game Application
// Uses shared AppLauncher for all bootstrap boilerplate

#include "SceneTypes.h"
#include <application/AppLauncher.h>

int main(int argc, char* argv[]) {
	engine::AppConfig config{
		.windowTitle = "World-Sim",
		.windowSizePercent = 0.8F,
		.enableDebugServer = true,
		.debugServerPort = 8081,
		.enableMetrics = true,
		.initializeScenes = world_sim::initializeSceneManager,
		.getDefaultSceneKey = []() { return world_sim::toKey(world_sim::SceneType::Splash); },
		.assetDefinitionPaths = {"flora/grass.xml", "flora/trees.xml"}
	};

	return engine::AppLauncher::launch(argc, argv, config);
}
