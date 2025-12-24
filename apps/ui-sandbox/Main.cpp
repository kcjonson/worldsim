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
		.getDefaultSceneKey =
			[]() {
				return ui_sandbox::toKey(ui_sandbox::SceneType::Shapes);
			}
		// Uses default assetsRootPath = "assets/world"
	};

	auto ctx = engine::AppLauncher::initialize(argc, argv, config);
	if (!ctx) {
		return 1;
	}

	// Set up navigation menu overlay (only when no --scene argument)
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

		// Register with SceneManager - it handles input/update/render lifecycle
		engine::SceneManager::Get().pushOverlay(&(*g_navigationMenu));
		LOG_INFO(UI, "Navigation menu overlay registered (%zu scenes available)", sceneNames.size());
	}

	// Overlay renderer just for Primitives::endFrame()
	ctx.app->setOverlayRenderer([]() { Renderer::Primitives::endFrame(); });

	// Window resize notifies SceneManager which forwards to overlays
	engine::AppLauncher::setWindowResizeCallback([]() { engine::SceneManager::Get().onWindowResize(); });

	engine::AppLauncher::run(ctx);

	// Clear overlays before destroying navigation menu (avoid dangling pointers)
	engine::SceneManager::Get().clearOverlays();
	g_navigationMenu.reset();

	return engine::AppLauncher::shutdown(ctx);
}
