#include "CraftingAdapter.h"

#include <ecs/components/WorkQueue.h>

#include <sstream>

namespace world_sim {

namespace {
	// Visual spacing
	constexpr float kSectionSpacing = 8.0F;
} // namespace

std::string formatRecipeLabel(const engine::assets::RecipeDef& recipe) {
	// Use label if available, otherwise defName
	std::string name = recipe.label.empty() ? recipe.defName : recipe.label;

	// Add input summary if recipe has inputs
	if (!recipe.inputs.empty()) {
		std::ostringstream stream;
		stream << name << " (";
		bool first = true;
		for (const auto& input : recipe.inputs) {
			if (!first) {
				stream << ", ";
			}
			stream << input.count << "x " << input.defName;
			first = false;
		}
		stream << ")";
		return stream.str();
	}

	return name;
}

PanelContent adaptCraftingStatus(const ecs::World& world, ecs::EntityID entityId, const std::string& stationDefName) {
	PanelContent content;
	content.title = stationDefName;

	// Get work queue
	const auto* workQueue = world.getComponent<ecs::WorkQueue>(entityId);
	if (workQueue == nullptr) {
		content.slots.push_back(
			TextSlot{
				.label = "Status",
				.value = "No work queue",
			}
		);
		return content;
	}

	// Show status based on queue state
	if (!workQueue->hasPendingWork()) {
		content.slots.push_back(
			TextSlot{
				.label = "Status",
				.value = "Idle",
			}
		);
	} else {
		// Show current job
		const auto* currentJob = workQueue->getNextJob();
		if (currentJob != nullptr) {
			// Show what's being crafted
			std::ostringstream jobStream;
			jobStream << currentJob->recipeDefName;
			jobStream << " (" << currentJob->completed << "/" << currentJob->quantity << ")";
			content.slots.push_back(
				TextSlot{
					.label = "Crafting",
					.value = jobStream.str(),
				}
			);

			// Show progress bar for current item
			content.slots.push_back(
				ProgressBarSlot{
					.label = "Progress",
					.value = workQueue->progress,
				}
			);
		}

		// Show total pending if more than current job
		uint32_t totalPending = workQueue->totalPending();
		if (totalPending > 1 || (currentJob != nullptr && workQueue->jobs.size() > 1)) {
			std::ostringstream queueStream;
			queueStream << totalPending << " items in queue";
			content.slots.push_back(
				TextSlot{
					.label = "Queue",
					.value = queueStream.str(),
				}
			);
		}
	}

	// Show all queued jobs as a list
	if (!workQueue->jobs.empty()) {
		content.slots.push_back(SpacerSlot{.height = kSectionSpacing});

		std::vector<std::string> jobStrings;
		jobStrings.reserve(workQueue->jobs.size());
		for (const auto& job : workQueue->jobs) {
			std::ostringstream itemStream;
			itemStream << job.recipeDefName << " x" << job.remaining();
			if (job.completed > 0) {
				itemStream << " (" << job.completed << " done)";
			}
			jobStrings.push_back(itemStream.str());
		}

		content.slots.push_back(
			TextListSlot{
				.header = "Work Orders",
				.items = std::move(jobStrings),
			}
		);
	}

	return content;
}

PanelContent adaptCraftingRecipes(
	const std::string&					  stationDefName,
	const engine::assets::RecipeRegistry& registry,
	QueueRecipeCallback					  onQueueRecipe
) {
	PanelContent content;
	content.title = "Recipes";

	// Get recipes available at this station
	auto recipes = registry.getRecipesForStation(stationDefName);

	if (recipes.empty()) {
		content.slots.push_back(
			TextSlot{
				.label = "Available",
				.value = "No recipes",
			}
		);
		return content;
	}

	// Add each recipe as a clickable slot
	for (const auto* recipe : recipes) {
		if (recipe == nullptr) {
			continue;
		}

		// Capture recipe defName for the callback
		std::string recipeDefName = recipe->defName;

		content.slots.push_back(
			ClickableTextSlot{
				.label = formatRecipeLabel(*recipe),
				.value = "> Queue",
				.onClick = [onQueueRecipe, recipeDefName]() {
					if (onQueueRecipe) {
						onQueueRecipe(recipeDefName, 1);
					}
				},
			}
		);
	}

	return content;
}

} // namespace world_sim
