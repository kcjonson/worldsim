#include "CraftingGoalSystem.h"

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

		// Colony "availability" = the union of colonist memories. Knowledge is per-colonist (no
		// god-view), but a craft goal is colony-level, so we resolve against what ANY colonist knows.
		// A goal created from this is still only fulfilled by a colonist that actually remembers the
		// source. These scans run on craft creation and to re-resolve NoSource children; both are
		// throttled and bounded by the (few) colonists and their capability-indexed memories.

		// Does any colonist remember a carryable instance of this item (loose ground stock)?
		bool colonyKnowsStock(World* world, uint32_t itemDefNameId) {
			for (auto [colonist, memory] : world->view<Memory>()) {
				(void)colonist;
				for (uint64_t key : memory.getEntitiesWithCapability(engine::assets::CapabilityType::Carryable)) {
					const KnownWorldEntity* known = memory.getWorldEntity(key);
					if (known != nullptr && known->defNameId == itemDefNameId) {
						return true;
					}
				}
			}
			return false;
		}

		// Does any colonist remember a harvestable whose yield is this item (e.g. a tree for Wood)?
		bool colonyKnowsHarvestableSource(World* world, const engine::assets::AssetRegistry& reg, uint32_t yieldDefNameId) {
			for (auto [colonist, memory] : world->view<Memory>()) {
				(void)colonist;
				for (uint64_t key : memory.getEntitiesWithCapability(engine::assets::CapabilityType::Harvestable)) {
					const KnownWorldEntity* known = memory.getWorldEntity(key);
					if (known == nullptr) {
						continue;
					}
					const auto& srcDefName = reg.getDefName(known->defNameId);
					const auto* srcDef = reg.getDefinition(srcDefName);
					if (srcDef != nullptr && srcDef->capabilities.harvestable.has_value() &&
						reg.getDefNameId(srcDef->capabilities.harvestable->yieldDefName) == yieldDefNameId) {
						return true;
					}
				}
			}
			return false;
		}

		// Re-resolve a craft's NoSource children (a Haul with no known source) as colony knowledge
		// grows: known stock -> Available fetch Haul; known harvestable -> swap to a Harvest goal.
		// In-progress Harvest/Haul children are left untouched.
		void reresolveUnsourcedChildren(
			World* world, GoalTaskRegistry& registry, const engine::assets::AssetRegistry& reg, uint64_t craftGoalId
		) {
			struct Pending {
				uint64_t id;
				uint32_t inputDefNameId;
			};
			std::vector<Pending> unsourced;
			for (const auto* child : registry.getChildGoals(craftGoalId)) {
				if (child->type == TaskType::Haul && child->status == GoalStatus::NoSource && !child->acceptedDefNameIds.empty()) {
					unsourced.push_back({child->id, child->acceptedDefNameIds.front()});
				}
			}

			for (const auto& p : unsourced) {
				if (colonyKnowsStock(world, p.inputDefNameId)) {
					registry.updateGoal(p.id, [](GoalTask& g) { g.status = GoalStatus::Available; });
					continue;
				}
				if (!colonyKnowsHarvestableSource(world, reg, p.inputDefNameId)) {
					continue; // still nothing known -> stays NoSource
				}
				// Swap the unsourced Haul for a Harvest (type change = remove + recreate).
				const GoalTask* old = registry.getGoal(p.id);
				if (old == nullptr) {
					continue;
				}
				GoalTask harvest;
				harvest.type = TaskType::Harvest;
				harvest.owner = old->owner;
				harvest.destinationEntity = old->destinationEntity;
				harvest.destinationPosition = old->destinationPosition;
				harvest.destinationDefNameId = old->destinationDefNameId;
				harvest.acceptedDefNameIds = old->acceptedDefNameIds;
				harvest.acceptedCategory = old->acceptedCategory;
				harvest.targetAmount = old->targetAmount;
				harvest.deliveredAmount = old->deliveredAmount;
				harvest.createdAt = old->createdAt;
				harvest.parentGoalId = old->parentGoalId;
				harvest.status = GoalStatus::Available;
				harvest.yieldDefNameId = p.inputDefNameId;
				harvest.chainId = old->chainId;
				registry.removeGoal(p.id);
				registry.createGoal(std::move(harvest));
			}
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

					// Resolve this input against colony knowledge (any colonist's memory):
					//  - known carryable stock    -> Haul it (Available, fetched from that source)
					//  - else a known harvestable  -> Harvest (cut); its Haul is created lazily
					//  - else                      -> a Haul that waits, NoSource ("none found")
					// NoSource children are re-resolved each tick as colonists discover sources.
					const bool stockKnown = colonyKnowsStock(world, inputDefNameId);
					const bool harvestKnown =
						!stockKnown && canHarvest && colonyKnowsHarvestableSource(world, assetRegistry, inputDefNameId);

					if (harvestKnown) {
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

						goalRegistry.createGoal(std::move(harvestGoal));
					} else {
						// No known harvestable: haul from existing stock if a colonist knows of some,
						// otherwise wait (NoSource) until a source is discovered.
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
						haulGoal.status = stockKnown ? GoalStatus::Available : GoalStatus::NoSource;
						haulGoal.chainId = chainId;

						goalRegistry.createGoal(std::move(haulGoal));
					}
				}
				return totalInputsNeeded;
			};

			// Check if goal already exists
			const auto* existingGoal = goalRegistry.getGoalByDestination(entity);
			if (existingGoal != nullptr) {
				if (existingGoal->recipeNameId == recipeId) {
					// Same recipe - keep the hierarchy, just refresh remaining job count.
					// Don't clobber targetAmount (it's the material total, not the job count)
					// or deliveredAmount (delivery progress). Re-resolve any still-unsourced
					// (NoSource) child against current colony knowledge: a discovered stockpile
					// upgrades it to a fetch Haul, a discovered harvestable swaps it to a Harvest.
					reresolveUnsourcedChildren(world, goalRegistry, assetRegistry, existingGoal->id);
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
