#include "CraftingAdapter.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/StorageConfiguration.h>
#include <ecs/components/WorkQueue.h>
#include <ecs/systems/GoalSystemHelpers.h>

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

std::unordered_set<uint32_t> collectMemorySources(ecs::World& world) {
	auto&						 registry = engine::assets::AssetRegistry::Get();
	std::unordered_set<uint32_t> obtainable;

	// Known sources in any colonist's Memory: a discovered loose carryable of a type, or a
	// discovered harvestable whose yield is that type. Iterate the per-capability index sets
	// (small) rather than every remembered entity (up to kMaxWorldEntities each) - the union
	// across all colonists is what the colony "knows".
	for (auto [entity, memory] : world.view<ecs::Memory>()) {
		(void)entity;
		for (uint64_t key : memory.getEntitiesWithCapability(engine::assets::CapabilityType::Carryable)) {
			auto it = memory.knownWorldEntities.find(key);
			if (it != memory.knownWorldEntities.end()) {
				obtainable.insert(it->second.defNameId);
			}
		}
		for (uint64_t key : memory.getEntitiesWithCapability(engine::assets::CapabilityType::Harvestable)) {
			auto it = memory.knownWorldEntities.find(key);
			if (it == memory.knownWorldEntities.end()) {
				continue;
			}
			const auto& defName = registry.getDefName(it->second.defNameId);
			const auto* def = registry.getDefinition(defName);
			if (def != nullptr && def->capabilities.harvestable.has_value()) {
				const uint32_t yieldId = registry.getDefNameId(def->capabilities.harvestable->yieldDefName);
				if (yieldId != 0) {
					obtainable.insert(yieldId);
				}
			}
		}
	}

	return obtainable;
}

std::unordered_set<uint32_t> collectInventorySources(
	ecs::World& world, const std::function<bool(ecs::EntityID, const ecs::Inventory&)>& accept
) {
	auto&						 registry = engine::assets::AssetRegistry::Get();
	std::unordered_set<uint32_t> obtainable;

	// Already-held stock: items in any inventory (storage or colonist backpack), plus items
	// actively held in hand (unless the hands carry a packaged entity, which is furniture in
	// transit, not a raw material). An inventory only contributes when `accept` admits it; the
	// always-true predicate reproduces the unconditional scan crafting relies on.
	for (auto [entity, inventory] : world.view<ecs::Inventory>()) {
		if (!accept(entity, inventory)) {
			continue;
		}
		for (const auto& stack : inventory.items) {
			if (stack.quantity == 0) {
				continue;
			}
			const uint32_t id = registry.getDefNameId(stack.defName);
			if (id != 0) {
				obtainable.insert(id);
			}
		}
		if (!inventory.carryingPackagedEntity.has_value()) {
			for (const ecs::ItemStack* hand : {inventory.getLeftHand(), inventory.getRightHand()}) {
				if (hand != nullptr && hand->quantity > 0) {
					const uint32_t id = registry.getDefNameId(hand->defName);
					if (id != 0) {
						obtainable.insert(id);
					}
				}
			}
		}
	}

	return obtainable;
}

std::unordered_set<uint32_t> collectObtainableMaterials(ecs::World& world) {
	std::unordered_set<uint32_t> obtainable = collectMemorySources(world);
	const auto inventory = collectInventorySources(world, [](ecs::EntityID, const ecs::Inventory&) { return true; });
	obtainable.insert(inventory.begin(), inventory.end());
	return obtainable;
}

bool isStorageSourceKnown(ecs::World& world, ecs::EntityID destBox, const std::string& itemDefName) {
	auto&		   registry = engine::assets::AssetRegistry::Get();
	const uint32_t itemId = registry.getDefNameId(itemDefName);
	if (itemId == 0) {
		return false;
	}

	// A loose pile or a known harvestable's yield satisfies the pull straight from Memory.
	if (collectMemorySources(world).count(itemId) > 0) {
		return true;
	}

	// Otherwise the engine pull only sources from a STRICTLY-lower-priority box the colony KNOWS.
	// Mirror evaluateHaulOptions' source gate exactly: same item, strict-< getPriorityFor on the
	// item's category, colonyKnowsStorageEntity. getPriorityFor falls back to Low for an
	// unconfigured box, so an unknown-priority box holding the item is a valid lower source.
	const auto* itemDef = registry.getDefinition(itemDefName);
	if (itemDef == nullptr) {
		return false;
	}
	const engine::assets::ItemCategory category = itemDef->category;

	const auto* destConfig = world.getComponent<ecs::StorageConfiguration>(destBox);
	if (destConfig == nullptr) {
		return false;
	}
	const ecs::StoragePriority destPriority = destConfig->getPriorityFor(itemDefName, category);

	const auto known = collectInventorySources(
		world,
		[&](ecs::EntityID source, const ecs::Inventory& inv) {
			if (source == destBox) {
				return false;
			}
			if (inv.getQuantity(itemDefName) == 0) {
				return false;
			}
			const auto* srcConfig = world.getComponent<ecs::StorageConfiguration>(source);
			if (srcConfig == nullptr) {
				return false;
			}
			const ecs::StoragePriority srcPriority = srcConfig->getPriorityFor(itemDefName, category);
			if (!(static_cast<uint8_t>(srcPriority) < static_cast<uint8_t>(destPriority))) {
				return false;
			}
			return ecs::colonyKnowsStorageEntity(&world, registry, source);
		}
	);
	return known.count(itemId) > 0;
}

bool isMaterialObtainable(const std::unordered_set<uint32_t>& obtainable, const std::string& itemDefName) {
	const uint32_t id = engine::assets::AssetRegistry::Get().getDefNameId(itemDefName);
	return id != 0 && obtainable.count(id) > 0;
}

std::vector<std::string> unobtainableInputs(
	const std::unordered_set<uint32_t>& obtainable, const engine::assets::RecipeDef& recipe
) {
	std::vector<std::string> missing;
	for (const auto& input : recipe.inputs) {
		if (!isMaterialObtainable(obtainable, input.defName)) {
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
			const auto missing = unobtainableInputs(collectObtainableMaterials(world), *recipe);
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
