#include "EntityInfoModel.h"

#include "scenes/game/ui/adapters/CraftingAdapter.h"
#include "scenes/game/ui/adapters/SelectionAdapter.h"

#include <ecs/components/ResourceStack.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/PlacedItemSystem.h>
#include <ecs/systems/WallStandPoint.h>

namespace world_sim {

namespace {

	/// What is wrong with an item a colonist can't use (rotatable-placed-items 12) and what to do about it, first in
	/// its panel; nothing while it is usable.
	void prependUseNote(ecs::World& world, ecs::EntityID entity, PanelContent& content) {
		const ecs::PlacedItemSystem*	   items = world.tryGetSystem<ecs::PlacedItemSystem>();
		const ecs::PlacedItemIndex::Entry* entry = items != nullptr ? items->items().find(entity) : nullptr;
		if (entry == nullptr || entry->pending || entry->use == ecs::UseState::Usable) {
			return;
		}
		std::vector<InfoSlot> note;
		if (entry->use == ecs::UseState::Blocked) {
			note.emplace_back(TextSlot{.label = "Blocked", .value = "Nowhere to stand"});
			note.emplace_back(TextListSlot{
				.header = "Something is in the way of the place a colonist stands to use this.",
				.items = {"Move what is in the way, or move this somewhere with room to stand. Its orders wait until then."},
			});
		} else {
			note.emplace_back(TextSlot{.label = "Use area", .value = "Unexplored"});
			note.emplace_back(TextListSlot{
				.header = "Nobody has seen the ground a colonist would stand on to use this.",
				.items = {"It can be used once a colonist has been there."},
			});
		}
		content.slots.insert(content.slots.begin(), note.begin(), note.end());
	}

} // namespace

// ============================================================================
// CachedSelection implementation
// ============================================================================

bool CachedSelection::matches(const Selection& selection) const {
	return std::visit(
		[this](const auto& sel) -> bool {
			using T = std::decay_t<decltype(sel)>;
			if constexpr (std::is_same_v<T, NoSelection>) {
				return type == Type::None;
			} else if constexpr (std::is_same_v<T, ColonistSelection>) {
				return type == Type::Colonist && colonistId == sel.entityId;
			} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
				return type == Type::WorldEntity && worldEntityDef == sel.defName && worldEntityPos.x == sel.position.x &&
					   worldEntityPos.y == sel.position.y;
			} else if constexpr (std::is_same_v<T, CraftingStationSelection>) {
				return type == Type::CraftingStation && stationId == sel.entityId;
			} else if constexpr (std::is_same_v<T, FurnitureSelection>) {
				return type == Type::Furniture && furnitureId == sel.entityId && furniturePackaged == sel.isPackaged;
			} else if constexpr (std::is_same_v<T, ResourcePileSelection>) {
				return type == Type::ResourcePile && pileId == sel.entityId;
			} else if constexpr (std::is_same_v<T, WallSegmentSelection>) {
				return type == Type::WallSegment && wallSegmentId == sel.id;
			} else if constexpr (std::is_same_v<T, OpeningSelection>) {
				return type == Type::Opening && openingId == sel.id;
			} else if constexpr (std::is_same_v<T, FoundationSelection>) {
				return type == Type::Foundation && foundationId == sel.id;
			} else if constexpr (std::is_same_v<T, RoomSelection>) {
				return type == Type::Room && roomId == sel.roomId;
			}
			return false;
		},
		selection
	);
}

void CachedSelection::update(const Selection& selection) {
	std::visit(
		[this](const auto& sel) {
			using T = std::decay_t<decltype(sel)>;
			// Reset all identity fields, then set the ones the active variant needs.
			colonistId = ecs::EntityID{0};
			stationId = ecs::EntityID{0};
			furnitureId = ecs::EntityID{0};
			pileId = ecs::EntityID{0};
			worldEntityDef.clear();
			stationDefName.clear();
			furnitureDefName.clear();
			worldEntityPos = {};
			furniturePackaged = false;
			foundationId = construction::kInvalidFoundation;
			wallSegmentId = construction::kInvalidSegment;
			openingId = construction::kInvalidOpening;
			roomId = 0;

			if constexpr (std::is_same_v<T, NoSelection>) {
				type = Type::None;
			} else if constexpr (std::is_same_v<T, ColonistSelection>) {
				type = Type::Colonist;
				colonistId = sel.entityId;
			} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
				type = Type::WorldEntity;
				worldEntityDef = sel.defName;
				worldEntityPos = sel.position;
			} else if constexpr (std::is_same_v<T, CraftingStationSelection>) {
				type = Type::CraftingStation;
				stationId = sel.entityId;
				stationDefName = sel.defName;
			} else if constexpr (std::is_same_v<T, FurnitureSelection>) {
				type = Type::Furniture;
				furnitureId = sel.entityId;
				furnitureDefName = sel.defName;
				furniturePackaged = sel.isPackaged;
			} else if constexpr (std::is_same_v<T, ResourcePileSelection>) {
				type = Type::ResourcePile;
				pileId = sel.entityId;
			} else if constexpr (std::is_same_v<T, WallSegmentSelection>) {
				type = Type::WallSegment;
				wallSegmentId = sel.id;
			} else if constexpr (std::is_same_v<T, OpeningSelection>) {
				type = Type::Opening;
				openingId = sel.id;
			} else if constexpr (std::is_same_v<T, FoundationSelection>) {
				type = Type::Foundation;
				foundationId = sel.id;
			} else if constexpr (std::is_same_v<T, RoomSelection>) {
				type = Type::Room;
				roomId = sel.roomId;
			}
		},
		selection
	);
}

// ============================================================================
// EntityInfoModel implementation
// ============================================================================

EntityInfoModel::UpdateType EntityInfoModel::refresh(
	const Selection&							   selection,
	ecs::World&									   world,
	const assets::AssetRegistry&		   assetRegistry,
	const assets::RecipeRegistry&		   recipeRegistry,
	const Callbacks&							   callbacks,
	const construction::ConstructionWorld* constructionWorld,
	const ecs::RoomDetectionSystem*				   roomDetection
) {
	// Detect selection types
	bool		  isColonist = std::holds_alternative<ColonistSelection>(selection);
	bool		  isStation = std::holds_alternative<CraftingStationSelection>(selection);
	bool		  isFurniture = std::holds_alternative<FurnitureSelection>(selection);
	bool		  isPile = std::holds_alternative<ResourcePileSelection>(selection);
	bool		  isWallSegment = std::holds_alternative<WallSegmentSelection>(selection);
	bool		  isOpening = std::holds_alternative<OpeningSelection>(selection);
	bool		  isFoundation = std::holds_alternative<FoundationSelection>(selection);
	bool		  isRoom = std::holds_alternative<RoomSelection>(selection);
	ecs::EntityID colonistId{0};
	ecs::EntityID stationId{0};
	std::string   stationDefName;

	// Resolve the room record up front so the existence guard and the content arm
	// share one lookup. A room whose id vanished (wall demolished out from under it)
	// resolves to null and is treated as not-a-room, so the panel clears rather than
	// showing a stale record (mirrors the foundation existence guard).
	const ecs::RoomDetectionSystem::RoomRecord* roomRecord = nullptr;

	if (isColonist) {
		colonistId = std::get<ColonistSelection>(selection).entityId;
		if (!world.isAlive(colonistId)) {
			isColonist = false;
		}
	}
	if (isStation) {
		const auto& stationSel = std::get<CraftingStationSelection>(selection);
		stationId = stationSel.entityId;
		stationDefName = stationSel.defName;
		if (!world.isAlive(stationId)) {
			isStation = false;
		}
	}
	if (isFurniture) {
		const auto& furnitureSel = std::get<FurnitureSelection>(selection);
		if (!world.isAlive(furnitureSel.entityId)) {
			isFurniture = false;
		}
	}
	if (isPile) {
		const auto& pileSel = std::get<ResourcePileSelection>(selection);
		if (!world.hasComponent<ecs::ResourceStack>(pileSel.entityId)) {
			isPile = false;
		}
	}
	if (isWallSegment) {
		const auto& wallSel = std::get<WallSegmentSelection>(selection);
		if (constructionWorld == nullptr || constructionWorld->getSegment(wallSel.id) == nullptr) {
			isWallSegment = false;
		}
	}
	if (isOpening) {
		const auto& openingSel = std::get<OpeningSelection>(selection);
		if (constructionWorld == nullptr || constructionWorld->getOpening(openingSel.id) == nullptr) {
			isOpening = false;
		}
	}
	if (isFoundation) {
		const auto& foundationSel = std::get<FoundationSelection>(selection);
		if (constructionWorld == nullptr || constructionWorld->get(foundationSel.id) == nullptr) {
			isFoundation = false;
		}
	}
	if (isRoom) {
		const auto& roomSel = std::get<RoomSelection>(selection);
		if (roomDetection != nullptr) {
			for (const auto& record : roomDetection->rooms()) {
				if (record.roomId == roomSel.roomId) {
					roomRecord = &record;
					break;
				}
			}
		}
		if (roomRecord == nullptr) {
			isRoom = false; // id vanished (wall demolished); fall through to the hide path
		}
	}

	// Handle NoSelection -> hide panel. A RoomSelection whose room vanished resolves
	// to no room above (isRoom cleared, no record) and matches none of the content
	// arms below; treat it like NoSelection so the panel clears rather than showing
	// a stale room. Same for a pile hauled down to nothing.
	const bool roomVanished = std::holds_alternative<RoomSelection>(selection) && !isRoom;
	const bool pileVanished = std::holds_alternative<ResourcePileSelection>(selection) && !isPile;
	if (std::holds_alternative<NoSelection>(selection) || roomVanished || pileVanished) {
		if (visible) {
			visible = false;
			cachedSelection.update(selection);
			return UpdateType::Hide;
		}
		return UpdateType::None;
	}

	// Determine if panel needs to show
	bool wasVisible = visible;
	visible = true;

	// Track layout mode change
	bool wasColonist = isColonistFlag;
	isColonistFlag = isColonist;

	// Check if selection identity changed
	bool selectionChanged = !cachedSelection.matches(selection);
	if (selectionChanged) {
		cachedSelection.update(selection);
	}

	// Determine update type (structure update if selection changed or layout mode changed)
	bool needsStructure = selectionChanged || wasColonist != isColonistFlag;

	// Capture the previous content shape before regenerating. A Values update only rewrites
	// existing slots in place; it can't add or remove them. So when the slot count changes
	// for the same selection - e.g. a station's "no materials found" warning appearing or
	// clearing while it stays selected - force a Structure rebuild instead, or the new slots
	// never render (and stale ones linger).
	const size_t prevSlotCount = contentData.slots.size();

	// Generate content
	if (isColonist) {
		contentData = adaptColonistStatus(world, colonistId);
	} else if (isStation) {
		contentData = getCraftingStationContent(world, stationId, stationDefName, callbacks.onOpenCraftingDialog);
		prependUseNote(world, stationId, contentData);
	} else if (isFurniture) {
		const auto& furnitureSel = std::get<FurnitureSelection>(selection);
		// Create onConfigure callback that captures entity info for storage config dialog
		std::function<void()> onConfigure;
		if (callbacks.onOpenStorageConfig) {
			auto entityId = furnitureSel.entityId;
			auto defName = furnitureSel.defName;
			onConfigure = [cb = callbacks.onOpenStorageConfig, entityId, defName]() {
				cb(entityId, defName);
			};
		}
		contentData = adaptFurniture(assetRegistry, furnitureSel, callbacks.onPlace, callbacks.onMoveFurniture, onConfigure);
		prependUseNote(world, furnitureSel.entityId, contentData);
	} else if (isPile) {
		contentData = adaptResourcePile(world, assetRegistry, std::get<ResourcePileSelection>(selection));
	} else if (isWallSegment && constructionWorld != nullptr) {
		const auto& wallSel = std::get<WallSegmentSelection>(selection);
		const auto* segment = constructionWorld->getSegment(wallSel.id);
		contentData = adaptWallSegment(
			world, *constructionWorld, wallSel, callbacks.onDemolishWallSegment,
			segment != nullptr && wallHasNoGround(world, *constructionWorld, segment->entity)
		);
	} else if (isOpening && constructionWorld != nullptr) {
		const auto& openingSel = std::get<OpeningSelection>(selection);
		contentData = adaptOpening(world, *constructionWorld, openingSel, callbacks.onDemolishOpening);
	} else if (isFoundation && constructionWorld != nullptr) {
		const auto& foundationSel = std::get<FoundationSelection>(selection);
		contentData = adaptFoundation(
			world,
			*constructionWorld,
			foundationSel,
			{.onDemolish = callbacks.onDemolishFoundation,
			 .onDemolishBuilding = callbacks.onDemolishBuilding,
			 .onAdd = callbacks.onAddToFoundation,
			 .onSubtract = callbacks.onSubtractFromFoundation}
		);
	} else if (isRoom && roomRecord != nullptr) {
		contentData = adaptRoom(world, *roomRecord);
	} else {
		// World entity - use standard adapter with resource query callback
		auto worldContent = adaptSelection(selection, world, assetRegistry, callbacks.queryResources);
		if (worldContent.has_value()) {
			contentData = std::move(worldContent.value());
		}
	}

	// A change in slot composition (not just values) needs a full relayout.
	if (contentData.slots.size() != prevSlotCount) {
		needsStructure = true;
	}

	// Return appropriate update type
	if (!wasVisible) {
		return UpdateType::Show;
	}
	if (needsStructure) {
		return UpdateType::Structure;
	}
	return UpdateType::Values;
}

bool EntityInfoModel::wallHasNoGround(ecs::World& world, const construction::ConstructionWorld& constructionWorld, ecs::EntityID wallEntity) {
	const auto* navigation = world.tryGetSystem<ecs::NavigationSystem>();
	if (navigation == nullptr) {
		return false;
	}
	const auto*			goal = ecs::GoalTaskRegistry::Get().getGoalByDestination(wallEntity);
	const WallGroundKey key{
		.wallEntity = wallEntity,
		.constructionVersion = constructionWorld.version(),
		.navigationGeneration = navigation->generation(),
		.navigationConnectivity = navigation->connectivityEpoch(),
		.goalId = goal != nullptr ? goal->id : 0,
		.goalStatus = goal != nullptr ? static_cast<int>(goal->status) : -1,
	};
	if (key != wallGroundKey) {
		wallGroundKey = key;
		// Asked for a colonist's default radius, as a builder of the wall would be.
		wallNoGround = goal != nullptr && goal->type == ecs::GoalType::Build &&
					   ecs::noGroundToBuildFrom(goal->wallStands, ecs::AgentRadius{}.radiusMeters, *navigation);
	}
	return wallNoGround;
}

PanelContent EntityInfoModel::getCraftingStationContent(
	ecs::World& world,
	ecs::EntityID entityId,
	const std::string& stationDefName,
	const OpenCraftingDialogCallback& onOpenCraftingDialog
) const {
	// Get base status content (shows queue status, not recipes)
	PanelContent content = adaptCraftingStatus(world, entityId, stationDefName);

	// Add "Open Crafting Menu" button
	content.slots.push_back(ActionButtonSlot{
		.label = "Open Crafting Menu",
		.onClick = [onOpenCraftingDialog, entityId, stationDefName]() {
			if (onOpenCraftingDialog) {
				onOpenCraftingDialog(entityId, stationDefName);
			}
		}
	});

	return content;
}

} // namespace world_sim
