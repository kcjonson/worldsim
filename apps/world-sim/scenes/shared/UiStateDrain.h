#pragma once

// UiStateDrain - serves /api/ui/tree, /api/ui/lint, and /api/state?what=scene
// for the pre-game scenes.
//
// GameScene owns its own state drain (it also answers world-state queries);
// every other scene calls serveUiStateRequests(*this) once per frame at the
// END of render(), after LayoutContainer has laid out, so the snapshot sees
// this frame's final geometry. Mirrors the ui-sandbox drain in its Main.cpp.

namespace engine {
	class IScene;
}

namespace world_sim {

	void serveUiStateRequests(engine::IScene& scene);

} // namespace world_sim
