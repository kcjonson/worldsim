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
[[nodiscard]] std::optional<PanelContent> adaptSelection(
	const Selection& selection,
	const ecs::World& world,
	const engine::assets::AssetRegistry& registry
);

/// Convert colonist data into panel content
[[nodiscard]] PanelContent adaptColonist(const ecs::World& world, ecs::EntityID entityId);

/// Convert world entity data into panel content
[[nodiscard]] PanelContent adaptWorldEntity(
	const engine::assets::AssetRegistry& registry,
	const WorldEntitySelection& selection
);

} // namespace world_sim
