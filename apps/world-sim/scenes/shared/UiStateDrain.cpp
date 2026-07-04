#include "scenes/shared/UiStateDrain.h"

#include <application/AppLauncher.h>
#include <debug/DebugServer.h>
#include <debug/LayoutLint.h>
#include <debug/UiTreeSerializer.h>
#include <primitives/Primitives.h>
#include <scene/Scene.h>

#include <string>
#include <vector>

namespace world_sim {

	void serveUiStateRequests(engine::IScene& scene) {
		auto* debugServer = engine::AppLauncher::getDebugServer();
		if (debugServer == nullptr) {
			return;
		}
		std::string what;
		if (!debugServer->consumeStateRequest(what)) {
			return;
		}
		if (what == "scene") {
			debugServer->deliverState(scene.exportState());
			return;
		}
		if (what != "ui.tree" && what != "ui.lint") {
			// Pre-game scenes have no world state; answer instead of letting the
			// HTTP request park until its timeout.
			debugServer->deliverState("{\"error\":\"unknown state query (this scene serves scene, ui.tree, ui.lint)\"}");
			return;
		}
		int viewportW = 0;
		int viewportH = 0;
		Renderer::Primitives::getLogicalViewport(viewportW, viewportH);
		const Foundation::Vec2 viewport{static_cast<float>(viewportW), static_cast<float>(viewportH)};
		const auto			   roots = scene.getUiRoots();
		debugServer->deliverState(
			what == "ui.tree" ? UI::serializeUiTreeJson(roots, viewport) : UI::lintUiTreeJson(roots, viewport)
		);
	}

} // namespace world_sim
