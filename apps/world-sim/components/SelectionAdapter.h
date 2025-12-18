#pragma once

// SelectionAdapter - Converts selection data into InfoSlots for display
//
// Adapters transform domain-specific data (colonist components, world entities)
// into generic slot descriptions that EntityInfoPanel can render.
// This decouples the panel from specific data sources.

#include "InfoSlot.h"
#include "Selection.h"

#include <assets/AssetRegistry.h>
#include <ecs/World.h>

#include <optional>

namespace world_sim {

/// Convert a Selection variant into panel content.
/// Returns std::nullopt for NoSelection (panel should hide).
/// @param onTaskListToggle Optional callback for toggling task list panel (only used for colonists)
[[nodiscard]] std::optional<PanelContent> adaptSelection(
	const Selection& selection,
	const ecs::World& world,
	const engine::assets::AssetRegistry& registry,
	std::function<void()> onTaskListToggle = nullptr
);

/// Convert colonist status data into panel content (Status tab)
/// @param onTaskListToggle Optional callback for toggling task list panel
[[nodiscard]] PanelContent adaptColonistStatus(
	const ecs::World& world,
	ecs::EntityID entityId,
	std::function<void()> onTaskListToggle = nullptr
);

/// Convert colonist inventory data into panel content (Inventory tab)
[[nodiscard]] PanelContent adaptColonistInventory(
	const ecs::World& world,
	ecs::EntityID entityId
);

/// Legacy adapter - returns status content (for backward compatibility)
/// @deprecated Use adaptColonistStatus instead
[[nodiscard]] inline PanelContent adaptColonist(
	const ecs::World& world,
	ecs::EntityID entityId,
	std::function<void()> onTaskListToggle = nullptr
) {
	return adaptColonistStatus(world, entityId, onTaskListToggle);
}

/// Convert world entity data into panel content
[[nodiscard]] PanelContent adaptWorldEntity(
	const engine::assets::AssetRegistry& registry,
	const WorldEntitySelection& selection
);

} // namespace world_sim
