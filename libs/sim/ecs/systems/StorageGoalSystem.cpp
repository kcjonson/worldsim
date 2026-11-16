#include "StorageGoalSystem.h"

#include "GoalSystemHelpers.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/Packaged.h"
#include "../components/StorageConfiguration.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>

#include <algorithm>
#include <cstdint>

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
		auto& assetRegistry = assets::AssetRegistry::Get();

		// Track existing goals to detect removed storages
		// Using ownership: only track goals that StorageGoalSystem created
		std::unordered_set<EntityID> storagesWithGoals;
		for (const auto* goal : registry.getGoalsByOwner(GoalOwner::StorageGoalSystem)) {
			storagesWithGoals.insert(goal->destinationEntity);
		}

		activeGoalCount = 0;

		// Cascade-remove a storage's umbrella Haul goal AND its stocking Harvest children, which
		// hang off the umbrella. A plain removeGoalByDestination drops only the top-level Haul and
		// would orphan the children. Another system's goal on the box (a placement goal) is not ours.
		auto removeStorageGoalTree = [&registry](EntityID storage) {
			const auto* goal = registry.getGoalByDestination(storage);
			if (goal != nullptr && goal->owner == GoalOwner::StorageGoalSystem) {
				registry.removeGoalWithChildren(goal->id);
			}
		};

		// Query all entities with StorageConfiguration + Inventory + Position
		for ([[maybe_unused]] auto [entity, config, inventory, position] :
			 world->view<StorageConfiguration, Inventory, Position>()) {
			// A box with no rules, or a packaged one (being placed or moved, it wants nothing), has no storage goal
			if (!config.hasRules() || world->hasComponent<Packaged>(entity)) {
				removeStorageGoalTree(entity);
				storagesWithGoals.erase(entity);
				continue;
			}

			// The placement goal of a box just installed stays until BuildGoalSystem retires it at its next poll
			const auto* existingGoal = registry.getGoalByDestination(entity);
			if (existingGoal != nullptr && existingGoal->owner != GoalOwner::StorageGoalSystem) {
				continue;
			}

			// The umbrella stays while the box has rules, full or not, so goods on their way keep counting
			// toward it and a suspended record of them stays adoptable. How much of each item it wants is
			// its remaining need (GoalTaskRegistry::remainingNeed), counted in items against its rules and
			// its room, so a full box simply wants nothing.
			std::vector<uint32_t> acceptedDefNameIds;
			assets::ItemCategory  primaryCategory = assets::ItemCategory::None;
			for (const auto& rule : config.rules) {
				if (rule.isWildcard()) {
					primaryCategory = rule.category;
				} else if (const uint32_t defNameId = assetRegistry.getDefNameId(rule.defName); defNameId != 0) {
					acceptedDefNameIds.push_back(defNameId);
				}
			}

			uint64_t umbrellaId = 0;
			if (existingGoal != nullptr) {
				umbrellaId = existingGoal->id;
				registry.updateGoal(existingGoal->id, [&](GoalTask& goal) {
					goal.acceptedDefNameIds = acceptedDefNameIds;
					goal.acceptedCategory = primaryCategory;
				});
			} else {
				GoalTask goal;
				goal.type = TaskType::Haul;
				goal.owner = GoalOwner::StorageGoalSystem;
				goal.destinationEntity = entity;
				goal.destinationPosition = position.value;
				goal.acceptedDefNameIds = acceptedDefNameIds;
				goal.acceptedCategory = primaryCategory;
				umbrellaId = registry.createGoal(std::move(goal));
			}

			reconcileStockingHarvests(registry, assetRegistry, config, umbrellaId);

			storagesWithGoals.erase(entity);
			activeGoalCount++;
		}

		// Remove goals (and stocking children) for storages that no longer exist
		for (EntityID oldStorage : storagesWithGoals) {
			removeStorageGoalTree(oldStorage);
		}
	}

	void StorageGoalSystem::reconcileStockingHarvests(
		GoalTaskRegistry& registry, const assets::AssetRegistry& assetRegistry, const StorageConfiguration& config, uint64_t umbrellaId
	) {
		// One Harvest child per specific-item rule whose minimum the box lacks, while a colonist knows
		// a harvestable that yields the item. The child only marks that cutting is open for the item;
		// how much more to cut is the umbrella's remaining need toward the minimum, net of in-flight.
		// Category wildcard rules never drive harvest.
		const auto stocksItem = [&](uint32_t itemDefNameId) {
			for (const auto& rule : config.rules) {
				if (!rule.isWildcard() && rule.minAmount > 0 && assetRegistry.getDefNameId(rule.defName) == itemDefNameId) {
					return registry.remainingNeed(*world, umbrellaId, itemDefNameId, NeedBasis::Harvest) > 0 &&
						   colonyKnowsHarvestableSource(world, assetRegistry, itemDefNameId);
				}
			}
			return false;
		};

		std::vector<uint32_t> stocked;
		std::vector<uint64_t> retired;
		for (const auto* child : registry.getChildGoals(umbrellaId)) {
			if (child->type == TaskType::Harvest && stocksItem(child->yieldDefNameId) &&
				std::find(stocked.begin(), stocked.end(), child->yieldDefNameId) == stocked.end()) {
				stocked.push_back(child->yieldDefNameId);
			} else {
				retired.push_back(child->id);
			}
		}
		for (uint64_t id : retired) {
			registry.removeGoal(id);
		}

		const auto* umbrella = registry.getGoal(umbrellaId);
		for (const auto& rule : config.rules) {
			const uint32_t itemDefNameId = rule.isWildcard() ? 0U : assetRegistry.getDefNameId(rule.defName);
			if (itemDefNameId == 0 || std::find(stocked.begin(), stocked.end(), itemDefNameId) != stocked.end() || !stocksItem(itemDefNameId)) {
				continue;
			}
			stocked.push_back(itemDefNameId);
			GoalTask harvest;
			harvest.type = TaskType::Harvest;
			harvest.owner = GoalOwner::StorageGoalSystem;
			harvest.destinationEntity = umbrella->destinationEntity;
			harvest.destinationPosition = umbrella->destinationPosition;
			harvest.acceptedDefNameIds = {itemDefNameId};
			harvest.parentGoalId = umbrellaId;
			harvest.yieldDefNameId = itemDefNameId;
			registry.createGoal(std::move(harvest));
		}
	}

} // namespace ecs
