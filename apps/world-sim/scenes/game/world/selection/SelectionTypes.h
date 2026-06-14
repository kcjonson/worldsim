#pragma once

// Selection - Polymorphic type for entity selection in the game
//
// Uses std::variant to represent different selection states:
// - NoSelection: Nothing selected (panel hidden)
// - ColonistSelection: An ECS colonist entity
// - WorldEntitySelection: A placed world entity (bush, tree, etc.)
// - WallSegmentSelection: A construction wall segment (ConstructionWorld topology)
// - FoundationSelection: A construction foundation (ConstructionWorld topology)

#include <construction/ConstructionWorld.h>
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
	std::string		 defName;  // Asset definition name
	Foundation::Vec2 position; // World position
};

/// A crafting station (ECS entity with WorkQueue) is selected
struct CraftingStationSelection {
	ecs::EntityID	 entityId; // ECS entity ID
	std::string		 defName;  // Asset definition name (e.g., "CraftingSpot")
	Foundation::Vec2 position; // World position
};

/// A furniture item (shelf, box, etc.) is selected
/// Furniture can be packaged (needs placement) or placed (can be re-packaged)
struct FurnitureSelection {
	ecs::EntityID	 entityId;	  // ECS entity ID
	std::string		 defName;	  // Asset definition name (e.g., "BasicShelf")
	Foundation::Vec2 position;	  // World position
	bool			 isPackaged;  // True if has Packaged component
};

/// A construction foundation is selected. Geometry, material, and state are
/// queried from the ConstructionWorld by id (topology is the source of truth,
/// not a snapshot here); the ECS mirror entity is reached via the foundation's
/// `entity` handle for blueprint/progress info.
struct FoundationSelection {
	engine::construction::FoundationId id = engine::construction::kInvalidFoundation;
};

/// A construction wall segment is selected. Like FoundationSelection, geometry,
/// material, thickness, and state are queried from the ConstructionWorld by id
/// (topology is the source of truth); the ECS mirror entity is reached via the
/// segment's `entity` handle for blueprint/progress info. The segment is the
/// design's unit of selection and demolition (D4/D6), not the chain.
struct WallSegmentSelection {
	engine::construction::SegmentId id = engine::construction::kInvalidSegment;
};

/// Selection variant - represents current selection state
using Selection = std::variant<
	NoSelection,
	ColonistSelection,
	WorldEntitySelection,
	CraftingStationSelection,
	FurnitureSelection,
	WallSegmentSelection,
	FoundationSelection>;

/// Helper to check if selection is empty
[[nodiscard]] inline bool hasSelection(const Selection& sel) {
	return !std::holds_alternative<NoSelection>(sel);
}

} // namespace world_sim
