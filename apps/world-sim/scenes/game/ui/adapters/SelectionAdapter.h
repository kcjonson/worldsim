#pragma once

// SelectionAdapter - Converts selection data into InfoSlots for display
//
// Adapters transform domain-specific data (colonist components, world entities)
// into generic slot descriptions that EntityInfoView can render.
// This decouples the panel from specific data sources.

#include "scenes/game/ui/components/InfoSlot.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/AssetRegistry.h>
#include <construction/ConstructionWorld.h>
#include <ecs/World.h>

#include <functional>
#include <optional>

namespace world_sim {

/// Callback to query remaining resource count for a world entity
using ResourceQueryCallback = std::function<std::optional<uint32_t>(const std::string& defName, Foundation::Vec2 position)>;

/// Convert a Selection variant into panel content.
/// Returns std::nullopt for NoSelection (panel should hide).
/// @param constructionWorld Topology store for FoundationSelection (nullable).
/// @param onDemolish Callback for a foundation's Demolish button (nullable).
[[nodiscard]] std::optional<PanelContent> adaptSelection(
	const Selection& selection,
	const ecs::World& world,
	const engine::assets::AssetRegistry& registry,
	const ResourceQueryCallback& queryResources = {},
	const engine::construction::ConstructionWorld* constructionWorld = nullptr,
	const std::function<void()>& onDemolish = {}
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
/// @param queryResources Optional callback to query remaining resource count
[[nodiscard]] PanelContent adaptWorldEntity(
	const engine::assets::AssetRegistry& registry,
	const WorldEntitySelection& selection,
	const ResourceQueryCallback& queryResources = {}
);

/// Convert furniture entity data into panel content
/// Shows [Place] button for packaged furniture, [Package] button for placed furniture
/// Shows [Configure] button for storage containers
/// @param onPlace Callback for placing packaged furniture
/// @param onPackage Callback for re-packaging placed furniture
/// @param onConfigure Callback for opening storage configuration dialog
[[nodiscard]] PanelContent adaptFurniture(
	const engine::assets::AssetRegistry& registry,
	const FurnitureSelection& selection,
	const std::function<void()>& onPlace = {},
	const std::function<void()>& onPackage = {},
	const std::function<void()>& onConfigure = {}
);

/// Convert a selected construction foundation into panel content.
/// Pulls material/area/state from the ConstructionWorld and build progress from
/// the ECS StructureBlueprint on the foundation's mirror entity. Emits a
/// Demolish ActionButtonSlot wired to onDemolish.
/// @param world ECS world (for the blueprint component).
/// @param constructionWorld Topology store (geometry, material, state).
/// @param selection The selected foundation.
/// @param onDemolish Callback for the Demolish button (nullable).
[[nodiscard]] PanelContent adaptFoundation(
	const ecs::World& world,
	const engine::construction::ConstructionWorld& constructionWorld,
	const FoundationSelection& selection,
	const std::function<void()>& onDemolish = {}
);

} // namespace world_sim
