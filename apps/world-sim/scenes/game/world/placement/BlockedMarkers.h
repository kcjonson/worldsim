#pragma once

// The blocked marker (rotatable-placed-items 12): over each installed item with nowhere to stand, a round warning
// badge with an exclamation mark, a constant size on the screen at any zoom. It reads PlacedItemIndex's use state and
// decides nothing.

#include <ecs/World.h>
#include <world/camera/WorldCamera.h>

namespace world_sim {

	class BlockedMarkers {
	  public:
		/// Draws a marker over every blocked item within the view. `uiScale` is logical px per ui px.
		static void render(ecs::World& world, const world::WorldCamera& camera, int viewportW, int viewportH, float pixelsPerMeter, float uiScale);
	};

} // namespace world_sim
