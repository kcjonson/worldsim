#include "EntityInfoModel.h"

#include "scenes/game/ui/adapters/SelectionAdapter.h"

namespace world_sim {

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
			if constexpr (std::is_same_v<T, NoSelection>) {
				type = Type::None;
				colonistId = ecs::EntityID{0};
				stationId = ecs::EntityID{0};
				furnitureId = ecs::EntityID{0};
				worldEntityDef.clear();
				stationDefName.clear();
				furnitureDefName.clear();
				worldEntityPos = {};
				furniturePackaged = false;
			} else if constexpr (std::is_same_v<T, ColonistSelection>) {
				type = Type::Colonist;
				colonistId = sel.entityId;
				stationId = ecs::EntityID{0};
				furnitureId = ecs::EntityID{0};
				worldEntityDef.clear();
				stationDefName.clear();
				furnitureDefName.clear();
				worldEntityPos = {};
				furniturePackaged = false;
			} else if constexpr (std::is_same_v<T, WorldEntitySelection>) {
				type = Type::WorldEntity;
				colonistId = ecs::EntityID{0};
				stationId = ecs::EntityID{0};
				furnitureId = ecs::EntityID{0};
				worldEntityDef = sel.defName;
				stationDefName.clear();
				furnitureDefName.clear();
				worldEntityPos = sel.position;
				furniturePackaged = false;
			} else if constexpr (std::is_same_v<T, CraftingStationSelection>) {
				type = Type::CraftingStation;
				colonistId = ecs::EntityID{0};
				stationId = sel.entityId;
				furnitureId = ecs::EntityID{0};
				worldEntityDef.clear();
				stationDefName = sel.defName;
				furnitureDefName.clear();
				worldEntityPos = {};
				furniturePackaged = false;
			} else if constexpr (std::is_same_v<T, FurnitureSelection>) {
				type = Type::Furniture;
				colonistId = ecs::EntityID{0};
				stationId = ecs::EntityID{0};
				furnitureId = sel.entityId;
				worldEntityDef.clear();
				stationDefName.clear();
				furnitureDefName = sel.defName;
				worldEntityPos = {};
				furniturePackaged = sel.isPackaged;
			}
		},
		selection
	);
}

// ============================================================================
// EntityInfoModel implementation
// ============================================================================

EntityInfoModel::UpdateType EntityInfoModel::refresh(
	const Selection& selection,
	const ecs::World& world,
	const engine::assets::AssetRegistry& assetRegistry,
	const engine::assets::RecipeRegistry& recipeRegistry,
	const Callbacks& callbacks
) {
	// Detect selection types
	bool		  isColonist = std::holds_alternative<ColonistSelection>(selection);
	bool		  isStation = std::holds_alternative<CraftingStationSelection>(selection);
	bool		  isFurniture = std::holds_alternative<FurnitureSelection>(selection);
	ecs::EntityID colonistId{0};
	ecs::EntityID stationId{0};
	std::string   stationDefName;

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

	// Handle NoSelection -> hide panel
	if (std::holds_alternative<NoSelection>(selection)) {
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

	// Generate content
	if (isColonist) {
		contentData = getColonistContent(world, colonistId, callbacks.onDetails);
	} else if (isStation) {
		contentData = getCraftingStationContent(world, stationId, stationDefName, recipeRegistry, callbacks.onQueueRecipe);
	} else if (isFurniture) {
		const auto& furnitureSel = std::get<FurnitureSelection>(selection);
		contentData = adaptFurniture(assetRegistry, furnitureSel, callbacks.onPlace);
	} else {
		// World entity - use standard adapter with resource query callback
		auto worldContent = adaptSelection(selection, world, assetRegistry, callbacks.queryResources);
		if (worldContent.has_value()) {
			contentData = std::move(worldContent.value());
		}
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

PanelContent EntityInfoModel::getColonistContent(
	const ecs::World& world,
	ecs::EntityID entityId,
	const std::function<void()>& onDetails
) const {
	// Generate two-column colonist content with onDetails callback
	return adaptColonistStatus(world, entityId, onDetails);
}

PanelContent EntityInfoModel::getCraftingStationContent(
	const ecs::World& world,
	ecs::EntityID entityId,
	const std::string& stationDefName,
	const engine::assets::RecipeRegistry& recipeRegistry,
	const QueueRecipeCallback& onQueueRecipe
) const {
	// Get base status content
	PanelContent content = adaptCraftingStatus(world, entityId, stationDefName);

	// Add recipes
	auto recipes = recipeRegistry.getRecipesForStation(stationDefName);
	if (!recipes.empty()) {
		content.slots.push_back(SpacerSlot{.height = 8.0F});
		for (const auto* recipe : recipes) {
			if (recipe == nullptr) {
				continue;
			}
			// Format recipe name (use label if available)
			std::string recipeName = recipe->label.empty() ? recipe->defName : recipe->label;

			// Format ingredients list
			std::string ingredients;
			if (recipe->inputs.empty()) {
				ingredients = "No materials required";
			} else {
				bool first = true;
				for (const auto& input : recipe->inputs) {
					if (!first) {
						ingredients += ", ";
					}
					ingredients += std::to_string(input.count) + "x " + input.defName;
					first = false;
				}
			}

			std::string recipeDefName = recipe->defName;
			content.slots.push_back(RecipeSlot{
				.name = recipeName,
				.ingredients = ingredients,
				.onQueue = [onQueueRecipe, recipeDefName]() {
					if (onQueueRecipe) {
						onQueueRecipe(recipeDefName);
					}
				},
			});
		}
	}

	return content;
}

} // namespace world_sim
