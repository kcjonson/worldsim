#include "ActionSystem.h"

#include "../World.h"
#include "TimeSystem.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Packaged.h"
#include "../components/Skills.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <utils/Log.h>

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

			// Only process actionable tasks (FulfillNeed, Gather, Craft, Haul, PlacePackaged,
			// Harvest, Build, Deconstruct)
			if (task.type != TaskType::FulfillNeed && task.type != TaskType::Gather && task.type != TaskType::Craft &&
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
					startAction(task, action, position, memory, needs, inventory);
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

				LOG_INFO(
					Engine,
					"[Action] Entity %llu: Started %s action (%.1fs duration)",
					static_cast<unsigned long long>(entity),
					actionTypeName(action.type),
					action.duration
				);
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
				}
				LOG_INFO(
					Engine,
					"[Action] Entity %llu: Completed %s action (restored %.1f%%)",
					static_cast<unsigned long long>(entity),
					actionTypeName(action.type),
					restoreAmount
				);
				completeAction(entity, action, needs, task, inventory, memory);
			}
		}
	}

	void ActionSystem::startAction(
		Task&				  task,
		Action&				  action,
		const Position&		  position,
		Memory&				  memory,
		const NeedsComponent& needs,
		const Inventory&	  inventory
	) {
		// Handle Craft tasks separately
		if (task.type == TaskType::Craft) {
			startCraftAction(task, action, inventory);
			return;
		}

		// Handle Gather tasks - pickup or harvest materials for crafting
		if (task.type == TaskType::Gather) {
			startGatherAction(task, action, position, memory);
			return;
		}

		// Handle Haul tasks - pickup from source, then deposit to storage
		if (task.type == TaskType::Haul) {
			startHaulAction(task, action, position, memory);
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

	void ActionSystem::completeAction(EntityID entity, Action& action, NeedsComponent& needs, Task& task, Inventory& inventory, Memory& memory) {
		// Apply effects based on variant type
		if (action.hasNeedEffect()) {
			applyNeedEffect(action, needs);
		}

		// Handle collection effects (Pickup, Harvest)
		if (action.hasCollectionEffect()) {
			applyCollectionEffect(action, task, inventory, memory);
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
			applyDepositEffect(action, task, inventory);
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
		if (task.type == TaskType::Haul && action.type == ActionType::Pickup) {
			// Phase 1 complete (Pickup) - move to phase 2 (Deposit)
			task.targetPosition = task.haulTargetPosition;
			task.state = TaskState::Pending;
			task.chainStep++; // Advance chain: step 0 (Pickup) → step 1 (Deposit)
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

			// Transition to phase 2 - walk to placement target
			task.targetPosition = task.placeTargetPosition;
			task.state = TaskState::Moving;  // Must be Moving for MovementSystem to set Arrived
			task.chainStep++; // Advance chain: step 0 (PickupPackaged) → step 1 (Place)

			// Update MovementTarget to the new destination
			// This is critical - without this, the movement system won't know where to go
			auto* movementTarget = world->getComponent<MovementTarget>(entity);
			if (movementTarget != nullptr) {
				movementTarget->target = task.placeTargetPosition;
				movementTarget->active = true;
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

		// Handle PlacePackaged completion (phase 2) - move entity to target and remove Packaged component
		if (action.type == ActionType::PlacePackaged && action.hasPlacePackagedEffect()) {
			applyPlacePackagedEffect(action, inventory);
		}

		// Clear the action and task
		action.clear();
		task.clear();
		task.timeSinceEvaluation = 0.0F;
	}

} // namespace ecs
