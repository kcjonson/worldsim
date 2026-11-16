#pragma once

// Where a player's move order goes (player-visibility.md 3.7): the clicked point when it is seen,
// otherwise the last seen point on the way there from the colonist, so the order is one order to one
// point at the edge of seen ground. With player visibility off, always the clicked point.

#include <ecs/EntityID.h>

#include <glm/vec2.hpp>

#include <optional>

namespace ecs {
	class World;
}

namespace world_sim {

	/// Nullopt when the click is unseen and nothing on the way is: nothing is ordered.
	[[nodiscard]] std::optional<glm::vec2> moveOrderGoal(ecs::World& world, ecs::EntityID colonist, glm::vec2 clicked);

} // namespace world_sim
