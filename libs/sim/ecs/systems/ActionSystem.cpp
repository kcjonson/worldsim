#include "ActionSystem.h"

#include "AIDecisionSystem.h"
#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "ChainClaims.h"
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
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <utils/Log.h>

#include <limits>

namespace ecs {

	void ActionSystem::update(float deltaTime) {
		// Actions advance on game time, so fast-forward (3x/10x) speeds up work and pause
		// freezes it. TimeSystem is always registered in-game; unit tools/tests that omit it
		// fall back to real time (scale 1.0), keeping action durations equal to seconds there.
		float timeScale = 1.0F;
		if (auto* timeSystem = world->tryGetSystem<TimeSystem>()) {
			timeScale = timeSystem->effectiveTimeScale();
		}
		const float scaledDt = deltaTime * timeScale;

		// Process all colonists with the required components.
		//
		// Action Interruption Policy: Once an action starts, it runs to completion.
		// If a colonist's task changes mid-action (e.g., AIDecisionSystem assigns a
		// higher-priority need), the action stops being processed but remains in an
		// incomplete state until the colonist returns and the task state becomes
		// Arrived again. This is intentional - colonists "commit" to actions rather
		// than abandoning half-eaten food or interrupted sleep. Future work may add
		// explicit action cancellation for emergencies (e.g., flee from danger).
		for (auto [entity, position, task, action, needs, memory, inventory] :
			 world->view<Position, Task, Action, NeedsComponent, Memory, Inventory>()) {

			// Only process entities that have arrived at their destination
			if (task.state != TaskState::Arrived) {
				continue;
			}

			// Only process actionable tasks (FulfillNeed, Craft, Haul, PlacePackaged,
			// Harvest, Build, Deconstruct)
			if (task.type != TaskType::FulfillNeed && task.type != TaskType::Craft &&
				task.type != TaskType::Haul && task.type != TaskType::PlacePackaged && task.type != TaskType::Harvest &&
				task.type != TaskType::Build && task.type != TaskType::Deconstruct) {
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
				// seconds here using the colonist's Harvesting skill (time = durability / rate).
				// A skilled chopper fells a tree faster. Sampled once per chop; other action
				// types keep their authored seconds.
				if (action.type == ActionType::Harvest) {
					float harvestSkill = 0.0F;
					if (const auto* skills = world->getComponent<Skills>(entity)) {
						harvestSkill = skills->getLevel("Harvesting");
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
				}

				// Start-time chain handoff: a start routine can decline to start an action and
				// instead re-point the task to its next leg (state flipped back to Moving), e.g. a
				// haul that arrives at the SOURCE already carrying the item redirects to the deposit
				// leg. The task must then drive itself to the new target; there is no action to
				// process or complete this tick. Bail before the process/complete block below, which
				// would otherwise "complete" the inactive (None) action and clear the task -- the
				// behavior the OTHER no-action paths (pickup target gone) intentionally rely on to
				// recover a stuck task. Gate strictly on Moving so only a deliberate handoff skips;
				// a stall leaves the task Arrived and still falls through to that recovery.
				if (!action.isActive() && task.state == TaskState::Moving) {
					continue;
				}
			}

			// Construction actions advance continuously by workDone, not by elapsed/duration.
			// A failed start (e.g. blueprint gone) clears the action, so guard on the effect.
			if (action.hasProgressEffect()) {
				advanceConstructionWork(scaledDt, action);
			} else {
				// Process the action
				processAction(scaledDt, action, needs, task);
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
			startHarvestAction(task, action, position, memory, inventory);
			return;
		}

		// Handle FulfillNeed tasks
		startNeedAction(task, action, position, memory, needs, inventory);
	}

	void ActionSystem::processAction(float deltaTime, Action& action, NeedsComponent& needs, Task& task) {
		(void)needs; // Not used during in-progress phase
		(void)task;	 // Not used during in-progress phase

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
											: haulLifts ? ecs::availableQuantity(inventory, task.haulItemDefName)
														: 0U;

		// Apply effects based on variant type
		if (action.hasNeedEffect()) {
			applyNeedEffect(action, needs);
		}

		// Handle collection effects (Pickup, Harvest)
		if (action.hasCollectionEffect()) {
			applyCollectionEffect(action, position.value, inventory, memory);
		}

		// Handle consumption effects (Eat action)
		if (action.hasConsumptionEffect()) {
			applyConsumptionEffect(action, needs, inventory);
		}

		// Handle spawn effects (pooping creates Bio Pile, peeing does not)
		if (action.spawnBioPile) {
			applySpawnEffect(action);
		}

		// Handle crafting effects
		if (action.hasCraftingEffect()) {
			applyCraftingEffect(action, inventory);
		}

		// Handle deposit effects (put items into storage)
		if (action.hasDepositEffect()) {
			applyDepositEffect(action, inventory);
		}

		// Handle construction progress effects (Build, Deconstruct).
		// advanceConstructionWork already moved workDone to the bound and set the phase; here we
		// signal the layer above ActionSystem (which doesn't know ConstructionWorld) via callback.
		if (action.hasProgressEffect()) {
			applyProgressEffect(action);
		}

		// Special handling for Haul tasks - may need to continue to deposit phase
		// NOTE: This intentionally returns early WITHOUT clearing the task. Haul is a
		// two-phase task (Pickup→Deposit), so after phase 1 we set up phase 2 and return.
		// The action is cleared but the task persists. This differs from single-phase
		// tasks that clear both action and task at the end of this function.
		if (haulLifts) {
			// Of food, only what this pickup lifted is cargo; what the colonist already had is its own
			// provisions. Anything else it holds of the item rides along.
			const uint32_t held = ecs::availableQuantity(inventory, task.haulItemDefName);
			const auto*	   itemDef = assets::AssetRegistry::Get().getDefinition(task.haulItemDefName);
			const uint32_t cargo = itemDef != nullptr && itemDef->isEdible() ? (held > heldBefore ? held - heldBefore : 0U) : held;
			// Nothing came off the source (gone, emptied by another hauler, or no room to take it):
			// there is nothing to deliver, so the haul ends here instead of walking to storage empty.
			if (cargo == 0) {
				LOG_DEBUG(
					Engine,
					"[Action] Haul pickup of %s at (%.1f, %.1f) collected nothing - ending the haul",
					task.haulItemDefName.c_str(),
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
			// Phase 1 complete (PickupPackaged) - clear hands, pick up entity, then move to phase 2
			// Hand clearing: stow items to backpack, drop if can't fit
			clearHandsForTwoHandedPickup(inventory, action.targetPosition);

			// The packaged item is possessed now; nobody else can take it.
			claimsToDeliveryLeg(task, 0);

			// Get the packaged entity from the effect
			if (action.hasPlacePackagedEffect()) {
				const auto& placeEff = action.placePackagedEffect();
				auto		packagedEntity = static_cast<EntityID>(placeEff.packagedEntityId);

				// Mark the entity as being carried (hides from world rendering)
				auto* packaged = world->getComponent<Packaged>(packagedEntity);
				if (packaged != nullptr) {
					packaged->beingCarried = true;
				}

				// Track the carried entity in inventory
				inventory.carryingPackagedEntity = placeEff.packagedEntityId;

				// Put the entity's defName in both hands (for UI display)
				auto* appearance = world->getComponent<Appearance>(packagedEntity);
				if (appearance != nullptr && !appearance->defName.empty()) {
					inventory.leftHand = ItemStack{appearance->defName, 1};
					inventory.rightHand = ItemStack{appearance->defName, 1};
					LOG_INFO(
						Engine,
						"[Action] Picked up %s (entity %llu) - now carrying in both hands",
						appearance->defName.c_str(),
						static_cast<unsigned long long>(placeEff.packagedEntityId)
					);
				} else {
					LOG_WARNING(
						Engine,
						"[Action] Could not set hands: appearance=%p, defName=%s",
						static_cast<void*>(appearance),
						appearance ? appearance->defName.c_str() : "(null)"
					);
				}
			}

			// Transition to phase 2 - walk to placement target. Re-point the task at the place
			// GOAL and set it Moving; the AI's per-tick chain-leg repath (a Moving task with an
			// active target but no live route) requests a navmesh path, and the colonist follows
			// that A* route to the bench. Invalidate the pickup-leg route, which still points at
			// the source, so the stale path can't carry the colonist the wrong way for a frame.
			task.targetPosition = task.placeTargetPosition;
			task.state = TaskState::Moving;  // Must be Moving for MovementSystem to set Arrived
			task.chainStep++; // Advance chain: step 0 (PickupPackaged) → step 1 (Place)

			auto* movementTarget = world->getComponent<MovementTarget>(entity);
			if (movementTarget != nullptr) {
				movementTarget->target = task.placeTargetPosition;
				movementTarget->active = true;
			}
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}

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

		// Handle PlacePackaged completion (phase 2) - move entity to target and remove Packaged component
		if (action.type == ActionType::PlacePackaged && action.hasPlacePackagedEffect()) {
			applyPlacePackagedEffect(action, inventory);
		}

		// Clear the action and task
		action.clear();
		task.clear();
		task.timeSinceEvaluation = 0.0F;
	}

	void ActionSystem::headToDeposit(EntityID entity, Task& task) {
		// Moving with an active target and no live route: the AI's chain-leg repath plans the A* route
		// to the destination, so the colonist never slides straight at it.
		task.targetPosition = task.haulTargetPosition;
		task.state = TaskState::Moving;
		task.chainStep = 1;
		if (auto* movementTarget = world->getComponent<MovementTarget>(entity)) {
			movementTarget->target = task.haulTargetPosition;
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
		task.haulItemDefName = itemDefName;
		task.haulQuantity = quantity;
		task.haulFromInventory = true;
		task.haulSourceStorageId = 0;
		task.haulSourcePosition = position.value;
		task.haulTargetStorageId = static_cast<uint64_t>(root->destinationEntity);
		task.haulTargetPosition = root->destinationPosition;
		task.haulGoalId = rootId;
		task.harvestTargetEntityId = 0;
		task.harvestGoalId = 0;
		task.harvestYieldDefNameId = 0;
		task.reason = "Delivering " + itemDefName;
		harvestToDeliveryLeg(task, rootId, root->destinationEntity, item, quantity);
		headToDeposit(entity, task);
		// Decided again at once rather than after a step toward the destination: a further cut may come
		// first (task-claims.md, Batching).
		task.timeSinceEvaluation = std::numeric_limits<float>::max();
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
