#include "CraftingGoalSystem.h"

#include "GoalSystemHelpers.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "../components/Colonist.h"
#include "../components/Inventory.h"
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

		// Does any COLONIST already PHYSICALLY carry this item? If so, the recipe input should be a
		// Haul (deliver-from-inventory into the station), not a fresh Harvest -- a colonist holding a
		// Stick must deliver it, not go cut another one.
		// Scope to entities with a Colonist tag: a craft-material haul only sources from a colonist's
		// own pack (deliver-from-inventory) or a loose ground pile, never another station's or
		// storage's store. Viewing every Inventory would count a leftover stack sitting in a
		// different crafting station as "carried", building a fetch Haul that can never source it --
		// the craft then strands with that input missing. Stations/storage lack the Colonist tag.
		bool colonyCarriesStock(World* world, const std::string& itemDefName) {
			for (auto [entity, colonist, inventory] : world->view<Colonist, Inventory>()) {
				(void)entity;
				(void)colonist;
				if (ecs::availableQuantity(inventory, itemDefName) > 0) {
					return true;
				}
			}
			return false;
		}

		// Re-resolve a craft's Haul children against current colony knowledge each tick:
		//  - a NoSource Haul whose stock becomes known    -> Available fetch Haul
		//  - a Haul (NoSource OR Available) whose stock is GONE but a harvestable source is known
		//    -> swap to a Harvest goal (cut it)
		// The second case is the rescue: a fetch Haul is created Available when loose stock is known
		// (e.g. natural ground scatter), but that stock gets consumed/forgotten while the craft is
		// only half-provisioned. The fetch then finds nothing to pick up, emits no AI option, and
		// the craft strands forever. Swapping it to cut a known source recovers it. We only swap
		// when NOBODY can still fetch it (no known loose stock, none carried) so an in-flight
		// delivery is never yanked out from under a colonist.
		// In-progress Harvest children are left untouched (only Haul children are re-resolved).
		void reresolveUnsourcedChildren(
			World* world, GoalTaskRegistry& registry, const engine::assets::AssetRegistry& reg, uint64_t craftGoalId
		) {
			struct Pending {
				uint64_t id;
				uint32_t inputDefNameId;
			};
			std::vector<Pending> candidates;
			for (const auto* child : registry.getChildGoals(craftGoalId)) {
				const bool reresolvable =
					child->status == GoalStatus::NoSource || child->status == GoalStatus::Available;
				if (child->type == TaskType::Haul && reresolvable && !child->acceptedDefNameIds.empty()) {
					candidates.push_back({child->id, child->acceptedDefNameIds.front()});
				}
			}

			for (const auto& p : candidates) {
				const std::string& inputDefName = reg.getDefName(p.inputDefNameId);
				const bool			stockFetchable = colonyCarriesStock(world, inputDefName) || colonyKnowsStock(world, p.inputDefNameId);
				if (stockFetchable) {
					// Stock is still reachable as loose pile or in a pack: keep it a fetch Haul.
					registry.updateGoal(p.id, [](GoalTask& g) {
						if (g.status == GoalStatus::NoSource) {
							g.status = GoalStatus::Available;
						}
					});
					continue;
				}
				if (!colonyKnowsHarvestableSource(world, reg, p.inputDefNameId)) {
					continue; // no stock and no harvestable known -> wait (stays as-is)
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

			// Materials already in the station's store count as delivered: only the shortfall needs
			// provisioning. This mirrors construction reading the blueprint's delivered[] manifest,
			// and keeps a re-queued job or leftover stock from re-gathering what's already on hand.
			const auto* stationStore = world->getComponent<Inventory>(entity);

			// {targetAmount, alreadyStaged}: the total inputs the recipe needs, and how many of those
			// the station store currently holds (clamped per-input, summed). Recomputed from the LIVE
			// store, never accumulated: when a finished unit drains the store, this drops, which is how
			// the Craft goal learns the next unit still needs provisioning.
			auto computeStaged = [&]() -> std::pair<uint32_t, uint32_t> {
				uint32_t totalInputsNeeded = 0;
				uint32_t alreadyStaged = 0;
				for (const auto& input : recipe->inputs) {
					if (assetRegistry.getDefNameId(input.defName) == 0) {
						continue;
					}
					totalInputsNeeded += input.count;
					const uint32_t inStore = stationStore != nullptr ? stationStore->getQuantity(input.defName) : 0U;
					alreadyStaged += std::min(inStore, input.count);
				}
				return {totalInputsNeeded, alreadyStaged};
			};

			// Build the child Harvest/Haul hierarchy under a Craft goal. Returns {targetAmount,
			// alreadyStaged}: the total inputs the recipe needs, and how many of those the station
			// store already holds (pre-credited to the Craft goal so it unblocks once the rest arrive).
			auto buildChildHierarchy = [&](uint64_t craftGoalId) -> std::pair<uint32_t, uint32_t> {
				uint32_t totalInputsNeeded = 0;
				uint32_t alreadyStaged = 0;
				for (const auto& input : recipe->inputs) {
					uint32_t inputDefNameId = assetRegistry.getDefNameId(input.defName);
					if (inputDefNameId == 0) {
						continue;
					}

					totalInputsNeeded += input.count;

					// Subtract what the station already holds; only provision the remainder.
					const uint32_t inStore = stationStore != nullptr ? stationStore->getQuantity(input.defName) : 0U;
					const uint32_t staged = std::min(inStore, input.count);
					alreadyStaged += staged;
					const uint32_t stillNeeded = input.count - staged;
					if (stillNeeded == 0) {
						continue; // fully satisfied from the store; no child goal needed
					}

					// Check if this input can come from harvestable sources
					bool canHarvest = canItemBeHarvested(assetRegistry, input.defName);

					// Generate chain ID to link Harvest → Haul (for continuity bonus)
					uint64_t chainId = generateChainId();

					// Resolve this input against colony state (any colonist):
					//  - already carried in a pack -> Haul it straight into the station (deliver it,
					//                                 don't go harvest a fresh one)
					//  - known carryable stock     -> Haul it (Available, fetched from that source)
					//  - else a known harvestable  -> Harvest (cut); its Haul is created lazily
					//  - else                      -> a Haul that waits, NoSource ("none found")
					// NoSource children are re-resolved each tick as colonists discover sources.
					const bool carried = colonyCarriesStock(world, input.defName);
					const bool stockKnown = carried || colonyKnowsStock(world, inputDefNameId);
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
						harvestGoal.targetAmount = stillNeeded;
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
						haulGoal.targetAmount = stillNeeded;
						haulGoal.deliveredAmount = 0;
						haulGoal.createdAt = 0.0F;
						haulGoal.parentGoalId = craftGoalId;
						haulGoal.status = stockKnown ? GoalStatus::Available : GoalStatus::NoSource;
						haulGoal.chainId = chainId;

						goalRegistry.createGoal(std::move(haulGoal));
					}
				}
				return {totalInputsNeeded, alreadyStaged};
			};

			// Check if goal already exists
			const auto* existingGoal = goalRegistry.getGoalByDestination(entity);
			if (existingGoal != nullptr) {
				if (existingGoal->recipeNameId == recipeId) {
					// Same recipe across a multi-unit job. deliveredAmount tracks materials staged in
					// the station store toward the NEXT unit, so it must be recomputed from the live
					// store every tick, not treated as a monotonic counter: finishing a unit drains the
					// store (CraftActions consumes the inputs), and only re-reading the store reflects
					// that drop. Treating it as monotonic was the multi-unit stall -- after unit 1 the
					// store emptied but deliveredAmount stayed >= targetAmount, so the Craft never went
					// back to Blocked and its provisioning children were never rebuilt (hung at 1/N).
					auto [totalInputsNeeded, alreadyStaged] = computeStaged();

					// A unit was consumed if the store no longer covers the recipe AND no provisioning
					// children are currently outstanding for this Craft. Rebuild the Harvest/Haul
					// hierarchy for the shortfall (children already provisioning an in-flight unit are
					// left alone; we only rebuild once they're gone and the store is short).
					const bool hasChildren = !goalRegistry.getChildGoals(existingGoal->id).empty();
					if (alreadyStaged < totalInputsNeeded && !hasChildren) {
						buildChildHierarchy(existingGoal->id);
					}

					goalRegistry.updateGoal(existingGoal->id, [&](GoalTask& goal) {
						goal.targetAmount = totalInputsNeeded;
						goal.deliveredAmount = alreadyStaged;
						goal.status = alreadyStaged >= totalInputsNeeded ? GoalStatus::Available : GoalStatus::Blocked;
					});

					// Re-resolve any still-unsourced (NoSource) child against current colony knowledge:
					// a discovered stockpile upgrades it to a fetch Haul, a discovered harvestable swaps
					// it to a Harvest.
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
				auto [totalInputsNeeded, alreadyStaged] = buildChildHierarchy(craftGoalId);
				goalRegistry.updateGoal(craftGoalId, [&](GoalTask& goal) {
					goal.recipeNameId = recipeId;
					goal.targetAmount = totalInputsNeeded;
					goal.deliveredAmount = alreadyStaged;
					// If the store already holds every input, the craft is workable immediately.
					goal.status = alreadyStaged >= totalInputsNeeded ? GoalStatus::Available : GoalStatus::Blocked;
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

			// 2. For each recipe input, create Harvest and/or Haul goals (only for the shortfall
			//    the station store doesn't already hold).
			auto [totalInputsNeeded, alreadyStaged] = buildChildHierarchy(craftGoalId);

			// Update craft goal with the material total it tracks, pre-crediting what the store
			// already holds. If everything is already staged, it's workable right away.
			goalRegistry.updateGoal(craftGoalId, [&](GoalTask& goal) {
				goal.targetAmount = totalInputsNeeded;
				goal.deliveredAmount = alreadyStaged;
				goal.status = alreadyStaged >= totalInputsNeeded ? GoalStatus::Available : GoalStatus::Blocked;
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
