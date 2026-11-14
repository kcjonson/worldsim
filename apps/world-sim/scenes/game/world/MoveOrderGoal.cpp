#include "MoveOrderGoal.h"

#include <ecs/World.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>

namespace world_sim {

	std::optional<glm::vec2> moveOrderGoal(ecs::World& world, ecs::EntityID colonist, glm::vec2 clicked) {
		const ecs::PlayerVisibilitySystem* visibility = ecs::PlayerVisibilitySystem::gate(world);
		if (visibility == nullptr || visibility->isSeen(clicked)) {
			return clicked;
		}
		const auto* position = world.getComponent<ecs::Position>(colonist);
		if (position == nullptr) {
			return std::nullopt;
		}
		return visibility->lastSeenToward(position->value, clicked);
	}

} // namespace world_sim
