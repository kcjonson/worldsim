// World-Sim - Main Game Application
// Runs on salal's App: the window, the frame, the scene stack and the dev server

#include "BaseShader.h"
#include "GameBootstrap.h"
#include "SceneTypes.h"

#include <application/App.h>
#include <application/CommandLine.h>
#include <platform/Platform.h>
#include <theme/Fonts.h>
#include <utils/Log.h>

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

namespace {

	constexpr const char* kTitle = "World-Sim";
	constexpr int		  kDevServerPort = 8081;

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
		{.applicationName = "world-sim",
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

	world_sim::initializeResources(commandLine.scene.has_value());

	world_sim::addScenes(app->scenes());

	// "game" starts at "gameloading", so loading always runs first
	std::string firstScene = commandLine.scene.value_or(world_sim::scenes::Splash.name);
	if (firstScene == world_sim::scenes::Game.name) {
		LOG_INFO(Engine, "Remapping scene '%s' -> '%s'", firstScene.c_str(), world_sim::scenes::GameLoading.name);
		firstScene = world_sim::scenes::GameLoading.name;
	}
	if (!app->scenes().switchTo(firstScene)) {
		app->scenes().switchTo(world_sim::scenes::Splash.name);
	}

	app->run();
	app->shutdown();
	world_sim::shutdownResources();
	return 0;
}
