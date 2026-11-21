#pragma once

// Keeps the placed item index (PlacedItemIndex.h, rotatable-placed-items 11.3) for placement, the wall tool, the dev
// spawn, and a crafted crate's install target, which reach it through World::tryGetSystem.
//
// It runs right after NavigationSystem (51), which refreshes ColliderEntityIndex as it gathers: an item that leaves its
// crate to be installed (the install action, 350) is read as an obstacle and loses its pending target in the same
// refresh, the next tick, so it is never in neither. Spawns and removals the tick defers (DeferredEditsSystem, 800)
// arrive the tick after; an install target handed out in between is held (PlacedItemIndex::hold) until then.
// The world's input, read before World::update, sees the index as of the last tick.

#include "../ColliderEntityIndex.h"
#include "../ISystem.h"
#include "../PlacedItemIndex.h"

namespace ecs {

	class PlacedItemSystem : public ISystem {
	  public:
		void update(float deltaTime) override;
		[[nodiscard]] int		  priority() const override { return 52; }
		[[nodiscard]] const char* name() const override { return "PlacedItem"; }

		[[nodiscard]] const PlacedItemIndex& items() const { return index; }
		[[nodiscard]] PlacedItemIndex&		 items() { return index; }

	  private:
		PlacedItemIndex		index;
		ColliderEntityIndex noObstacles;
	};

} // namespace ecs
