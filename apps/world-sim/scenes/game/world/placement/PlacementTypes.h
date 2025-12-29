#pragma once

// PlacementTypes - Shared types for the placement subsystem.
//
// Contains data structures used by both the placement system (world layer)
// and the build menu UI. UI layer includes this to display placeable items.

#include <string>

namespace world_sim {

/// A single item that can be built/placed.
/// Used by build menu UI to display options and by PlacementSystem
/// to track what's being placed.
struct BuildMenuItem {
	std::string defName; ///< Definition name (e.g., "CraftingSpot")
	std::string label;	 ///< Display name (e.g., "Crafting Spot")
};

} // namespace world_sim
