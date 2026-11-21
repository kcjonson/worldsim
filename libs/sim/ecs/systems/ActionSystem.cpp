#include "ActionSystem.h"

#include "AIDecisionSystem.h"
#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "ChainClaims.h"
#include "NavigationSystem.h"
#include "TimeSystem.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/NavPath.h"
#include "../components/Needs.h"
#include "../components/Packaged.h"
#include "../components/Skills.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <utils/Log.h>

#include <glm/geometric.hpp>

#include <limits>

namespace ecs {

	namespace {
		// Within this of its task's target a colonist acts. Colonists bound for one point (a station, a shore
		// point) push one another apart, so no more than one could ever reach the point itself. Inside the
		// haul and placement legs' 0.5 m tolerance, so a leg that arrives this way is at its source or target.
		constexpr float kActionReachMeters = 0.4F;

		bool actsOnArrival(TaskType type) {
			return type == TaskType::FulfillNeed || type == TaskType::Craft || type == TaskType::Haul || type == TaskType::PlacePackaged ||
				   type == TaskType::Harvest || type == TaskType::Build || type == TaskType::Deconstruct;
		}
	} // namespace

	void ActionSystem::update(float deltaTime) {
		// Actions advance on game time, so fast-forward (3x/10x) speeds up work and pause
		// freezes it. TimeSystem is always registered in-game; unit tools/tests that omit it
		// fall back to real time (scale 1.0), keeping action durations equal to seconds there.
		float timeScale = 1.0F;
		if (auto* timeSystem = world->tryGetSystem<TimeSystem>()) {
			timeScale = timeSystem->effectiveTimeScale();
		}
		const float scaledDt = deltaTime * timeScale;
		forgetGoneWalls();

		// Process all colonists with the required components.
		//
		// Action Interruption Policy: a non-interruptable action (every one but Sleep)
		// runs to completion; AIDecisionSystem holds the task until it ends, unless the
		// colonist loses the task another way (direct control, or a snap back onto the
		// nav mesh), which clears the action with it. When AIDecisionSystem switches away
		// from an interruptable one, it clears the action with the task, and nothing
		// resumes: an interrupted sleep pays out nothing.
		for (auto [entity, position, task, action, needs, memory, inventory] :
			 world->view<Position, Task, Action, NeedsComponent, Memory, Inventory>()) {

			if (task.state == TaskState::Moving && actsOnArrival(task.type) &&
				glm::distance(position.value, task.targetPosition) <= kActionReachMeters) {
				task.state = TaskState::Arrived;
				if (auto* movementTarget = world->getComponent<MovementTarget>(entity)) {
					movementTarget->active = false;
				}
				if (auto* navPath = world->getComponent<NavPath>(entity)) {
					navPath->valid = false;
				}
			}

			if (task.state != TaskState::Arrived) {
				continue;
			}

			if (!actsOnArrival(task.type)) {
				// For non-actionable tasks like Wander, just clear when arrived
				task.clear();
				task.timeSinceEvaluation = 0.0F;
				continue;
			}

			// Start action if not already active
			if (!action.isActive()) {
				if (task.type == TaskType::Build || task.type == TaskType::Deconstruct) {
					startBuildAction(entity, task, action);
				} else {
					startAction(entity, task, action, position, memory, needs, inventory);
				}

				// Harvest is work-based, not a fixed time: the action is created with the
				// harvestable's `durability` (work units) in `duration`, which we convert to
				// seconds here using the colonist's level in the harvest entry's skill (time =
				// durability / rate). A skilled chopper fells a tree faster. Sampled once per chop;
				// other action types keep their authored seconds.
				if (action.type == ActionType::Harvest) {
					float harvestSkill = 0.0F;
					if (const auto* skills = world->getComponent<Skills>(entity)) {
						harvestSkill = skills->getLevel(actionEntries.harvest.skill);
					}
					action.duration /= harvestWorkRate(harvestSkill);
				}

				// Only log a genuine start. A start routine may decline to start an action (no
				// reachable pickup, or a chain handoff that re-points the task), leaving it None;
				// logging "Started None action" then is just noise.
				if (action.isActive()) {
					LOG_INFO(
						Engine,
						"[Action] Entity %llu: Started %s action (%.1fs duration)",
						static_cast<unsigned long long>(entity),
						actionTypeName(action.type),
						action.duration
					);
					if (!continuesWork(action)) {
						reportAction(entity, action, ActionMoment::Started, task, memory);
					}
				}

				// A start routine that starts nothing has either handed the task to its next leg (a haul
				// arriving at its source already carrying the item heads to the deposit, state Moving), or
				// found it cannot be done (the target gone from memory or no longer what it was, the tool
				// missing, a box packaged). A handoff drives itself on; anything else ends the task, so the
				// colonist decides again instead of standing on an Arrived task with no action.
				if (!action.isActive()) {
					if (task.state != TaskState::Moving) {
						LOG_DEBUG(
							Engine, "[Action] Entity %llu: nothing to start for its task (type %d) - ending it",
							static_cast<unsigned long long>(entity), static_cast<int>(task.type)
						);
						task.clear();
						task.timeSinceEvaluation = 0.0F;
					}
					continue;
				}
			}

			// Construction actions advance continuously by workDone, not by elapsed/duration.
			// A failed start (e.g. blueprint gone) clears the action, so guard on the effect.
			if (action.hasProgressEffect()) {
				advanceConstructionWork(scaledDt, action);
			} else {
				// Process the action
				processAction(scaledDt, action);
			}

			// Update WorkQueue progress for craft actions (for UI progress bar)
			if (action.hasCraftingEffect() && action.isActive()) {
				const auto& craftEff = action.craftingEffect();
				auto*		workQueue = world->getComponent<WorkQueue>(static_cast<EntityID>(craftEff.stationEntityId));
				if (workQueue != nullptr) {
					workQueue->progress = action.progress();
				}
			}

			// Complete action if done
			if (action.isComplete()) {
				float restoreAmount = 0.0F;
				if (action.hasNeedEffect()) {
					restoreAmount = action.needEffect().restoreAmount;
				} else if (action.hasConsumptionEffect()) {
					restoreAmount = action.consumptionEffect().restoreAmount;
				}
				LOG_INFO(
					Engine,
					"[Action] Entity %llu: Completed %s action (restored %.1f%%)",
					static_cast<unsigned long long>(entity),
					actionTypeName(action.type),
					restoreAmount
				);
				completeAction(entity, position, action, needs, task, inventory, memory);
			}
		}

		for (const ActionEvent& event : m_reportedActions) {
			for (const ActionObserver& observer : m_actionObservers) {
				observer(event);
			}
		}
		m_reportedActions.clear();
	}

	void ActionSystem::startAction(
		EntityID			  entity,
		Task&				  task,
		Action&				  action,
		const Position&		  position,
		Memory&				  memory,
		const NeedsComponent& needs,
		Inventory&			  inventory
	) {
		// Handle Craft tasks separately
		if (task.type == TaskType::Craft) {
			startCraftAction(task, action);
			return;
		}

		// Handle Haul tasks - pickup from source, then deposit to storage
		if (task.type == TaskType::Haul) {
			startHaulAction(entity, task, action, position, memory, inventory);
			return;
		}

		// Handle PlacePackaged tasks - pickup packaged item, then place at target
		if (task.type == TaskType::PlacePackaged) {
			startPlacePackagedAction(task, action, position, inventory);
			return;
		}

		// Handle Harvest tasks (goal-driven harvesting for crafting materials)
		if (task.type == TaskType::Harvest) {
			startHarvestAction(task, action, memory, inventory);
			return;
		}

		// Handle FulfillNeed tasks
		startNeedAction(task, action, position, memory, needs, inventory);
	}

	void ActionSystem::processAction(float deltaTime, Action& action) {
		// Update elapsed time
		action.elapsed += deltaTime;

		// Transition from Starting to InProgress
		if (action.state == ActionState::Starting) {
			action.state = ActionState::InProgress;
		}

		// Check for completion
		if (action.elapsed >= action.duration) {
			action.state = ActionState::Complete;
		}
	}

	void ActionSystem::completeAction(
		EntityID		entity,
		const Position& position,
		Action&			action,
		NeedsComponent& needs,
		Task&			task,
		Inventory&		inventory,
		Memory&			memory
	) {
		// What a cut or a pickup lands is what its collection adds to the colonist; what it held of the
		// item before is not this chain's.
		const bool		   cutLands = task.type == TaskType::Harvest && action.type == ActionType::Harvest;
		const bool		   haulLifts = task.type == TaskType::Haul && (action.type == ActionType::Pickup || action.type == ActionType::Withdraw);
		const std::string& yieldDefName = assets::AssetRegistry::Get().getDefName(cutLands ? task.harvestYieldDefNameId : 0U);
		const uint32_t	   heldBefore = cutLands	? ecs::availableQuantity(inventory, yieldDefName)
											: haulLifts ? ecs::availableQuantity(inventory, assets::AssetRegistry::Get().getDefName(task.haulItemDefNameId))
														: 0U;
		// Goods a pile pickup took over from earlier legs of a gathering ride along with what it lifts.
		const uint32_t carriedBefore = haulLifts ? std::min(taskCargo(task), heldBefore) : 0U;

		// Whether the action's effect took: only then has the colonist finished it. A need, a toilet, and a
		// construction bound reached always take; the rest can find their thing gone at completion.
		bool took = true;
		if (action.hasNeedEffect()) {
			applyNeedEffect(action, needs);
		}
		if (action.hasCollectionEffect()) {
			took = applyCollectionEffect(action, position.value, inventory, memory);
		}
		if (action.hasConsumptionEffect()) {
			took = applyConsumptionEffect(action, needs, inventory);
		}
		// Pooping creates a Bio Pile, peeing does not.
		if (action.spawnBioPile) {
			applySpawnEffect(action);
		}
		if (action.hasCraftingEffect()) {
			took = applyCraftingEffect(action, inventory);
		}
		if (action.hasDepositEffect()) {
			took = applyDepositEffect(action, inventory);
		}
		// advanceConstructionWork already moved workDone to the bound and set the phase; here we
		// signal the layer above ActionSystem (which doesn't know ConstructionWorld) via callback.
		if (action.hasProgressEffect()) {
			applyProgressEffect(action);
		}
		if (action.type == ActionType::PickupPackaged) {
			const auto* packaged = world->getComponent<Packaged>(static_cast<EntityID>(action.placePackagedEffect().packagedEntityId));
			took = packaged != nullptr && !packaged->beingCarried;
		}
		if (action.type == ActionType::PlacePackaged) {
			took = applyPlacePackagedEffect(action, inventory);
		}
		if (took) {
			reportAction(entity, action, ActionMoment::Finished, task, memory);
		}

		// Special handling for Haul tasks - may need to continue to deposit phase
		// NOTE: This intentionally returns early WITHOUT clearing the task. Haul is a
		// two-phase task (Pickup→Deposit), so after phase 1 we set up phase 2 and return.
		// The action is cleared but the task persists. This differs from single-phase
		// tasks that clear both action and task at the end of this function.
		if (haulLifts) {
			// Of food, only what this pickup lifted is cargo; what the colonist already had is its own
			// provisions. Anything else it holds of the item rides along.
			const auto&	   registry = assets::AssetRegistry::Get();
			const uint32_t held = ecs::availableQuantity(inventory, registry.getDefName(task.haulItemDefNameId));
			const auto*	   itemDef = registry.getDefinitionById(task.haulItemDefNameId);
			const uint32_t cargo =
				itemDef != nullptr && itemDef->isEdible() ? (held > heldBefore ? held - heldBefore : 0U) + carriedBefore : held;
			// Nothing came off the source (gone, emptied by another hauler, or no room to take it):
			// there is nothing to deliver, so the haul ends here instead of walking to storage empty.
			if (cargo == 0) {
				LOG_DEBUG(
					Engine,
					"[Action] Haul pickup of %s at (%.1f, %.1f) collected nothing - ending the haul",
					registry.getDefName(task.haulItemDefNameId).c_str(),
					task.haulSourcePosition.x,
					task.haulSourcePosition.y
				);
				action.clear();
				task.clear();
				task.timeSinceEvaluation = 0.0F;
				return;
			}

			// The source claim is spent; the cargo is what the deposit leg commits.
			claimsToDeliveryLeg(task, cargo);

			// Phase 1 complete (Pickup, or Withdraw for a storage->storage pull): the same task carries on
			// as its deposit leg, so AIDecisionSystem keeps it (isSameTask) and never reselects it.
			headToDeposit(entity, task);
			if (carriedBefore > 0) {
				// A leg of a gathering: decided again at once, as after a cut (task-claims.md, Batching).
				requestDecision(task);
			}

			action.clear();
			LOG_DEBUG(
				Engine,
				"[Action] Haul phase 1 complete (chain step %u), moving to storage at (%.1f, %.1f)",
				task.chainStep,
				task.haulTargetPosition.x,
				task.haulTargetPosition.y
			);
			return;
		}

		// Special handling for PlacePackaged tasks - two-phase (PickupPackaged→PlacePackaged)
		if (task.type == TaskType::PlacePackaged && action.type == ActionType::PickupPackaged) {
			const auto& placeEffect = action.placePackagedEffect();
			const auto	packagedEntity = static_cast<EntityID>(placeEffect.packagedEntityId);
			// A package gone, placed, or in another colonist's hands is not there to lift: the task ends
			// with the colonist's hands as they were.
			if (!took) {
				LOG_DEBUG(
					Engine, "[Action] Packaged entity %llu is not there to pick up - ending the task", static_cast<unsigned long long>(packagedEntity)
				);
				action.clear();
				task.clear();
				task.timeSinceEvaluation = 0.0F;
				return;
			}

			// Phase 1 complete (PickupPackaged) - clear hands, pick up entity, then move to phase 2
			// Hand clearing: stow items to backpack, drop if can't fit
			clearHandsForTwoHandedPickup(inventory, action.targetPosition);

			// The packaged item is possessed now; nobody else can take it.
			claimsToDeliveryLeg(task, 0);

			// Mark the entity as being carried (hides from world rendering)
			world->getComponent<Packaged>(packagedEntity)->beingCarried = true;
			inventory.carryingPackagedEntity = placeEffect.packagedEntityId;

			// Put the entity's defName in both hands (for UI display)
			auto* appearance = world->getComponent<Appearance>(packagedEntity);
			if (appearance != nullptr && !appearance->defName.empty()) {
				inventory.leftHand = ItemStack{appearance->defName, 1};
				inventory.rightHand = ItemStack{appearance->defName, 1};
				LOG_INFO(
					Engine,
					"[Action] Picked up %s (entity %llu) - now carrying in both hands",
					appearance->defName.c_str(),
					static_cast<unsigned long long>(placeEffect.packagedEntityId)
				);
			} else {
				LOG_WARNING(
					Engine,
					"[Action] Could not set hands: appearance=%p, defName=%s",
					static_cast<void*>(appearance),
					appearance ? appearance->defName.c_str() : "(null)"
				);
			}

			// Transition to phase 2 - walk to placement target.
			headTo(entity, task, task.placeTargetPosition);
			task.chainStep++; // Advance chain: step 0 (PickupPackaged) → step 1 (Place)

			action.clear();
			LOG_DEBUG(
				Engine,
				"[Action] PlacePackaged phase 1 complete (chain step %u), moving to target at (%.1f, %.1f)",
				task.chainStep,
				task.placeTargetPosition.x,
				task.placeTargetPosition.y
			);
			return;
		}

		if (cutLands) {
			const uint32_t heldAfterCut = ecs::availableQuantity(inventory, yieldDefName);
			if (carryHarvestIn(entity, position, task, inventory, heldAfterCut > heldBefore ? heldAfterCut - heldBefore : 0U)) {
				action.clear();
				return;
			}
		}

		// Clear the action and task
		action.clear();
		task.clear();
		task.timeSinceEvaluation = 0.0F;
	}

	void ActionSystem::reportAction(EntityID entity, const Action& action, ActionMoment moment, const Task& task, const Memory& memory) {
		if (m_actionObservers.empty()) {
			return;
		}
		const auto& registry = assets::AssetRegistry::Get();
		uint32_t	about = 0;
		if (action.hasConsumptionEffect()) {
			about = registry.getDefNameId(action.consumptionEffect().itemDefName);
		} else if (action.hasCraftingEffect() && !action.craftingEffect().outputs.empty()) {
			about = registry.getDefNameId(action.craftingEffect().outputs.front().first);
		} else if (action.hasCollectionEffect()) {
			about = registry.getDefNameId(action.collectionEffect().sourceDefName);
		} else if (action.hasDepositEffect()) {
			about = registry.getDefNameId(action.depositEffect().itemDefName);
		} else if (const KnownWorldEntity* source = memory.getWorldEntity(task.needSourceKey); source != nullptr) {
			about = source->defNameId;
		}
		m_reportedActions.push_back(
			{.colonist = entity, .type = action.type, .action = action.entry, .moment = moment, .aboutDefinition = about}
		);
	}

	bool ActionSystem::continuesWork(const Action& action) const {
		if (action.hasProgressEffect()) {
			const ProgressEffect&	  effect = action.progressEffect();
			const StructureBlueprint* blueprint = world->getComponent<StructureBlueprint>(static_cast<EntityID>(effect.targetEntityId));
			if (blueprint == nullptr) {
				return false;
			}
			return effect.deconstruct ? blueprint->teardownBegun : blueprint->workDone > 0.0F;
		}
		if (action.hasCraftingEffect()) {
			const CraftingEffect& effect = action.craftingEffect();
			const WorkQueue*	  queue = world->getComponent<WorkQueue>(static_cast<EntityID>(effect.stationEntityId));
			const CraftingJob*	  job = queue != nullptr ? queue->getNextJob() : nullptr;
			return job != nullptr && job->recipeDefName == effect.recipeDefName && job->completed > 0;
		}
		return false;
	}

	void ActionSystem::headToDeposit(EntityID entity, Task& task) {
		headTo(entity, task, task.haulTargetPosition);
		task.chainStep = 1;
	}

	void ActionSystem::headTo(EntityID entity, Task& task, glm::vec2 target) {
		// A route still out was asked for the leg just finished.
		if (task.routeTicket != 0) {
			if (auto* nav = world->tryGetSystem<NavigationSystem>()) {
				nav->cancelRoute(task.routeTicket);
			}
			task.routeTicket = 0;
		}
		// Moving with an active target and no route: the AI asks for the route to the new target, so the
		// colonist never slides straight at it, and the old leg's route can't carry him the wrong way.
		task.targetPosition = target;
		task.obstructedTarget.reset();
		task.state			= TaskState::Moving;
		if (auto* movementTarget = world->getComponent<MovementTarget>(entity)) {
			movementTarget->target = target;
			movementTarget->active = true;
		}
		if (auto* navPath = world->getComponent<NavPath>(entity)) {
			navPath->valid = false;
		}
	}

	bool ActionSystem::carryHarvestIn(EntityID entity, const Position& position, Task& task, const Inventory& inventory, uint32_t landed) {
		// The in-flight claim taken at acceptance names the root, which outlives the Harvest child the
		// cut was picked through (a goal system may retire it while the colonist works).
		uint64_t rootId = 0;
		uint32_t accepted = 0;
		for (const Claim& claim : task.claims) {
			if (claim.kind() == ClaimKind::InFlight) {
				rootId = claim.target();
				accepted = claim.quantity();
			}
		}
		const auto&		goals = GoalTaskRegistry::Get();
		const GoalTask* root = goals.getGoal(rootId);
		if (root == nullptr) {
			return false;
		}
		auto&			   registry = assets::AssetRegistry::Get();
		const uint32_t	   item = task.harvestYieldDefNameId;
		const std::string& itemDefName = registry.getDefName(item);
		// The cargo is what earlier cuts of this gathering carry plus what this one landed; anything else
		// of the yield the colonist holds is not this task's to deliver.
		const uint32_t cargo = std::min(cargoOnClaims(task.claims, rootId, item) + landed, ecs::availableQuantity(inventory, itemDefName));
		if (cargo == 0) {
			return false;
		}
		// The index is the one the AI last built, so this commits no more than this task's own in-flight
		// there: another colonist converting a cut in this same update read that figure, and the two
		// together never pass the need. The need itself is read now, past every deposit already landed.
		static const ClaimIndex kNoClaims;
		const auto*				ai = world->tryGetSystem<AIDecisionSystem>();
		const uint32_t			quantity = std::min(
			 {cargo, accepted,
			  rootAvailable(goals.remainingNeed(*world, rootId, item, harvestBasis(*root)), ai != nullptr ? ai->claimIndex() : kNoClaims, rootId,
							item, ClaimExclusion::of(task))}
		 );
		if (quantity == 0) {
			return false;
		}

		task.type = TaskType::Haul;
		task.haulItemDefNameId = item;
		task.haulQuantity = quantity;
		task.haulFromInventory = true;
		task.haulSourceStorageId = 0;
		task.haulSourcePosition = position.value;
		task.haulDestinationId = static_cast<uint64_t>(root->destinationEntity);
		task.haulTargetPosition = root->destinationPosition;
		task.haulGoalId = rootId;
		task.harvestTargetKey = 0;
		task.harvestGoalId = 0;
		task.harvestYieldDefNameId = 0;
		task.reason = "Delivering " + itemDefName;
		harvestToDeliveryLeg(task, rootId, root->destinationEntity, item, quantity);
		headToDeposit(entity, task);
		// Decided again at once rather than after a step toward the destination: a further cut may come
		// first (task-claims.md, Batching).
		requestDecision(task);
		LOG_DEBUG(
			Engine,
			"[Action] Harvest landed %u x %s; carrying %u toward goal %llu",
			landed,
			itemDefName.c_str(),
			quantity,
			static_cast<unsigned long long>(rootId)
		);
		return true;
	}

} // namespace ecs
