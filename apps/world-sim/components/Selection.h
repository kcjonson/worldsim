#pragma once

// Selection - Polymorphic type for entity selection in the game
//
// Uses std::variant to represent different selection states:
// - NoSelection: Nothing selected (panel hidden)
// - ColonistSelection: An ECS colonist entity
// - WorldEntitySelection: A placed world entity (bush, tree, etc.)

#include <ecs/EntityID.h>
#include <math/Types.h>

#include <string>
#include <variant>

namespace world_sim {

/// No entity selected - panel should be hidden
struct NoSelection {};

/// A colonist (ECS entity) is selected
struct ColonistSelection {
	ecs::EntityID entityId;
};

/// A world entity (placed asset) is selected
struct WorldEntitySelection {
	std::string		  defName;	// Asset definition name
	Foundation::Vec2 position; // World position
};

/// Selection variant - represents current selection state
using Selection = std::variant<NoSelection, ColonistSelection, WorldEntitySelection>;

/// Helper to check if selection is empty
[[nodiscard]] inline bool hasSelection(const Selection& sel) {
	return !std::holds_alternative<NoSelection>(sel);
}

} // namespace world_sim
