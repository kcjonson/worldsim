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
#include <ecs/systems/RoomDetectionSystem.h>

#include <functional>
#include <optional>

namespace world_sim {

/// Callback to query remaining resource count for a world entity
using ResourceQueryCallback = std::function<std::optional<uint32_t>(const std::string& defName, Foundation::Vec2 position)>;

/// Convert a Selection variant into panel content.
/// Returns std::nullopt for NoSelection (panel should hide).
/// @param constructionWorld Topology store for Foundation/WallSegment selection (nullable).
/// @param onDemolish Callback for a foundation's Demolish button (nullable).
/// @param onDemolishWallSegment Callback for a wall segment's Demolish button (nullable).
/// @param onDemolishOpening Callback for an opening's Demolish button (nullable).
[[nodiscard]] std::optional<PanelContent> adaptSelection(
	const Selection&							   selection,
	const ecs::World&							   world,
	const engine::assets::AssetRegistry&		   registry,
	const ResourceQueryCallback&				   queryResources = {},
	const engine::construction::ConstructionWorld* constructionWorld = nullptr,
	const std::function<void()>&				   onDemolish = {},
	const std::function<void()>&				   onDemolishWallSegment = {},
	const std::function<void()>&				   onDemolishOpening = {}
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
/// Shows [Place] button for packaged furniture, [Move] button for placed furniture
/// Shows [Configure] button for storage containers
/// @param onPlace Callback for placing packaged furniture
/// @param onMoveFurniture Callback for moving (re-packaging + relocating) placed furniture
/// @param onConfigure Callback for opening storage configuration dialog
[[nodiscard]] PanelContent adaptFurniture(
	const engine::assets::AssetRegistry& registry,
	const FurnitureSelection& selection,
	const std::function<void()>& onPlace = {},
	const std::function<void()>& onMoveFurniture = {},
	const std::function<void()>& onConfigure = {}
);

/// Convert a selected construction foundation into panel content.
/// Pulls material/area/state from the ConstructionWorld and build progress from
/// the ECS StructureBlueprint on the foundation's mirror entity. The demolish
/// button is conditional: a built foundation that still hosts walls offers
/// "Demolish building" (the cascade) since the plain foundation removal would
/// orphan those walls; a clear (or blueprint) foundation offers "Demolish".
/// (ActionButtonSlot has no disabled flag, so this swaps the button rather than
/// disabling it.)
/// @param world ECS world (for the blueprint component).
/// @param constructionWorld Topology store (geometry, material, state, hosted walls).
/// @param selection The selected foundation.
/// @param onDemolish Callback for the plain Demolish button (nullable).
/// @param onDemolishBuilding Callback for the cascade Demolish-building button (nullable).
[[nodiscard]] PanelContent adaptFoundation(
	const ecs::World&							   world,
	const engine::construction::ConstructionWorld& constructionWorld,
	const FoundationSelection&					   selection,
	const std::function<void()>&				   onDemolish = {},
	const std::function<void()>&				   onDemolishBuilding = {}
);

/// Convert a selected wall segment into panel content.
/// Pulls material, thickness preset (name + meters), and length (from the
/// segment's vertex positions) from the ConstructionWorld, and build state +
/// progress + delivered/required materials from the ECS StructureBlueprint on the
/// segment's mirror entity. Emits a Demolish ActionButtonSlot wired to onDemolish.
/// Mirrors adaptFoundation; the segment (not the chain) is the demolition unit.
/// @param world ECS world (for the blueprint component).
/// @param constructionWorld Topology store (geometry, material, thickness, state).
/// @param selection The selected wall segment.
/// @param onDemolish Callback for the Demolish button (nullable).
[[nodiscard]] PanelContent adaptWallSegment(
	const ecs::World& world,
	const engine::construction::ConstructionWorld& constructionWorld,
	const WallSegmentSelection& selection,
	const std::function<void()>& onDemolish = {}
);

/// Convert a selected opening (door/window) into panel content.
/// Pulls type (Door/Window), material, pathable, and build state + progress +
/// delivered/required materials (from the ECS StructureBlueprint on the opening's
/// mirror entity) from the ConstructionWorld. Emits a Demolish ActionButtonSlot
/// wired to onDemolish. Mirrors adaptWallSegment; the opening is the demolition unit.
/// @param world ECS world (for the blueprint component).
/// @param constructionWorld Topology store (opening type, material, state).
/// @param selection The selected opening.
/// @param onDemolish Callback for the Demolish button (nullable).
[[nodiscard]] PanelContent adaptOpening(
	const ecs::World&							   world,
	const engine::construction::ConstructionWorld& constructionWorld,
	const OpeningSelection&						   selection,
	const std::function<void()>&				   onDemolish = {}
);

/// Convert a selected room into read-only panel content. Name + area come from the
/// RoomDetectionSystem record (the source of truth for room geometry; the Room ECS
/// component carries no polygon); the enclosing-wall count comes from the Room
/// component's boundingSegmentIds on the record's mirror entity. Rooms have NO
/// demolish action -- only their walls do -- so there are no action callbacks.
/// @param world ECS world (for the Room component).
/// @param record The room record resolved by roomId by the caller.
[[nodiscard]] PanelContent adaptRoom(const ecs::World& world, const ecs::RoomDetectionSystem::RoomRecord& record);

} // namespace world_sim
