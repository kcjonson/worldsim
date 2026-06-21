#include "ColonistDetailsModel.h"

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>

#include <theme/Tokens.h>

#include <cmath>

namespace world_sim {

ColonistDetailsModel::UpdateType ColonistDetailsModel::refresh(ecs::World& world, ecs::EntityID colonistId) {
	// Check if colonist changed
	bool colonistChanged = (colonistId != currentColonistId);
	currentColonistId = colonistId;

	// Validate colonist exists
	const auto* colonist = world.getComponent<ecs::Colonist>(colonistId);
	if (colonist == nullptr) {
		valid = false;
		return colonistChanged ? UpdateType::Structure : UpdateType::None;
	}

	valid = true;

	// Extract all data
	extractBioData(world, colonistId);
	extractHealthData(world, colonistId);
	extractSocialData();
	extractGearData(world, colonistId);
	extractMemoryData(world, colonistId);
	extractTasksData(world, colonistId);

	// Detect what changed
	if (colonistChanged) {
		// Save current values for next comparison
		prevNeedValues = healthData.needValues;
		prevMood = healthData.mood;
		prevInventorySize = gearData.items.size();
		prevMemoryCount = memoryData.totalKnown;
		prevTaskCount = tasksData.totalCount;
		prevBioTask = bioData.currentTask;
		return UpdateType::Structure;
	}

	// Check for value changes
	bool valuesChanged = false;

	// Check needs
	for (size_t i = 0; i < 8; ++i) {
		if (std::abs(healthData.needValues[i] - prevNeedValues[i]) > 0.1F) {
			valuesChanged = true;
			break;
		}
	}

	// Check mood
	if (std::abs(healthData.mood - prevMood) > 0.5F) {
		valuesChanged = true;
	}

	// Check inventory size
	if (gearData.items.size() != prevInventorySize) {
		valuesChanged = true;
	}

	// Check memory count
	if (memoryData.totalKnown != prevMemoryCount) {
		valuesChanged = true;
	}

	// Check task count
	if (tasksData.totalCount != prevTaskCount) {
		valuesChanged = true;
	}

	// Check the rendered bio task line (driven by task type/state/navState + labels).
	// extractBioData ran above, so bioData.currentTask reflects this frame's task.
	if (bioData.currentTask != prevBioTask) {
		valuesChanged = true;
	}

	// Update previous values
	prevNeedValues = healthData.needValues;
	prevMood = healthData.mood;
	prevInventorySize = gearData.items.size();
	prevMemoryCount = memoryData.totalKnown;
	prevTaskCount = tasksData.totalCount;
	prevBioTask = bioData.currentTask;

	return valuesChanged ? UpdateType::Values : UpdateType::None;
}

void ColonistDetailsModel::extractBioData(const ecs::World& world, ecs::EntityID colonistId) {
	const auto* colonist = world.getComponent<ecs::Colonist>(colonistId);
	if (colonist != nullptr) {
		bioData.name = colonist->name;
	} else {
		bioData.name = "Unknown";
	}

	// Placeholder data
	bioData.age = "--";
	bioData.traits.clear();
	bioData.background = "No background";

	// Get mood from needs
	const auto* needs = world.getComponent<ecs::NeedsComponent>(colonistId);
	if (needs != nullptr) {
		bioData.mood = ecs::computeMood(*needs);
		bioData.moodLabel = getMoodLabel(bioData.mood);
	} else {
		bioData.mood = 100.0F;
		bioData.moodLabel = "Unknown";
	}

	// Get current task
	const auto* task = world.getComponent<ecs::Task>(colonistId);
	if (task != nullptr && task->isActive()) {
		// For moving tasks, prefer the nav-state vocabulary so the player understands
		// belief-driven navigation ("Re-routing", "Can't find a way") as intent, not bug.
		if (task->state == ecs::TaskState::Moving) {
			switch (task->navState) {
				case ecs::NavState::Rerouting:
					bioData.currentTask = "Re-routing";
					bioData.currentTaskColor = UI::text;
					break;
				case ecs::NavState::CantFindWayTo:
					bioData.currentTask = "Can't find a way to target";
					bioData.currentTaskColor = UI::status_crit;
					break;
				case ecs::NavState::SearchingLKP:
					bioData.currentTask = "Searching for target";
					bioData.currentTaskColor = UI::status_warn;
					break;
				case ecs::NavState::LookingForWayIn:
					bioData.currentTask = "Looking for a way in";
					bioData.currentTaskColor = UI::status_warn;
					break;
				case ecs::NavState::Traveling:
				default: {
					// Build a destination label from the task type
					std::string dest;
					switch (task->type) {
						case ecs::TaskType::FulfillNeed:
							dest = (task->needToFulfill != ecs::NeedType::Count)
								? ecs::needLabel(task->needToFulfill)
								: "need";
							break;
						case ecs::TaskType::Gather:
							dest = task->gatherItemDefName.empty() ? "item" : task->gatherItemDefName;
							break;
						case ecs::TaskType::Craft:
							dest = "crafting station";
							break;
						case ecs::TaskType::Haul:
							dest = task->haulItemDefName.empty() ? "storage" : task->haulItemDefName;
							break;
						case ecs::TaskType::Build:
							dest = "build site";
							break;
						case ecs::TaskType::Deconstruct:
							dest = "structure";
							break;
						case ecs::TaskType::PlacePackaged:
							dest = "placement";
							break;
						case ecs::TaskType::Harvest:
							dest = "resource";
							break;
						default:
							dest = "target";
							break;
					}
					bioData.currentTask = "Going to " + dest;
					bioData.currentTaskColor = UI::status_ok;
					break;
				}
			}
		} else {
			// Not moving (Pending/Arrived or non-nav tasks)
			switch (task->type) {
				case ecs::TaskType::FulfillNeed:
					bioData.currentTask = (task->needToFulfill != ecs::NeedType::Count)
						? std::string("Fulfilling ") + ecs::needLabel(task->needToFulfill)
						: "Fulfilling need";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Gather:
					bioData.currentTask = "Gathering " + task->gatherItemDefName;
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Craft:
					bioData.currentTask = "Crafting " + task->craftRecipeDefName;
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Haul:
					bioData.currentTask = "Hauling " + (task->haulItemDefName.empty() ? "item" : task->haulItemDefName);
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Build:
					bioData.currentTask = "Building";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Deconstruct:
					bioData.currentTask = "Deconstructing";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::PlacePackaged:
					bioData.currentTask = "Placing item";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Harvest:
					bioData.currentTask = "Harvesting";
					bioData.currentTaskColor = UI::status_ok;
					break;
				case ecs::TaskType::Wander:
					bioData.currentTask = "Wandering";
					bioData.currentTaskColor = UI::text_dim;
					break;
				default:
					bioData.currentTask = "Idle";
					bioData.currentTaskColor = UI::text_dim;
					break;
			}
		}
	} else {
		bioData.currentTask = "Idle";
		bioData.currentTaskColor = UI::text_dim;
	}
}

void ColonistDetailsModel::extractHealthData(const ecs::World& world, ecs::EntityID colonistId) {
	const auto* needs = world.getComponent<ecs::NeedsComponent>(colonistId);
	if (needs != nullptr) {
		for (size_t i = 0; i < 8; ++i) {
			const auto& need = needs->needs[i];
			healthData.needValues[i] = need.value;
			healthData.needsAttention[i] = need.needsAttention();
			healthData.isCritical[i] = need.isCritical();
		}
		healthData.mood = ecs::computeMood(*needs);
		healthData.moodLabel = getMoodLabel(healthData.mood);
	} else {
		healthData.needValues.fill(100.0F);
		healthData.needsAttention.fill(false);
		healthData.isCritical.fill(false);
		healthData.mood = 100.0F;
		healthData.moodLabel = "Unknown";
	}
}

void ColonistDetailsModel::extractSocialData() {
	// Placeholder - no social system yet
	socialData.placeholder = "Relationships not yet tracked";
}

void ColonistDetailsModel::extractGearData(const ecs::World& world, ecs::EntityID colonistId) {
	const auto* inventory = world.getComponent<ecs::Inventory>(colonistId);
	if (inventory != nullptr) {
		// Hand items
		gearData.leftHand = inventory->leftHand;
		gearData.rightHand = inventory->rightHand;

		// Backpack items
		gearData.items = inventory->getAllItems();
		gearData.slotCount = inventory->getSlotCount();
		gearData.maxSlots = inventory->maxCapacity;
	} else {
		gearData.leftHand.reset();
		gearData.rightHand.reset();
		gearData.items.clear();
		gearData.slotCount = 0;
		gearData.maxSlots = 0;
	}
}

void ColonistDetailsModel::extractMemoryData(const ecs::World& world, ecs::EntityID colonistId) {
	memoryData.categories.clear();
	memoryData.totalKnown = 0;

	const auto* memory = world.getComponent<ecs::Memory>(colonistId);
	if (memory == nullptr) {
		memoryData.sightRadius = ecs::kDefaultSightRadius;
		return;
	}

	memoryData.sightRadius = memory->sightRadius;

	auto& assetRegistry = engine::assets::AssetRegistry::Get();

	// Helper to build category from capability
	auto buildCategory = [&](const std::string& categoryName, engine::assets::CapabilityType capability) {
		MemoryCategory category;
		category.name = categoryName;

		const auto& entityKeys = memory->getEntitiesWithCapability(capability);
		category.count = entityKeys.size();

		// Limit displayed entities to avoid performance issues
		constexpr size_t kMaxDisplayedEntities = 100;
		size_t			 displayed = 0;

		for (uint64_t key : entityKeys) {
			if (displayed >= kMaxDisplayedEntities) {
				break;
			}

			const auto* entity = memory->getWorldEntity(key);
			if (entity != nullptr) {
				MemoryEntity memEntity;
				memEntity.name = assetRegistry.getDefName(entity->defNameId);
				memEntity.x = entity->position.x;
				memEntity.y = entity->position.y;
				category.entities.push_back(memEntity);
				++displayed;
			}
		}

		return category;
	};

	// Build categories
	memoryData.categories.push_back(buildCategory("Food Sources", engine::assets::CapabilityType::Edible));
	memoryData.categories.push_back(buildCategory("Water Sources", engine::assets::CapabilityType::Drinkable));
	memoryData.categories.push_back(buildCategory("Resources", engine::assets::CapabilityType::Harvestable));

	// Threats category - placeholder (no threat system yet)
	MemoryCategory threats;
	threats.name = "Threats";
	threats.count = 0;
	memoryData.categories.push_back(threats);

	// Known colonists/dynamic entities
	MemoryCategory colonists;
	colonists.name = "Other Colonists";
	colonists.count = memory->knownDynamicEntities.size();
	for (const auto& [entityId, knownEntity] : memory->knownDynamicEntities) {
		// Try to get colonist name
		const auto*	 colonist = world.getComponent<ecs::Colonist>(entityId);
		MemoryEntity memEntity;
		if (colonist != nullptr) {
			memEntity.name = colonist->name;
		} else {
			memEntity.name = "Unknown Entity";
		}
		memEntity.x = knownEntity.lastKnownPosition.x;
		memEntity.y = knownEntity.lastKnownPosition.y;
		colonists.entities.push_back(memEntity);
	}
	memoryData.categories.push_back(colonists);

	// Calculate total from category counts (not totalKnown() which includes entities with no capabilities)
	memoryData.totalKnown = 0;
	for (const auto& category : memoryData.categories) {
		memoryData.totalKnown += category.count;
	}
}

std::string ColonistDetailsModel::getMoodLabel(float mood) {
	if (mood >= 80.0F) {
		return "Happy";
	}
	if (mood >= 60.0F) {
		return "Content";
	}
	if (mood >= 40.0F) {
		return "Neutral";
	}
	if (mood >= 20.0F) {
		return "Stressed";
	}
	return "Miserable";
}

void ColonistDetailsModel::extractTasksData(ecs::World& world, ecs::EntityID colonistId) {
	// Get colonist position for distance calculations
	glm::vec2	colonistPosition{0.0F, 0.0F};
	const auto* position = world.getComponent<ecs::Position>(colonistId);
	if (position != nullptr) {
		colonistPosition = position->value;
	}

	// Get tasks known by this colonist via the adapter.
	tasksData.tasks = adapters::getTasksForColonist(colonistPosition);
	adapters::sortTasksForDisplay(tasksData.tasks);
	tasksData.totalCount = tasksData.tasks.size();

	// The "Currently" panel mirrors the bio task line (extractBioData ran first).
	tasksData.currentTask = bioData.currentTask;
}

} // namespace world_sim
