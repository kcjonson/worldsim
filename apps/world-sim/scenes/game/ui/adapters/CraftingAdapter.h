#pragma once

// CraftingAdapter - Transforms crafting station data into panel content
//
// Provides two views for crafting stations:
// - Status: Current work queue, progress, station info
// - Recipes: Available recipes with clickable slots to queue work
//
// Follows the same adapter pattern as SelectionAdapter.

#include "scenes/game/ui/components/InfoSlot.h"
#include "scenes/game/world/selection/SelectionTypes.h"

#include <assets/RecipeDef.h>
#include <assets/RecipeRegistry.h>
#include <ecs/World.h>

#include <functional>
#include <string>
#include <vector>

namespace world_sim {

/// Callback type for queuing a recipe at a station
using QueueRecipeCallback = std::function<void(const std::string& recipeDefName, uint32_t quantity)>;

/// True if the colony can currently obtain at least one unit of itemDefName. A material counts
/// as obtainable when some colonist's Memory holds a loose carryable of that type, or a known
/// harvestable that yields it, or it already sits in any inventory (storage or colonist). This
/// drives the crafting warnings: a queued recipe whose inputs aren't obtainable will never get
/// worked, because colonists only act on materials they've discovered.
[[nodiscard]] bool isMaterialObtainable(ecs::World& world, const std::string& itemDefName);

/// Recipe input defNames the colony currently has no obtainable source for (empty = all sourced).
[[nodiscard]] std::vector<std::string> unobtainableInputs(ecs::World& world, const engine::assets::RecipeDef& recipe);

/// Adapt crafting station status to panel content
/// Shows: station name, current job, progress, pending work count, and a clear warning when the
/// current job's materials have no known source.
PanelContent adaptCraftingStatus(
	ecs::World& world,
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
