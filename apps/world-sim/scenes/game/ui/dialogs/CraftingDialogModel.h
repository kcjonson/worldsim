#pragma once

// CraftingDialogModel - ViewModel for CraftingDialog
//
// Encapsulates:
// - Recipe list for the station
// - Selected recipe details
// - Current queue state
//
// Supports per-frame refresh with change detection for live queue updates.

#include <assets/RecipeDef.h>
#include <assets/RecipeRegistry.h>
#include <ecs/EntityID.h>
#include <ecs/World.h>

#include <string>
#include <vector>

namespace world_sim {

/// A recipe in the list (left column)
struct RecipeListItem {
	std::string defName;  ///< Recipe defName for selection
	std::string label;    ///< Display name
	bool canCraft;        ///< Has all materials (false = warning indicator)
};

/// A job in the queue (right column)
struct QueuedJobItem {
	std::string recipeDefName;  ///< For cancel action
	std::string label;          ///< Display name
	uint32_t quantity;          ///< Total to craft
	uint32_t completed;         ///< Already done
	bool isInProgress;          ///< True if this is the current job
	float progress;             ///< 0-1 progress on current item
};

/// Material requirement with availability (center column)
struct MaterialRequirement {
	std::string defName;   ///< Material defName
	std::string label;     ///< Display name
	uint32_t required;     ///< How many needed
	uint32_t available;    ///< How many player has (0 = unknown for now)
	bool hasEnough;        ///< available >= required
};

/// Output item from recipe (center column)
struct RecipeOutputItem {
	std::string label;  ///< Display name
	uint32_t count;     ///< Quantity produced
};

/// Full details for selected recipe (center column)
struct SelectedRecipeDetails {
	std::string name;         ///< Recipe label
	std::string description;  ///< Recipe description
	std::vector<MaterialRequirement> materials;
	std::vector<RecipeOutputItem> outputs;
	float workTime;           ///< Approximate seconds to craft
	bool canCraft;            ///< Has all materials
};

/// ViewModel for CraftingDialog
class CraftingDialogModel {
  public:
	/// Type of update needed after refresh()
	enum class UpdateType {
		None,       ///< No change
		Queue,      ///< Queue values changed (progress update)
		Selection,  ///< Selected recipe changed
		Full,       ///< Station changed or initial load
	};

	/// Set the station to display recipes for
	/// @param stationId ECS entity ID of the crafting station
	/// @param stationDefName Asset defName (e.g., "CraftingSpot")
	void setStation(ecs::EntityID stationId, const std::string& stationDefName);

	/// Clear selection (when dialog closes)
	void clear();

	/// Refresh model from ECS world
	/// @return Type of update the dialog should perform
	[[nodiscard]] UpdateType refresh(
		const ecs::World& world,
		const engine::assets::RecipeRegistry& registry
	);

	/// Select a recipe by defName
	void selectRecipe(const std::string& defName);

	/// Set quantity to queue
	void setQuantity(uint32_t qty);

	/// Increment/decrement quantity
	void adjustQuantity(int delta);

	// --- Getters ---

	[[nodiscard]] bool isValid() const { return valid; }
	[[nodiscard]] const std::string& stationName() const { return stationLabel; }
	[[nodiscard]] ecs::EntityID stationId() const { return currentStationId; }
	[[nodiscard]] const std::string& stationDefName() const { return currentStationDefName; }

	[[nodiscard]] const std::vector<RecipeListItem>& recipes() const { return recipeList; }
	[[nodiscard]] const std::string& selectedRecipeDefName() const { return selectedRecipe; }
	[[nodiscard]] const SelectedRecipeDetails& selectedDetails() const { return details; }
	[[nodiscard]] uint32_t quantity() const { return currentQuantity; }

	[[nodiscard]] const std::vector<QueuedJobItem>& queue() const { return queueItems; }
	[[nodiscard]] bool hasQueuedJobs() const { return !queueItems.empty(); }

  private:
	/// Extract recipe list from registry
	void extractRecipeList(const engine::assets::RecipeRegistry& registry);

	/// Extract selected recipe details
	void extractSelectedDetails(const engine::assets::RecipeRegistry& registry);

	/// Extract queue from WorkQueue component
	void extractQueue(const ecs::World& world, const engine::assets::RecipeRegistry& registry);

	/// Check if materials are available (placeholder - always returns true for now)
	[[nodiscard]] bool checkMaterialAvailability(const engine::assets::RecipeDef& recipe) const;

	// State
	ecs::EntityID currentStationId{0};
	std::string currentStationDefName;
	std::string stationLabel;
	bool valid = false;

	// Recipe selection
	std::string selectedRecipe;
	uint32_t currentQuantity = 1;

	// Cached data
	std::vector<RecipeListItem> recipeList;
	SelectedRecipeDetails details;
	std::vector<QueuedJobItem> queueItems;

	// Change detection
	float prevProgress = 0.0F;
	size_t prevQueueSize = 0;
	uint32_t prevCompletedTotal = 0;
};

} // namespace world_sim
