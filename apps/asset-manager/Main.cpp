// Asset Manager - designer GUI for browsing, previewing, and inspecting the
// asset library. Sibling to ui-sandbox; renders every asset through the game's
// own pipeline.

#include "BaseShader.h"
#include "SceneTypes.h"

#include <application/App.h>
#include <application/CommandLine.h>
#include <assets/AssetBootstrap.h>
#include <platform/Platform.h>
#include <theme/Fonts.h>

#include <cstdio>
#include <memory>
#include <utility>

namespace {

	constexpr const char* kTitle = "Asset Manager";
	constexpr int		  kDevServerPort = 8070;

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
		{.applicationName = "asset-manager",
		 .frame = {.frameCapHz = 120},
		 .baseShader = base_shader::source(),
		 .fontAtlases = theme::kFontAtlases,
		 .defaultFont = theme::fontDefault,
		 .clearColor = {0.1F, 0.1F, 0.15F, 1.0F},
		 // On wherever the server is compiled in, so a Release build, which has none, logs nothing
		 .devServer =
			 {.enabled = app::kDevServerCompiledIn || commandLine.httpPort.has_value(),
			  .port = commandLine.httpPort.value_or(kDevServerPort)}},
		std::move(platform)
	);
	if (app == nullptr) {
		return 1;
	}

	// BrowserScene reads the registry on entry, so load synchronously.
	engine::assets::initializeAssetSystem(false);

	asset_manager::addScenes(app->scenes());
	if (!commandLine.scene || !app->scenes().switchTo(*commandLine.scene)) {
		app->scenes().switchTo(asset_manager::scenes::Browser.name);
	}

	app->run();
	app->shutdown();
	return 0;
}
