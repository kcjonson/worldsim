#pragma once

// Packaged Component
//
// Marks entities in a "packaged" state - meaning they haven't been placed yet.
// When an item is crafted (e.g., BasicShelf, BasicBox), it spawns with this
// component. The player uses ghost preview placement to choose a location.
//
// State progression:
// - targetPosition = nullopt → Waiting for player to choose location ([Place] button)
// - targetPosition = value   → Awaiting colonist delivery to target location
// - Component removed        → Item is placed and functional
//
// Visual: Packaged items render with a box outline. When targetPosition is set,
//         a ghost is rendered at the target location.
// UI: When selected and no target, shows "[Place]" button
// Carrying: Packaged items are typically 2-handed

#include <glm/vec2.hpp>

#include <optional>

namespace ecs {

/// Component for entities in packaged (unplaced) state.
/// Entities with this component:
/// - Render with box outline visual
/// - Show "[Place]" button in selection UI (when no target set)
/// - Can be picked up and carried by colonists (2-handed)
/// - Are delivered to targetPosition by colonists when set
/// - Component is removed when the item is finally placed
struct Packaged {
	/// Target position for placement.
	/// - nullopt: Player hasn't chosen a location yet (shows [Place] button)
	/// - has value: Colonist should pick up and deliver to this position
	std::optional<glm::vec2> targetPosition;

	/// True when a colonist is currently carrying this entity.
	/// Used to hide the entity from world rendering while in transit.
	/// The entity's Position stays at original location until placed.
	bool beingCarried = false;
};

} // namespace ecs
