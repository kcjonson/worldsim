#include "PlacedItemSystem.h"

#include "NavigationSystem.h"

#include "../World.h"

#include <assets/AssetRegistry.h>

namespace ecs {

	void PlacedItemSystem::update(float /*deltaTime*/) {
		if (world == nullptr) {
			return;
		}
		// With no nav system nothing is an obstacle, so nothing is installed.
		const NavigationSystem* navigation = world->tryGetSystem<NavigationSystem>();
		index.refresh(navigation != nullptr ? navigation->colliderEntities() : noObstacles, *world, assets::AssetRegistry::Get());
	}

} // namespace ecs
