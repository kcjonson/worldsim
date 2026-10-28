// UI Sandbox - Component Testing & Demo Environment
// Runs on salal's App, with the scene navigation menu as an overlay

#include "BaseShader.h"
#include "NavigationMenu.h"
#include "SceneTypes.h"

#include <app/UiBridge.h>
#include <application/App.h>
#include <application/CommandLine.h>
#include <assets/AssetBootstrap.h>
#include <platform/Platform.h>
#include <theme/Fonts.h>
#include <utils/Log.h>

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

	constexpr const char* kTitle = "UI Sandbox";
	constexpr int		  kDevServerPort = 8090;

} // namespace

int main(int argc, char* argv[]) {
	const app::CommandLine commandLine = app::parseCommandLine(argc, argv);
	if (commandLine.help) {
		std::printf("Usage: %s [options]\n", kTitle);
		std::printf("Options:\n");
		std::printf("  --scene=<name>       Load specific scene\n");
		std::printf("  --http-port <port>   Enable HTTP debug server on port\n");
		std::printf("  --help               Show this help message\n");
	}
	if (commandLine.help || !commandLine.valid) {
		return 1;
	}

	std::unique_ptr<app::Platform> platform = app::createGlfwPlatform({.title = kTitle});
	if (platform == nullptr) {
		return 1;
	}
	std::unique_ptr<app::App> app = app::App::create(
		{.applicationName = "ui-sandbox",
		 .frame = {.frameCapHz = 120},
		 .baseShader = base_shader::source(),
		 .fontAtlases = UI::kFontAtlases,
		 .defaultFont = UI::fontDefault,
		 .clearColor = {0.1F, 0.1F, 0.15F, 1.0F},
		 // On wherever the server is compiled in, so a Release build, which has none, logs nothing
		 .devServer =
			 {.enabled = app::kDevServerCompiledIn || commandLine.httpPort.has_value(),
			  .port = commandLine.httpPort.value_or(kDevServerPort)},
		 .metrics = true},
		std::move(platform)
	);
	if (app == nullptr) {
		return 1;
	}

	// GrassScene reads the registry in onEnter (also via --scene=grass), so load synchronously.
	engine::assets::initializeAssetSystem(false);

	engine::UiBridge bridge(*app);
	ui_sandbox::addScenes(app->scenes());
	if (!commandLine.scene || !app->scenes().switchTo(*commandLine.scene)) {
		app->scenes().switchTo(ui_sandbox::scenes::Svg.name);
	}

	// Navigation menu overlay (only when no --scene argument)
	if (!commandLine.scene) {
		std::vector<std::string> sceneNames{
#define SCENE_NAME(scene) ui_sandbox::scenes::scene.name,
			UI_SANDBOX_SCENES(SCENE_NAME)
#undef SCENE_NAME
		};
		std::ranges::sort(sceneNames);
		const std::size_t		 sceneCount = sceneNames.size();
		UI::NavigationMenu::Args menuArgs{
			.sceneNames = std::move(sceneNames),
			.onSceneSelected =
				[&stack = app->scenes()](const std::string& sceneName) {
					if (stack.switchTo(sceneName)) {
						LOG_INFO(UI, "Switched to scene: %s", sceneName.c_str());
					}
				},
			.coordinateSystem = &app->coordinateSystem(),
		};
		auto menu = std::make_unique<UI::NavigationMenu>(menuArgs);
		bridge.addOverlay(*menu);
		app->addOverlay(std::move(menu));
		LOG_INFO(UI, "Navigation menu overlay registered (%zu scenes available)", sceneCount);
	}

	app->run();
	app->shutdown();
	return 0;
}
