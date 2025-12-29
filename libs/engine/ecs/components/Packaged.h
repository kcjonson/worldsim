#pragma once

// Packaged Component
//
// Marks furniture entities that are in a "packaged" state - meaning they haven't
// been placed yet. When furniture is crafted, it spawns as a packaged item that
// a colonist can carry. The player then uses ghost preview placement to position
// it in the world.
//
// Visual: Packaged items render with a box outline or slight transparency
// UI: When selected, shows "[Place]" button instead of normal actions
// Carrying: Packaged items are typically 2-handed

namespace ecs {

/// Marker component for furniture in packaged (unplaced) state.
/// Entities with this component:
/// - Render with box outline visual
/// - Show "[Place]" button in selection UI
/// - Can be picked up and carried by colonists
/// - Are removed when the furniture is "placed" via ghost preview
struct Packaged {
	// Currently just a marker - visual indicator is derived from presence
	// Future: could store original packaging appearance data if needed
};

} // namespace ecs
