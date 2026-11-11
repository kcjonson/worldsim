#include "CraftingGoalSystem.h"

#include "GoalSystemHelpers.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Memory.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>

#include <functional>
#include <string>
#include <vector>

namespace ecs {

	namespace {
		// Stable, non-zero identity for a recipe defName (used to detect recipe swaps).
		uint32_t recipeIdentity(const std::string& recipeDefName) {
			if (recipeDefName.empty()) {
				return 0;
			}
			auto h = std::hash<std::string>{}(recipeDefName);
			auto id = static_cast<uint32_t>(h ^ (h >> 32));
			return id == 0 ? 1U : id; // never collide with the "no recipe" sentinel
		}

		// Colony "availability" = the union of colonist memories. Knowledge is per-colonist (no
		// god-view), but a craft goal is colony-level, so we resolve against what ANY colonist knows.
		// A goal created from this is still only fulfilled by a colonist that actually remembers the
		// source. Throttled and bounded by the (few) colonists and their capability-indexed memories.
		bool colonyKnowsStock(World* world, uint32_t itemDefNameId) {
			for (auto [colonist, memory] : world->view<Memory>()) {
				(void)colonist;
				for (uint64_t key : memory.getEntitiesWithCapability(assets::CapabilityType::Carryable)) {
					const KnownWorldEntity* known = memory.getWorldEntity(key);
					if (known != nullptr && known->defNameId == itemDefNameId) {
						return true;
					}
				}
			}
			return false;
		}

		GoalTask provisioningChild(TaskType type, const GoalTask& craft, uint32_t inputDefNameId, GoalStatus status) {
			GoalTask child;
			child.type = type;
			child.owner = GoalOwner::CraftingGoalSystem;
			child.destinationEntity = craft.destinationEntity;
			child.destinationPosition = craft.destinationPosition;
			child.acceptedDefNameIds = {inputDefNameId};
			child.parentGoalId = craft.id;
			child.status = status;
			child.yieldDefNameId = type == TaskType::Harvest ? inputDefNameId : 0U;
			return child;
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
		auto& assetRegistry = assets::AssetRegistry::Get();
		auto& recipeRegistry = assets::RecipeRegistry::Get();

		// Track existing Craft goals to detect removed stations
		std::unordered_set<EntityID> stationsWithGoals;
		for (const auto* goal : goalRegistry.getGoalsByOwner(GoalOwner::CraftingGoalSystem)) {
			if (goal->type == TaskType::Craft) {
				stationsWithGoals.insert(goal->destinationEntity);
			}
		}

		activeGoalCount = 0;

		for (auto [entity, workQueue, position] : world->view<WorkQueue, Position>()) {
			const CraftingJob* nextJob = workQueue.getNextJob();
			const auto*		   recipe = nextJob != nullptr ? recipeRegistry.getRecipe(nextJob->recipeDefName) : nullptr;
			if (recipe == nullptr) {
				// No pending work, or an invalid recipe: no goal and no children.
				if (const auto* existingGoal = goalRegistry.getGoalByDestination(entity)) {
					goalRegistry.removeGoalWithChildren(existingGoal->id);
				}
				stationsWithGoals.erase(entity);
				continue;
			}

			const uint32_t recipeId = recipeIdentity(nextJob->recipeDefName);
			uint64_t	   craftId = 0;
			if (const auto* existingGoal = goalRegistry.getGoalByDestination(entity)) {
				craftId = existingGoal->id;
				if (existingGoal->recipeNameId != recipeId) {
					// The next job is a different recipe: the old children provision the old inputs.
					std::vector<uint64_t> oldChildIds;
					for (const auto* child : goalRegistry.getChildGoals(craftId)) {
						oldChildIds.push_back(child->id);
					}
					for (uint64_t childId : oldChildIds) {
						goalRegistry.removeGoalWithChildren(childId);
					}
					goalRegistry.updateGoal(craftId, [&](GoalTask& goal) { goal.recipeNameId = recipeId; });
				}
			} else {
				GoalTask craftGoal;
				craftGoal.type = TaskType::Craft;
				craftGoal.owner = GoalOwner::CraftingGoalSystem;
				craftGoal.destinationEntity = entity;
				craftGoal.destinationPosition = position.value;
				craftGoal.status = GoalStatus::Blocked;
				craftGoal.recipeNameId = recipeId;
				craftId = goalRegistry.createGoal(std::move(craftGoal));
			}

			// One provisioning child per input the station store still lacks, re-resolved against what
			// the colony knows each poll: known stock opens a fetch Haul, otherwise a known harvestable
			// opens a Harvest, otherwise a Haul waits NoSource. A child only marks which source kind is
			// open; how much more to bring is the Craft root's remaining need net of in-flight, so an
			// input is provisioned unit after unit of a multi-unit job without rebuilding anything.
			bool allStaged = true;
			for (const auto& input : recipe->inputs) {
				const uint32_t inputId = assetRegistry.getDefNameId(input.defName);
				if (inputId == 0) {
					continue;
				}
				const GoalTask* child = nullptr;
				for (const auto* candidate : goalRegistry.getChildGoals(craftId)) {
					if (!candidate->acceptedDefNameIds.empty() && candidate->acceptedDefNameIds.front() == inputId) {
						child = candidate;
					}
				}
				if (goalRegistry.remainingNeed(*world, craftId, inputId, NeedBasis::Stock) == 0) {
					if (child != nullptr) {
						goalRegistry.removeGoal(child->id);
					}
					continue;
				}
				allStaged = false;
				if (child != nullptr && child->type == TaskType::Harvest) {
					continue;
				}
				const bool		stockKnown = colonyKnowsStock(world, inputId);
				const bool		harvestKnown = !stockKnown && colonyKnowsHarvestableSource(world, assetRegistry, inputId);
				const TaskType	type = harvestKnown ? TaskType::Harvest : TaskType::Haul;
				const GoalStatus status = stockKnown || harvestKnown ? GoalStatus::Available : GoalStatus::NoSource;
				if (child != nullptr && child->type == type) {
					goalRegistry.updateGoal(child->id, [status](GoalTask& goal) { goal.status = status; });
					continue;
				}
				if (child != nullptr) {
					goalRegistry.removeGoal(child->id);
				}
				goalRegistry.createGoal(provisioningChild(type, *goalRegistry.getGoal(craftId), inputId, status));
			}

			const GoalStatus craftStatus = allStaged ? GoalStatus::Available : GoalStatus::Blocked;
			goalRegistry.updateGoal(craftId, [craftStatus](GoalTask& goal) { goal.status = craftStatus; });
			stationsWithGoals.erase(entity);
			activeGoalCount++;
		}

		// Remove goals for stations that no longer exist or have no WorkQueue
		for (EntityID oldStation : stationsWithGoals) {
			if (const auto* existingGoal = goalRegistry.getGoalByDestination(oldStation)) {
				goalRegistry.removeGoalWithChildren(existingGoal->id);
			}
		}
	}

} // namespace ecs
