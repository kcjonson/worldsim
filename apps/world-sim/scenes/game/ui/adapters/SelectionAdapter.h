#pragma once

// SelectionAdapter - Converts selection data into InfoSlots for display
//
// Adapters transform domain-specific data (colonist components, world entities)
// into generic slot descriptions that EntityInfoView can render.
// This decouples the panel from specific data sources.

#include "scenes/game/ui/components/InfoSlot.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/AssetRegistry.h>
#include <ecs/World.h>

#include <optional>

namespace world_sim {

/// Convert a Selection variant into panel content.
/// Returns std::nullopt for NoSelection (panel should hide).
[[nodiscard]] std::optional<PanelContent> adaptSelection(
	const Selection& selection,
	const ecs::World& world,
	const engine::assets::AssetRegistry& registry
);

/// Convert colonist data into two-column panel content
/// Left column: task info, gear list
/// Right column: needs bars
/// @param onDetails Optional callback for opening colonist details modal
[[nodiscard]] PanelContent adaptColonistStatus(
	const ecs::World& world,
	ecs::EntityID entityId,
	const std::function<void()>& onDetails = {}
);

/// Convert world entity data into panel content
[[nodiscard]] PanelContent adaptWorldEntity(
	const engine::assets::AssetRegistry& registry,
	const WorldEntitySelection& selection
);

/// Convert furniture entity data into panel content
/// Shows [Place] button for packaged furniture, [Package] button for placed furniture
/// @param onPlace Callback for placing packaged furniture
/// @param onPackage Callback for re-packaging placed furniture
[[nodiscard]] PanelContent adaptFurniture(
	const engine::assets::AssetRegistry& registry,
	const FurnitureSelection& selection,
	const std::function<void()>& onPlace = {},
	const std::function<void()>& onPackage = {}
);

} // namespace world_sim
