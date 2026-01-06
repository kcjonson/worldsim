#include "StorageGoalSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/StorageConfiguration.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>

namespace ecs {

	void StorageGoalSystem::update(float /*deltaTime*/) {
		if (world == nullptr) {
			return;
		}

		// Throttle: only update every N frames
		frameCounter++;
		if (frameCounter < updateFrameInterval) {
			return;
		}
		frameCounter = 0;

		auto& registry = GoalTaskRegistry::Get();
		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		// Track existing goals to detect removed storages
		std::unordered_set<EntityID> storagesWithGoals;
		for (const auto* goal : registry.getGoalsOfType(TaskType::Haul)) {
			storagesWithGoals.insert(goal->destinationEntity);
		}

		activeGoalCount = 0;

		// Query all entities with StorageConfiguration + Inventory + Position
		for (auto [entity, config, inventory, position] :
			 world->view<StorageConfiguration, Inventory, Position>()) {
			// Only process storages with rules configured
			if (!config.hasRules()) {
				// No rules = storage not configured yet
				// Remove any existing goal
				registry.removeGoalByDestination(entity);
				storagesWithGoals.erase(entity);
				continue;
			}

			// Check available capacity
			// Storage capacity is based on maxCapacity (distinct item types)
			uint32_t usedSlots = static_cast<uint32_t>(inventory.items.size());
			uint32_t availableSlots = 0;
			if (usedSlots < inventory.maxCapacity) {
				availableSlots = inventory.maxCapacity - usedSlots;
			}

			// Check if storage is full
			if (availableSlots == 0) {
				// Full - remove goal if exists
				registry.removeGoalByDestination(entity);
				storagesWithGoals.erase(entity);
				continue;
			}

			// Build list of accepted defNameIds from storage rules
			std::vector<uint32_t> acceptedDefNameIds;
			engine::assets::ItemCategory primaryCategory = engine::assets::ItemCategory::None;

			for (const auto& rule : config.rules) {
				if (rule.isWildcard()) {
					// Wildcard rule - accept entire category
					// We'll track the category for filtering
					primaryCategory = rule.category;
				} else {
					// Specific item rule
					uint32_t defNameId = assetRegistry.getDefNameId(rule.defName);
					if (defNameId != 0) {
						acceptedDefNameIds.push_back(defNameId);
					}
				}
			}

			// Check if goal already exists
			const auto* existingGoal = registry.getGoalByDestination(entity);
			if (existingGoal != nullptr) {
				// Update existing goal
				registry.updateGoal(existingGoal->id, [&](GoalTask& goal) {
					goal.targetAmount = availableSlots;
					goal.acceptedDefNameIds = acceptedDefNameIds;
					goal.acceptedCategory = primaryCategory;
				});
				storagesWithGoals.erase(entity);
				activeGoalCount++;
				continue;
			}

			// Create new goal
			GoalTask goal;
			goal.type = TaskType::Haul;
			goal.destinationEntity = entity;
			goal.destinationPosition = position.value;
			goal.destinationDefNameId = 0; // Could be set from storage's defName if available
			goal.acceptedDefNameIds = acceptedDefNameIds;
			goal.acceptedCategory = primaryCategory;
			goal.targetAmount = availableSlots;
			goal.deliveredAmount = 0;
			goal.createdAt = 0.0F; // TODO: use actual game time

			registry.createGoal(std::move(goal));
			storagesWithGoals.erase(entity);
			activeGoalCount++;
		}

		// Remove goals for storages that no longer exist
		for (EntityID oldStorage : storagesWithGoals) {
			registry.removeGoalByDestination(oldStorage);
		}
	}

} // namespace ecs
