#include "CraftingAdapter.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/WorkQueue.h>

#include <sstream>
#include <utility>

namespace world_sim {

namespace {
	// Visual spacing
	constexpr float kSectionSpacing = 8.0F;

	// Display label for a material defName (asset label, else the raw defName).
	std::string materialLabel(const std::string& defName) {
		const auto* def = engine::assets::AssetRegistry::Get().getDefinition(defName);
		if (def != nullptr && !def->label.empty()) {
			return def->label;
		}
		return defName;
	}
} // namespace

bool isMaterialObtainable(ecs::World& world, const std::string& itemDefName) {
	auto&		   registry = engine::assets::AssetRegistry::Get();
	const uint32_t targetId = registry.getDefNameId(itemDefName);
	if (targetId == 0) {
		return false;
	}

	// Already-held stock counts: any storage or colonist inventory carrying the item.
	for (auto [entity, inventory] : world.view<ecs::Inventory>()) {
		(void)entity;
		if (inventory.getQuantity(itemDefName) > 0) {
			return true;
		}
	}

	// Otherwise a colonist must know where to get it: a discovered loose carryable of that type,
	// or a discovered harvestable that yields it. Knowledge is per-colonist, so the union across
	// all colonists' Memory is what the colony "knows".
	for (auto [entity, memory] : world.view<ecs::Memory>()) {
		(void)entity;
		for (const auto& [key, known] : memory.knownWorldEntities) {
			if (known.defNameId == targetId &&
				registry.hasCapability(known.defNameId, engine::assets::CapabilityType::Carryable)) {
				return true;
			}
			if (registry.hasCapability(known.defNameId, engine::assets::CapabilityType::Harvestable)) {
				const auto& defName = registry.getDefName(known.defNameId);
				const auto* def = registry.getDefinition(defName);
				if (def != nullptr && def->capabilities.harvestable.has_value() &&
					def->capabilities.harvestable->yieldDefName == itemDefName) {
					return true;
				}
			}
		}
	}

	return false;
}

std::vector<std::string> unobtainableInputs(ecs::World& world, const engine::assets::RecipeDef& recipe) {
	std::vector<std::string> missing;
	for (const auto& input : recipe.inputs) {
		if (!isMaterialObtainable(world, input.defName)) {
			missing.push_back(input.defName);
		}
	}
	return missing;
}

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

PanelContent adaptCraftingStatus(ecs::World& world, ecs::EntityID entityId, const std::string& stationDefName) {
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

	// Warn when the current job can't be worked because its materials have no known source.
	// Without this the order silently sits at 0/N while colonists wander, since they only act
	// on materials they've discovered.
	if (const auto* currentJob = workQueue->getNextJob(); currentJob != nullptr) {
		const auto* recipe = engine::assets::RecipeRegistry::Get().getRecipe(currentJob->recipeDefName);
		if (recipe != nullptr) {
			const auto missing = unobtainableInputs(world, *recipe);
			if (!missing.empty()) {
				std::vector<std::string> labels;
				labels.reserve(missing.size());
				for (const auto& defName : missing) {
					labels.push_back(materialLabel(defName));
				}
				content.slots.push_back(SpacerSlot{.height = kSectionSpacing});
				content.slots.push_back(
					TextSlot{
						.label = "Blocked",
						.value = "No materials found nearby",
					}
				);
				content.slots.push_back(
					TextListSlot{
						.header = "No known source for:",
						.items = std::move(labels),
					}
				);
			}
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
