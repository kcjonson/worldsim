// UI Sandbox - Component Testing & Demo Environment
// Runs on salal's App, with the scene navigation menu as a debug layer

#include "BaseShader.h"
#include "NavigationMenu.h"
#include "SceneTypes.h"

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
		 .fontAtlases = theme::kFontAtlases,
		 .defaultFont = theme::fontDefault,
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

	ui_sandbox::addScenes(app->scenes());
	if (!commandLine.scene || !app->scenes().switchTo(*commandLine.scene)) {
		app->scenes().switchTo(ui_sandbox::scenes::Svg.name);
	}

	// The scene menu, open at the start unless a scene was asked for
	std::vector<std::string> sceneNames{
#define SCENE_NAME(scene) ui_sandbox::scenes::scene.name,
		UI_SANDBOX_SCENES(SCENE_NAME)
#undef SCENE_NAME
	};
	std::ranges::sort(sceneNames);
	app->addDebugLayer(std::make_unique<ui_sandbox::NavigationMenu>(*app, ui_sandbox::NavigationMenu::Args{.scenes = std::move(sceneNames), .open = !commandLine.scene}));

	app->run();
	app->shutdown();
	return 0;
}
