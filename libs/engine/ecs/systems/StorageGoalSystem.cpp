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
		// Using ownership: only track goals that StorageGoalSystem created
		std::unordered_set<EntityID> storagesWithGoals;
		for (const auto* goal : registry.getGoalsByOwner(GoalOwner::StorageGoalSystem)) {
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

			// Existence/full gate: a storage wants items as long as it has a free slot. Storage
			// capacity is a slot count; each stack occupies one slot. This is the authoritative
			// "is there any room" check -- it creates the goal while a slot is free and removes it
			// when the storage is slot-full. How MUCH a single haul carries is sized elsewhere,
			// per item, from the destination's addableCount (stack headroom + freeSlots *
			// stackSize); it is not the slot count. targetAmount below mirrors the free-slot count
			// purely as a "still wants items" signal (availableCapacity() > 0) for the task UI and
			// the decision layer; it is NOT a haul-size or a delivery quota.
			uint32_t usedSlots = inventory.getSlotCount();
			uint32_t availableSlots = 0;
			if (usedSlots < inventory.maxCapacity) {
				availableSlots = inventory.maxCapacity - usedSlots;
			}

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
				// Refresh the "wants items" signal from the live free-slot count (deposits shrank
				// it). deliveredAmount resets to 0 each refresh: it isn't a real quota for storage
				// (a haul's true clamp is the destination's per-item addableCount at deposit
				// time), just bookkeeping so availableCapacity() == targetAmount == free slots.
				registry.updateGoal(existingGoal->id, [&](GoalTask& goal) {
					goal.targetAmount = availableSlots;
					goal.deliveredAmount = 0;
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
			goal.owner = GoalOwner::StorageGoalSystem;
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
