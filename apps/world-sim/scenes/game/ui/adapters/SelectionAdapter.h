#pragma once

// SelectionAdapter - Converts selection data into InfoSlots for display
//
// Adapters transform domain-specific data (colonist components, world entities)
// into generic slot descriptions that EntityInfoView can render.
// This decouples the panel from specific data sources.

#include "scenes/game/ui/components/InfoSlot.h"
#include "scenes/game/ui/components/Selection.h"

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

/// Convert colonist data into two-column panel content
/// Left column: task info, gear list
/// Right column: needs bars
/// @param onTaskListToggle Optional callback for toggling task list panel
/// @param onDetails Optional callback for opening colonist details modal
[[nodiscard]] PanelContent adaptColonistStatus(
	const ecs::World& world,
	ecs::EntityID entityId,
	std::function<void()> onTaskListToggle = nullptr,
	std::function<void()> onDetails = nullptr
);

/// Convert world entity data into panel content
[[nodiscard]] PanelContent adaptWorldEntity(
	const engine::assets::AssetRegistry& registry,
	const WorldEntitySelection& selection
);

} // namespace world_sim
