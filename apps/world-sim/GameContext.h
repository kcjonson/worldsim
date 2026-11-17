#pragma once

// The game's App context (App::setContext): what main knows that the scenes read, set once
// before the first scene.

namespace world_sim {

	struct GameContext {
		// Launched with --background: the window opens behind the others, unfocused, and is driven
		// by injected input, so nothing may wait on the window's focus.
		bool backgroundLaunch = false;
	};

} // namespace world_sim
