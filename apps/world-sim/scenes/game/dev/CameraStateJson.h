#pragma once

// The JSON of `/api/state?what=camera`: where the world camera looks, enough to turn a world point into
// the logical window pixel `/api/input` takes and back. Numbers print to three decimals.

#include <string>

namespace world {
	class WorldCamera;
}

namespace world_sim {

	/// `pixelsPerMeter` is the scale at zoom 1; the width and height are the window's logical size.
	std::string cameraStateJson(const world::WorldCamera& camera, float pixelsPerMeter, int logicalWidth, int logicalHeight);

} // namespace world_sim
