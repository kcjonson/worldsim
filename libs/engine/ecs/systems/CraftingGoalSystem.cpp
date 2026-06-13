#include "CraftingGoalSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>

#include <functional>
#include <string>
#include <vector>

namespace ecs {

	namespace {
		// Generate a unique chain ID for linking Harvest → Haul tasks
		uint64_t generateChainId() {
			static uint64_t nextChainId = 1;
			return nextChainId++;
		}

		// Stable, non-zero identity for a recipe defName (used to detect recipe swaps).
		uint32_t recipeIdentity(const std::string& recipeDefName) {
			if (recipeDefName.empty()) {
				return 0;
			}
			auto h = std::hash<std::string>{}(recipeDefName);
			auto id = static_cast<uint32_t>(h ^ (h >> 32));
			return id == 0 ? 1U : id; // never collide with the "no recipe" sentinel
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
		// Using ownership: only track root goals (Craft type) that we created
		std::unordered_set<EntityID> stationsWithGoals;
		for (const auto* goal : goalRegistry.getGoalsByOwner(GoalOwner::CraftingGoalSystem)) {
			if (goal->type == TaskType::Craft) {
				stationsWithGoals.insert(goal->destinationEntity);
			}
		}

		activeGoalCount = 0;

		// Query all entities with WorkQueue + Position (crafting stations)
		for (auto [entity, workQueue, position] : world->view<WorkQueue, Position>()) {
			// Check if there's pending work
			const CraftingJob* nextJob = workQueue.getNextJob();
			if (nextJob == nullptr) {
				// No pending work - remove goal and all children if exists
				const auto* existingGoal = goalRegistry.getGoalByDestination(entity);
				if (existingGoal != nullptr) {
					goalRegistry.removeGoalWithChildren(existingGoal->id);
				}
				stationsWithGoals.erase(entity);
				continue;
			}

			// Get recipe info
			const auto* recipe = recipeRegistry.getRecipe(nextJob->recipeDefName);
			if (recipe == nullptr) {
				// Invalid recipe - skip
				const auto* existingGoal = goalRegistry.getGoalByDestination(entity);
				if (existingGoal != nullptr) {
					goalRegistry.removeGoalWithChildren(existingGoal->id);
				}
				stationsWithGoals.erase(entity);
				continue;
			}

			uint32_t recipeId = recipeIdentity(nextJob->recipeDefName);

			// Build the child Harvest/Haul hierarchy under a Craft goal. Returns the total
			// number of input items the recipe needs (the Craft goal's targetAmount).
			auto buildChildHierarchy = [&](uint64_t craftGoalId) -> uint32_t {
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
						harvestGoal.owner = GoalOwner::CraftingGoalSystem;
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
					haulGoal.owner = GoalOwner::CraftingGoalSystem;
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
				return totalInputsNeeded;
			};

			// Check if goal already exists
			const auto* existingGoal = goalRegistry.getGoalByDestination(entity);
			if (existingGoal != nullptr) {
				if (existingGoal->recipeNameId == recipeId) {
					// Same recipe - keep the hierarchy, just refresh remaining job count.
					// Don't clobber targetAmount (it's the material total, not the job count)
					// or deliveredAmount (delivery progress).
					stationsWithGoals.erase(entity);
					activeGoalCount++;
					continue;
				}

				// Recipe swapped (e.g., job A finished, job B is now next): the old children
				// belong to the previous recipe. Tear the hierarchy down and rebuild for the
				// new recipe so children match the new inputs.
				uint64_t craftGoalId = existingGoal->id;
				std::vector<uint64_t> oldChildIds;
				for (const auto* child : goalRegistry.getChildGoals(craftGoalId)) {
					oldChildIds.push_back(child->id);
				}
				for (uint64_t childId : oldChildIds) {
					goalRegistry.removeGoalWithChildren(childId);
				}
				uint32_t totalInputsNeeded = buildChildHierarchy(craftGoalId);
				goalRegistry.updateGoal(craftGoalId, [&](GoalTask& goal) {
					goal.recipeNameId = recipeId;
					goal.targetAmount = totalInputsNeeded;
					goal.deliveredAmount = 0;
					goal.status = GoalStatus::Blocked;
				});
				stationsWithGoals.erase(entity);
				activeGoalCount++;
				continue;
			}

			// --- Create new goal hierarchy ---

			// 1. Create Craft goal (blocked until materials delivered)
			GoalTask craftGoal;
			craftGoal.type = TaskType::Craft;
			craftGoal.owner = GoalOwner::CraftingGoalSystem;
			craftGoal.destinationEntity = entity;
			craftGoal.destinationPosition = position.value;
			craftGoal.destinationDefNameId = 0;
			craftGoal.acceptedCategory = engine::assets::ItemCategory::None;
			craftGoal.targetAmount = nextJob->remaining();
			craftGoal.deliveredAmount = 0;
			craftGoal.createdAt = 0.0F;
			craftGoal.status = GoalStatus::Blocked; // Blocked until materials ready
			craftGoal.recipeNameId = recipeId;

			uint64_t craftGoalId = goalRegistry.createGoal(std::move(craftGoal));

			// 2. For each recipe input, create Harvest and/or Haul goals
			uint32_t totalInputsNeeded = buildChildHierarchy(craftGoalId);

			// Update craft goal with total inputs needed (the material total it tracks)
			goalRegistry.updateGoal(craftGoalId, [&](GoalTask& goal) {
				goal.targetAmount = totalInputsNeeded;
			});

			stationsWithGoals.erase(entity);
			activeGoalCount++;
		}

		// Remove goals for stations that no longer exist or have no WorkQueue
		for (EntityID oldStation : stationsWithGoals) {
			const auto* existingGoal = goalRegistry.getGoalByDestination(oldStation);
			if (existingGoal != nullptr) {
				goalRegistry.removeGoalWithChildren(existingGoal->id);
			}
		}
	}

} // namespace ecs
