#include "CraftingGoalSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <utils/Log.h>

namespace ecs {

	namespace {
		// Generate a unique chain ID for linking Harvest → Haul tasks
		uint64_t generateChainId() {
			static uint64_t nextChainId = 1;
			return nextChainId++;
		}

		// Check if any asset definition has a harvestable that yields the given item type
		// This determines if the item CAN be obtained through harvesting
		bool canItemBeHarvested(const engine::assets::AssetRegistry& assetRegistry, const std::string& itemDefName) {
			// Iterate all asset definitions to find harvestables that yield this item
			for (const auto& defName : assetRegistry.getDefinitionNames()) {
				const auto* def = assetRegistry.getDefinition(defName);
				if (def == nullptr) {
					continue;
				}

				// Check if this definition is harvestable and yields the item we need
				if (def->capabilities.harvestable.has_value()) {
					if (def->capabilities.harvestable->yieldDefName == itemDefName) {
						return true;
					}
				}
			}

			return false;
		}
	} // namespace

	void CraftingGoalSystem::update(float /*deltaTime*/) {
		if (world == nullptr) {
			return;
		}

		// Throttle: only update every N frames
		frameCounter++;
		if (frameCounter < updateFrameInterval) {
			return;
		}
		frameCounter = 0;

		auto& goalRegistry = GoalTaskRegistry::Get();
		auto& assetRegistry = engine::assets::AssetRegistry::Get();
		auto& recipeRegistry = engine::assets::RecipeRegistry::Get();

		// Track existing Craft goals to detect removed stations
		std::unordered_set<EntityID> stationsWithGoals;
		for (const auto* goal : goalRegistry.getGoalsOfType(TaskType::Craft)) {
			stationsWithGoals.insert(goal->destinationEntity);
		}

		activeGoalCount = 0;

		// Query all entities with WorkQueue + Position (crafting stations)
		LOG_DEBUG(Engine, "[CraftingGoalSystem] Starting main loop, stationsWithGoals has %zu entries",
			stationsWithGoals.size());

		for (auto [entity, workQueue, position] : world->view<WorkQueue, Position>()) {
			LOG_DEBUG(Engine, "[CraftingGoalSystem] Processing entity %llu",
				static_cast<unsigned long long>(entity));

			// Check if there's pending work
			const CraftingJob* nextJob = workQueue.getNextJob();
			if (nextJob == nullptr) {
				// No pending work - remove goal and all children if exists
				LOG_DEBUG(Engine, "[CraftingGoalSystem] Entity %llu has NO JOB, removing goals",
					static_cast<unsigned long long>(entity));
				const auto* existingGoal = goalRegistry.getGoalByDestination(entity);
				if (existingGoal != nullptr) {
					LOG_DEBUG(Engine, "[CraftingGoalSystem] Removing goal %llu for entity %llu (no job)",
						static_cast<unsigned long long>(existingGoal->id),
						static_cast<unsigned long long>(entity));
					goalRegistry.removeGoalWithChildren(existingGoal->id);
				}
				stationsWithGoals.erase(entity);
				continue;
			}

			// Get recipe info
			const auto* recipe = recipeRegistry.getRecipe(nextJob->recipeDefName);
			if (recipe == nullptr) {
				// Invalid recipe - skip
				LOG_DEBUG(Engine, "[CraftingGoalSystem] Entity %llu has INVALID RECIPE: %s",
					static_cast<unsigned long long>(entity),
					nextJob->recipeDefName.c_str());
				const auto* existingGoal = goalRegistry.getGoalByDestination(entity);
				if (existingGoal != nullptr) {
					goalRegistry.removeGoalWithChildren(existingGoal->id);
				}
				stationsWithGoals.erase(entity);
				continue;
			}

			LOG_DEBUG(Engine, "[CraftingGoalSystem] Entity %llu has job: %s (remaining=%u)",
				static_cast<unsigned long long>(entity),
				nextJob->recipeDefName.c_str(),
				nextJob->remaining());

			// Check if goal already exists
			const auto* existingGoal = goalRegistry.getGoalByDestination(entity);
			LOG_INFO(Engine, "[CraftingGoalSystem] Entity %llu: existingGoal=%s",
				static_cast<unsigned long long>(entity),
				existingGoal ? "found" : "NOT FOUND");
			if (existingGoal != nullptr) {
				// Goal exists - update target amount but keep hierarchy
				goalRegistry.updateGoal(existingGoal->id, [&](GoalTask& goal) {
					goal.targetAmount = nextJob->remaining();
				});
				stationsWithGoals.erase(entity);
				activeGoalCount++;
				continue;
			}

			LOG_INFO(Engine, "[CraftingGoalSystem] Creating NEW goal hierarchy for entity %llu",
				static_cast<unsigned long long>(entity));

			// --- Create new goal hierarchy ---

			// 1. Create Craft goal (blocked until materials delivered)
			GoalTask craftGoal;
			craftGoal.type = TaskType::Craft;
			craftGoal.destinationEntity = entity;
			craftGoal.destinationPosition = position.value;
			craftGoal.destinationDefNameId = 0;
			craftGoal.acceptedCategory = engine::assets::ItemCategory::None;
			craftGoal.targetAmount = nextJob->remaining();
			craftGoal.deliveredAmount = 0;
			craftGoal.createdAt = 0.0F;
			craftGoal.status = GoalStatus::Blocked; // Blocked until materials ready

			uint64_t craftGoalId = goalRegistry.createGoal(std::move(craftGoal));

			// 2. For each recipe input, create Harvest and/or Haul goals
			uint32_t totalInputsNeeded = 0;
			for (const auto& input : recipe->inputs) {
				uint32_t inputDefNameId = assetRegistry.getDefNameId(input.defName);
				if (inputDefNameId == 0) {
					continue;
				}

				totalInputsNeeded += input.count;

				// Check if this input can come from harvestable sources
				bool canHarvest = canItemBeHarvested(assetRegistry, input.defName);

				// Generate chain ID to link Harvest → Haul (for continuity bonus)
				uint64_t chainId = generateChainId();

				std::optional<uint64_t> harvestGoalId;

				if (canHarvest) {
					// Create Harvest goal
					GoalTask harvestGoal;
					harvestGoal.type = TaskType::Harvest;
					harvestGoal.destinationEntity = entity;
					harvestGoal.destinationPosition = position.value;
					harvestGoal.destinationDefNameId = 0;
					harvestGoal.acceptedDefNameIds = {inputDefNameId};
					harvestGoal.acceptedCategory = engine::assets::ItemCategory::None;
					harvestGoal.targetAmount = input.count;
					harvestGoal.deliveredAmount = 0;
					harvestGoal.createdAt = 0.0F;
					harvestGoal.parentGoalId = craftGoalId;
					harvestGoal.status = GoalStatus::Available;
					harvestGoal.yieldDefNameId = inputDefNameId;
					harvestGoal.chainId = chainId;

					harvestGoalId = goalRegistry.createGoal(std::move(harvestGoal));
				}

				// Create Haul goal
				GoalTask haulGoal;
				haulGoal.type = TaskType::Haul;
				haulGoal.destinationEntity = entity;
				haulGoal.destinationPosition = position.value;
				haulGoal.destinationDefNameId = 0;
				haulGoal.acceptedDefNameIds = {inputDefNameId};
				haulGoal.acceptedCategory = engine::assets::ItemCategory::None;
				haulGoal.targetAmount = input.count;
				haulGoal.deliveredAmount = 0;
				haulGoal.createdAt = 0.0F;
				haulGoal.parentGoalId = craftGoalId;
				haulGoal.chainId = chainId;

				if (harvestGoalId.has_value()) {
					// Haul depends on Harvest completing
					haulGoal.dependsOnGoalId = harvestGoalId;
					haulGoal.status = GoalStatus::WaitingForItems;
				} else {
					// No harvest needed - Haul can start immediately
					haulGoal.status = GoalStatus::Available;
				}

				goalRegistry.createGoal(std::move(haulGoal));
			}

			// Update craft goal with total inputs needed
			goalRegistry.updateGoal(craftGoalId, [&](GoalTask& goal) {
				goal.targetAmount = totalInputsNeeded;
			});

			stationsWithGoals.erase(entity);
			activeGoalCount++;
		}

		// Remove goals for stations that no longer exist or have no WorkQueue
		for (EntityID oldStation : stationsWithGoals) {
			LOG_DEBUG(Engine, "[CraftingGoalSystem] Cleanup: station %llu no longer in view",
				static_cast<unsigned long long>(oldStation));
			const auto* existingGoal = goalRegistry.getGoalByDestination(oldStation);
			if (existingGoal != nullptr) {
				LOG_DEBUG(Engine, "[CraftingGoalSystem] Cleanup: removing goal %llu",
					static_cast<unsigned long long>(existingGoal->id));
				goalRegistry.removeGoalWithChildren(existingGoal->id);
			}
		}

		// Log total goal count for debugging
		size_t totalGoals = goalRegistry.getGoalsOfType(TaskType::Craft).size() +
							goalRegistry.getGoalsOfType(TaskType::Harvest).size() +
							goalRegistry.getGoalsOfType(TaskType::Haul).size();
		LOG_DEBUG(Engine, "[CraftingGoalSystem] Update complete. Total goals: %zu (Craft=%zu, Harvest=%zu, Haul=%zu)",
			totalGoals,
			goalRegistry.getGoalsOfType(TaskType::Craft).size(),
			goalRegistry.getGoalsOfType(TaskType::Harvest).size(),
			goalRegistry.getGoalsOfType(TaskType::Haul).size());
	}

} // namespace ecs
