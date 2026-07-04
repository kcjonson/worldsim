// UI Sandbox - Component Testing & Demo Environment
// Uses shared AppLauncher with app-specific navigation menu overlay

#include "NavigationMenu.h"
#include "SceneTypes.h"
#include <application/AppLauncher.h>
#include <application/Application.h>
#include <debug/DebugServer.h>
#include <debug/LayoutLint.h>
#include <debug/UiTreeSerializer.h>
#include <primitives/Primitives.h>
#include <scene/SceneManager.h>
#include <utils/Log.h>

#include <optional>
#include <string>
#include <vector>

// Navigation menu (only created when no --scene argument)
static std::optional<UI::NavigationMenu> g_navigationMenu;

// Serve a pending ui.tree or ui.lint state query against the current scene's
// UI roots; any other query is consumed and answered with an error JSON so the
// HTTP request doesn't hang until its timeout. Runs on the main thread once per
// frame (after the scene rendered, so LayoutContainer positions are fresh);
// mirrors GameScene's drain in world-sim.
static void serveUiStateRequests() {
	auto* debugServer = engine::AppLauncher::getDebugServer();
	if (debugServer == nullptr) {
		return;
	}
	std::string what;
	if (!debugServer->consumeStateRequest(what)) {
		return;
	}
	if (what != "ui.tree" && what != "ui.lint") {
		debugServer->deliverState("{\"error\":\"unknown state query (ui-sandbox serves ui.tree and ui.lint)\"}");
		return;
	}
	engine::IScene* scene = engine::SceneManager::Get().getCurrentScene();
	std::vector<const UI::IComponent*> roots;
	if (scene != nullptr) {
		roots = scene->getUiRoots();
	}
	int viewportW = 0;
	int viewportH = 0;
	Renderer::Primitives::getLogicalViewport(viewportW, viewportH);
	const Foundation::Vec2 viewport{static_cast<float>(viewportW), static_cast<float>(viewportH)};
	debugServer->deliverState(
		what == "ui.tree" ? UI::serializeUiTreeJson(roots, viewport) : UI::lintUiTreeJson(roots, viewport)
	);
}

int main(int argc, char* argv[]) {
	engine::AppConfig config{
		.windowTitle = "UI Sandbox",
		.windowSizePercent = 0.8F,
		.enableDebugServer = true,
		.debugServerPort = 8090,
		.enableMetrics = true,
		.initializeScenes = ui_sandbox::initializeSceneManager,
		.getDefaultSceneKey =
			[]() {
				return ui_sandbox::toKey(ui_sandbox::SceneType::Svg);
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

	// Overlay renderer: flush primitives, then serve any pending UI-tree/lint
	// readback (post-render, so the snapshot sees this frame's layout)
	ctx.app->setOverlayRenderer([]() {
		Renderer::Primitives::endFrame();
		serveUiStateRequests();
	});

	// Window resize notifies SceneManager which forwards to overlays
	engine::AppLauncher::setWindowResizeCallback([]() { engine::SceneManager::Get().onWindowResize(); });

	engine::AppLauncher::run(ctx);

	// Clear overlays before destroying navigation menu (avoid dangling pointers)
	engine::SceneManager::Get().clearOverlays();
	g_navigationMenu.reset();

	return engine::AppLauncher::shutdown(ctx);
}
