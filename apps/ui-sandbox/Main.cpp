// UI Sandbox - Component Testing & Demo Environment
// Uses shared AppLauncher with app-specific navigation menu overlay

#include "NavigationMenu.h"
#include "SceneTypes.h"
#include <application/AppLauncher.h>
#include <application/Application.h>
#include <primitives/Primitives.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>

#include <optional>

// Navigation menu (only created when no --scene argument)
static std::optional<UI::NavigationMenu> g_navigationMenu;

int main(int argc, char* argv[]) {
	engine::AppConfig config{
		.windowTitle = "UI Sandbox",
		.windowSizePercent = 0.8F,
		.enableDebugServer = true,
		.debugServerPort = 8081,
		.enableMetrics = true,
		.initializeScenes = ui_sandbox::initializeSceneManager,
		.getDefaultSceneKey = []() { return ui_sandbox::toKey(ui_sandbox::SceneType::Shapes); },
		.assetDefinitionPaths = {"flora/grass.xml"}
	};

	auto ctx = engine::AppLauncher::initialize(argc, argv, config);
	if (!ctx) {
		return 1;
	}

	// Set up navigation menu (only when no --scene argument)
	if (!ctx.hasSceneArg) {
		auto sceneNames = engine::SceneManager::Get().getAllSceneNames();
		g_navigationMenu.emplace(
			UI::NavigationMenu::Args{
				.sceneNames = sceneNames,
				.onSceneSelected =
					[](const std::string& sceneName) {
						engine::SceneKey key = engine::SceneManager::Get().getKeyForName(sceneName);
						if (key != SIZE_MAX) {
							engine::SceneManager::Get().switchTo(key);
							LOG_INFO(UI, "Switched to scene: %s", sceneName.c_str());
						}
					},
				.coordinateSystem = ctx.coordinateSystem
			}
		);
		LOG_INFO(UI, "Navigation menu enabled (%zu scenes available)", sceneNames.size());
	}

	// Set up overlay renderer with navigation menu
	ctx.app->setOverlayRenderer([]() {
		if (g_navigationMenu) {
			g_navigationMenu->handleInput();
			g_navigationMenu->update(0.0F);
			g_navigationMenu->render();
		}
		Renderer::Primitives::endFrame();
	});

	// Set up window resize handler for navigation menu
	engine::AppLauncher::setWindowResizeCallback([]() {
		if (g_navigationMenu) {
			g_navigationMenu->onWindowResize();
		}
	});

	engine::AppLauncher::run(ctx);

	// Cleanup navigation menu before engine shutdown (FocusManager still alive)
	g_navigationMenu.reset();

	return engine::AppLauncher::shutdown(ctx);
}
