#pragma once

// CraftingAdapter - Transforms crafting station data into panel content
//
// Provides two views for crafting stations:
// - Status: Current work queue, progress, station info
// - Recipes: Available recipes with clickable slots to queue work
//
// Follows the same adapter pattern as SelectionAdapter.

#include "scenes/game/ui/components/InfoSlot.h"
#include "scenes/game/ui/components/Selection.h"

#include <assets/RecipeRegistry.h>
#include <ecs/World.h>

#include <functional>

namespace world_sim {

/// Callback type for queuing a recipe at a station
using QueueRecipeCallback = std::function<void(const std::string& recipeDefName)>;

/// Adapt crafting station status to panel content
/// Shows: station name, current job, progress, pending work count
PanelContent adaptCraftingStatus(
	const ecs::World& world,
	ecs::EntityID entityId,
	const std::string& stationDefName
);

/// Adapt available recipes to panel content
/// Shows: list of recipes with clickable slots to queue work
PanelContent adaptCraftingRecipes(
	const std::string& stationDefName,
	const engine::assets::RecipeRegistry& registry,
	QueueRecipeCallback onQueueRecipe
);

/// Format a recipe for display (label with input summary)
std::string formatRecipeLabel(const engine::assets::RecipeDef& recipe);

} // namespace world_sim
