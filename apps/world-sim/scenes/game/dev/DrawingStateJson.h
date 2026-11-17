#pragma once

// The JSON of `/api/state?what=drawing`: what the drawing tools hold, from one DrawingSnapshot and the settings, key guide and dialog around it.
// Every number is printed to three decimals, points are {x, y} in world meters, the
// enum names are the engine's own. Kept apart from the dev server so a test can build it.

#include "UserSettings.h"
#include "scenes/game/world/construction/DrawingSystem.h"

#include <string>

namespace world_sim {

	struct DrawingStateContext {
		DrawingSettings settings;
		bool			keyGuideCollapsed = false;
		bool			dialogOpen = false; // a dialog is open over the game
	};

	std::string drawingStateJson(const DrawingSnapshot& snapshot, const DrawingStateContext& context);

} // namespace world_sim
