#pragma once

// Opening Settings over the scene on top (the main menu, or the paused game): Back and Escape pop it and
// uncover that scene as it was left.

namespace app {
	class SceneStack;
} // namespace app

namespace world_sim {

	void openSettings(app::SceneStack& stack);

} // namespace world_sim
