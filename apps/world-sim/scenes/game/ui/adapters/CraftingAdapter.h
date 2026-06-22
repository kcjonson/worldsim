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

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace world_sim {

/// Callback type for queuing a recipe at a station
using QueueRecipeCallback = std::function<void(const std::string& recipeDefName, uint32_t quantity)>;

/// Collect, by defNameId, every material the colony can currently source: items in any inventory
/// (storage or colonist), known loose carryables, and the yields of known harvestables. Knowledge
/// is per-colonist, so this unions across all colonists' Memory. Build this ONCE per UI refresh
/// (one pass over inventories + memories, which can be large) and reuse it for every availability
/// check, rather than re-scanning the world per recipe input.
[[nodiscard]] std::unordered_set<uint32_t> collectObtainableMaterials(ecs::World& world);

/// True if itemDefName is in a set produced by collectObtainableMaterials. A queued recipe whose
/// inputs aren't obtainable will never get worked, because colonists only act on materials they've
/// discovered - this drives the crafting "missing materials" warnings.
[[nodiscard]] bool isMaterialObtainable(const std::unordered_set<uint32_t>& obtainable, const std::string& itemDefName);

/// Recipe input defNames not present in the obtainable set (empty = all sourced).
[[nodiscard]] std::vector<std::string> unobtainableInputs(
	const std::unordered_set<uint32_t>& obtainable, const engine::assets::RecipeDef& recipe
);

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
