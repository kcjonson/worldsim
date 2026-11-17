#pragma once

// The JSON of `/api/state?what=drawing`: what the drawing tools hold, from one DrawingSnapshot.
// Every number is printed to three decimals, points are {x, y} in world meters, the
// enum names are the engine's own. Kept apart from the dev server so a test can build it.

#include "scenes/game/world/construction/DrawingSystem.h"

#include <string>

namespace world_sim {

	std::string drawingStateJson(const DrawingSnapshot& snapshot);

} // namespace world_sim
