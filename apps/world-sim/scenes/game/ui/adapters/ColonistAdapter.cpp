#include "ColonistAdapter.h"

#include <ecs/components/Colonist.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Needs.h>

namespace world_sim::adapters {

	std::vector<ColonistData> getColonists(ecs::World& world) {
		std::vector<ColonistData> result;

		// Query all colonist entities
		for (auto [entity, colonist] : world.view<ecs::Colonist>()) {
			float mood = 100.0F; // Default to full mood

			// Compute mood from needs if available
			if (auto* needs = world.getComponent<ecs::NeedsComponent>(entity)) {
				mood = ecs::computeMood(*needs);
			}

			result.push_back({.id = entity, .name = colonist.name, .mood = mood});
		}

		return result;
	}

} // namespace world_sim::adapters
