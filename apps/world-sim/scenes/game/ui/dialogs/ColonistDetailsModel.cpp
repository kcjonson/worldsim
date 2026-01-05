#include "ColonistDetailsModel.h"

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <ecs/GlobalTaskRegistry.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Mood.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Transform.h>
#include <ecs/components/Task.h>

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

	// Update previous values
	prevNeedValues = healthData.needValues;
	prevMood = healthData.mood;
	prevInventorySize = gearData.items.size();
	prevMemoryCount = memoryData.totalKnown;
	prevTaskCount = tasksData.totalCount;

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
		switch (task->type) {
		case ecs::TaskType::FulfillNeed:
			if (task->needToFulfill != ecs::NeedType::Count) {
				bioData.currentTask = std::string("Fulfilling ") + ecs::needLabel(task->needToFulfill);
			} else {
				bioData.currentTask = "Fulfilling need";
			}
			break;
		case ecs::TaskType::Gather:
			bioData.currentTask = "Gathering " + task->gatherItemDefName;
			break;
		case ecs::TaskType::Craft:
			bioData.currentTask = "Crafting " + task->craftRecipeDefName;
			break;
		case ecs::TaskType::Wander:
			bioData.currentTask = "Wandering";
			break;
		default:
			bioData.currentTask = "Idle";
			break;
		}
	} else {
		bioData.currentTask = "Idle";
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
		return;
	}

	auto& assetRegistry = engine::assets::AssetRegistry::Get();

	// Helper to build category from capability
	auto buildCategory = [&](const std::string& categoryName, engine::assets::CapabilityType capability) {
		MemoryCategory category;
		category.name = categoryName;

		const auto& entityKeys = memory->getEntitiesWithCapability(capability);
		category.count = entityKeys.size();

		// Limit displayed entities to avoid performance issues
		constexpr size_t kMaxDisplayedEntities = 100;
		size_t displayed = 0;

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
		const auto* colonist = world.getComponent<ecs::Colonist>(entityId);
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
	tasksData.tasks.clear();
	tasksData.totalCount = 0;

	// Get colonist position for distance calculations
	glm::vec2 colonistPosition{0.0F, 0.0F};
	const auto* position = world.getComponent<ecs::Position>(colonistId);
	if (position != nullptr) {
		colonistPosition = position->value;
	}

	// Get tasks known by this colonist via the adapter
	auto displayData = adapters::getTasksForColonist(world, colonistId, colonistPosition);
	adapters::sortTasksForDisplay(displayData);

	// Convert to TasksTabItem format
	for (const auto& task : displayData) {
		TasksTabItem item;
		item.description = task.description;
		item.position = task.position;
		item.distance = task.distance;
		item.status = task.status;
		item.isMine = task.isMine;
		tasksData.tasks.push_back(item);
	}

	tasksData.totalCount = tasksData.tasks.size();
}

} // namespace world_sim
