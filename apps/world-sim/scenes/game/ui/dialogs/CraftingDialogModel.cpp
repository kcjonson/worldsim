#include "CraftingDialogModel.h"

#include <ecs/components/WorkQueue.h>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace world_sim {

void CraftingDialogModel::setStation(ecs::EntityID stationId, const std::string& stationDefName) {
	currentStationId = stationId;
	currentStationDefName = stationDefName;

	// Create human-readable label from defName
	// e.g., "CraftingSpot" -> "Crafting Spot"
	// Simple conversion: insert space before capitals (except first)
	std::string result;
	result.reserve(stationDefName.size() + 4); // Room for a few spaces
	for (size_t i = 0; i < stationDefName.size(); ++i) {
		char c = stationDefName[i];
		if (i > 0 && std::isupper(c) != 0) {
			result += ' ';
		}
		result += c;
	}
	stationLabel = std::move(result);

	// Reset selection
	selectedRecipe.clear();
	currentQuantity = 1;
	valid = false;

	// Clear cached data
	recipeList.clear();
	details = {};
	queueItems.clear();

	// Reset change detection
	prevProgress = 0.0F;
	prevQueueSize = 0;
	prevCompletedTotal = 0;
}

void CraftingDialogModel::clear() {
	currentStationId = 0;
	currentStationDefName.clear();
	stationLabel.clear();
	valid = false;
	selectedRecipe.clear();
	currentQuantity = 1;
	recipeList.clear();
	details = {};
	queueItems.clear();
}

CraftingDialogModel::UpdateType CraftingDialogModel::refresh(
	const ecs::World& world,
	const engine::assets::RecipeRegistry& registry
) {
	if (currentStationId == 0) {
		valid = false;
		return UpdateType::None;
	}

	bool wasValid = valid;
	valid = true;

	// Extract all data
	extractRecipeList(registry);
	extractSelectedDetails(registry);
	extractQueue(world, registry);

	// Auto-select first recipe if none selected
	if (selectedRecipe.empty() && !recipeList.empty()) {
		selectedRecipe = recipeList[0].defName;
		extractSelectedDetails(registry);
		return UpdateType::Full;
	}

	// If we just became valid, it's a full update
	if (!wasValid) {
		return UpdateType::Full;
	}

	// Check for queue changes
	const auto* workQueue = world.getComponent<ecs::WorkQueue>(currentStationId);
	if (workQueue != nullptr) {
		// Count total completed across all jobs
		uint32_t completedTotal = 0;
		for (const auto& job : workQueue->jobs) {
			completedTotal += job.completed;
		}

		// Detect changes
		bool queueChanged = (workQueue->jobs.size() != prevQueueSize) ||
		                    (completedTotal != prevCompletedTotal) ||
		                    (std::abs(workQueue->progress - prevProgress) > 0.01F);

		prevQueueSize = workQueue->jobs.size();
		prevCompletedTotal = completedTotal;
		prevProgress = workQueue->progress;

		if (queueChanged) {
			return UpdateType::Queue;
		}
	}

	return UpdateType::None;
}

void CraftingDialogModel::selectRecipe(const std::string& defName) {
	if (defName != selectedRecipe) {
		selectedRecipe = defName;
		currentQuantity = 1;  // Reset quantity on selection change
	}
}

void CraftingDialogModel::setQuantity(uint32_t qty) {
	currentQuantity = std::max(1U, qty);
}

void CraftingDialogModel::adjustQuantity(int delta) {
	int newQty = static_cast<int>(currentQuantity) + delta;
	currentQuantity = static_cast<uint32_t>(std::max(1, newQty));
}

void CraftingDialogModel::extractRecipeList(const engine::assets::RecipeRegistry& registry) {
	recipeList.clear();

	auto recipes = registry.getRecipesForStation(currentStationDefName);
	recipeList.reserve(recipes.size());

	for (const auto* recipe : recipes) {
		RecipeListItem item;
		item.defName = recipe->defName;
		item.label = recipe->label;
		item.canCraft = checkMaterialAvailability(*recipe);
		recipeList.push_back(item);
	}

	// Sort: craftable recipes first, then alphabetically
	std::sort(recipeList.begin(), recipeList.end(), [](const RecipeListItem& a, const RecipeListItem& b) {
		if (a.canCraft != b.canCraft) {
			return a.canCraft;  // true (craftable) comes before false
		}
		return a.label < b.label;
	});
}

void CraftingDialogModel::extractSelectedDetails(const engine::assets::RecipeRegistry& registry) {
	details = {};

	if (selectedRecipe.empty()) {
		return;
	}

	const auto* recipe = registry.getRecipe(selectedRecipe);
	if (recipe == nullptr) {
		return;
	}

	details.name = recipe->label;
	details.description = recipe->description;
	details.canCraft = checkMaterialAvailability(*recipe);

	// Work time in seconds (workAmount / assumed work rate)
	// Assume ~100 work units per second as baseline
	constexpr float kWorkUnitsPerSecond = 100.0F;
	details.workTime = recipe->workAmount / kWorkUnitsPerSecond;

	// Materials
	details.materials.reserve(recipe->inputs.size());
	for (const auto& input : recipe->inputs) {
		MaterialRequirement mat;
		mat.defName = input.defName;
		mat.label = input.defName;  // Could look up display name from asset registry
		mat.required = input.count;
		mat.available = 0;  // TODO: Query inventory when available
		mat.hasEnough = true;  // Placeholder: assume available for now
		details.materials.push_back(mat);
	}

	// Outputs
	details.outputs.reserve(recipe->outputs.size());
	for (const auto& output : recipe->outputs) {
		RecipeOutputItem out;
		out.label = output.defName;  // Could look up display name from asset registry
		out.count = output.count;
		details.outputs.push_back(out);
	}
}

void CraftingDialogModel::extractQueue(const ecs::World& world, const engine::assets::RecipeRegistry& registry) {
	queueItems.clear();

	const auto* workQueue = world.getComponent<ecs::WorkQueue>(currentStationId);
	if (workQueue == nullptr) {
		return;
	}

	bool firstJob = true;
	for (const auto& job : workQueue->jobs) {
		// Skip completed jobs
		if (job.isComplete()) {
			continue;
		}

		QueuedJobItem item;
		item.recipeDefName = job.recipeDefName;

		// Get display name from registry
		const auto* recipe = registry.getRecipe(job.recipeDefName);
		item.label = (recipe != nullptr) ? recipe->label : job.recipeDefName;

		item.quantity = job.quantity;
		item.completed = job.completed;
		item.isInProgress = firstJob;
		item.progress = firstJob ? workQueue->progress : 0.0F;

		queueItems.push_back(item);
		firstJob = false;
	}
}

bool CraftingDialogModel::checkMaterialAvailability(const engine::assets::RecipeDef& recipe) const {
	// TODO: When inventory/stockpile system is available, query actual availability
	// For now, always return true (player can queue anything)
	(void)recipe;
	return true;
}

} // namespace world_sim
