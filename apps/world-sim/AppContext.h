#pragma once

// world-sim's App context (App::setContext), which any scene reaches as app.context<world_sim::AppContext>().
// The scene handoffs (GameStartConfig, GameWorldState, NewGameSetup) can join it later.

#include <app/UiBridge.h>

namespace world_sim {

	struct AppContext {
		engine::UiBridge& ui; // GameScene zooms with the scroll the ui didn't take
	};

} // namespace world_sim
