#include "ActionSystem.h"

#include "../World.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Memory.h"
#include "../components/MemoryQueries.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include "assets/AssetRegistry.h"

#include <utils/Log.h>

namespace ecs {

	namespace {

		/// Default ground quality for sleep (reduced recovery rate)
		constexpr float kGroundSleepQuality = 0.5F;

		/// Default water quality for drinking
		constexpr float kDefaultWaterQuality = 1.0F;

		/// Default nutrition when no edible entity found (shouldn't happen in practice)
		constexpr float kDefaultNutrition = 0.3F;

	} // namespace

	void ActionSystem::update(float deltaTime) {
		// Process all colonists with the required components.
		//
		// Action Interruption Policy: Once an action starts, it runs to completion.
		// If a colonist's task changes mid-action (e.g., AIDecisionSystem assigns a
		// higher-priority need), the action stops being processed but remains in an
		// incomplete state until the colonist returns and the task state becomes
		// Arrived again. This is intentional - colonists "commit" to actions rather
		// than abandoning half-eaten food or interrupted sleep. Future work may add
		// explicit action cancellation for emergencies (e.g., flee from danger).
		for (auto [entity, position, task, action, needs, memory] : world->view<Position, Task, Action, NeedsComponent, Memory>()) {

			// Only process entities that have arrived at their destination
			if (task.state != TaskState::Arrived) {
				continue;
			}

			// Only process FulfillNeed tasks (wander doesn't trigger actions)
			if (task.type != TaskType::FulfillNeed) {
				// For non-need tasks like Wander, just clear when arrived
				task.clear();
				task.timeSinceEvaluation = 0.0F;
				continue;
			}

			// Start action if not already active
			if (!action.isActive()) {
				startAction(task, action, position, memory, needs);
				LOG_INFO(
					Engine,
					"[Action] Entity %llu: Started %s action (%.1fs duration)",
					static_cast<unsigned long long>(entity),
					actionTypeName(action.type),
					action.duration
				);
			}

			// Process the action
			processAction(deltaTime, action, needs, task);

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
				completeAction(action, needs, task);
			}
		}
	}

	void ActionSystem::startAction(
		Task& task,
		Action& action,
		const Position& position,
		const Memory& memory,
		const NeedsComponent& needs
	) {
		auto& registry = engine::assets::AssetRegistry::Get();

		switch (task.needToFulfill) {
			case NeedType::Hunger: {
				// Find nutrition value from the target entity using MemoryQueries
				auto  maybeNutrition = findNutritionAtPosition(memory, registry, task.targetPosition);
				float nutrition = kDefaultNutrition;
				if (maybeNutrition.has_value()) {
					nutrition = maybeNutrition.value();
				} else {
					LOG_WARNING(
						Engine,
						"[Action] No edible entity found at target (%.1f, %.1f), using default nutrition",
						task.targetPosition.x,
						task.targetPosition.y
					);
				}
				action = Action::Eat(nutrition);
				break;
			}

			case NeedType::Thirst: {
				// Water tiles are inexhaustible - drinking fully restores thirst
				action = Action::Drink();
				break;
			}

			case NeedType::Energy: {
				// Check if at current position (ground fallback) or at a bed
				bool  isGroundFallback = (task.targetPosition == position.value);
				float quality = isGroundFallback ? kGroundSleepQuality : 1.0F;
				action = Action::Sleep(quality);
				break;
			}

			case NeedType::Bladder:
			case NeedType::Digestion: {
				// Smart Toilet: check both bladder and digestion needs
				// If both need attention, handle both efficiently
				bool needsPee = needs.bladder().needsAttention();
				bool needsPoop = needs.digestion().needsAttention();

				// If the task was for one specific need but the other doesn't need attention,
				// still do at least what was asked
				if (task.needToFulfill == NeedType::Bladder && !needsPee) {
					needsPee = true; // Do what was asked
				}
				if (task.needToFulfill == NeedType::Digestion && !needsPoop) {
					needsPoop = true; // Do what was asked
				}

				action = Action::Toilet(position.value, needsPee, needsPoop);
				break;
			}

			case NeedType::Count:
				// Invalid need type - shouldn't happen
				LOG_ERROR(Engine, "[Action] Invalid need type in task: %d", static_cast<int>(task.needToFulfill));
				break;
		}
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

	void ActionSystem::completeAction(Action& action, NeedsComponent& needs, Task& task) {
		// Apply effects based on variant type
		if (action.hasNeedEffect()) {
			const auto& needEff = action.needEffect();

			// Apply primary need restoration
			if (needEff.need < NeedType::Count) {
				needs.get(needEff.need).restore(needEff.restoreAmount);
			}

			// Apply side effect (if any)
			if (needEff.sideEffectNeed < NeedType::Count) {
				// Side effect amount: positive = restore, negative = drain
				if (needEff.sideEffectAmount > 0.0F) {
					needs.get(needEff.sideEffectNeed).restore(needEff.sideEffectAmount);
				} else {
					// Negative amount means drain (e.g., drinking fills bladder)
					auto& need = needs.get(needEff.sideEffectNeed);
					need.value += needEff.sideEffectAmount; // sideEffectAmount is already negative
					if (need.value < 0.0F) {
						need.value = 0.0F;
					}
				}
			}
		}

		// Handle spawn effects (pooping creates Bio Pile, peeing does not)
		if (action.spawnBioPile) {
			// Spawn Bio Pile entity at action.targetPosition
			auto bioPile = world->createEntity();
			world->addComponent<Position>(bioPile, Position{action.targetPosition});
			world->addComponent<Rotation>(bioPile, Rotation{0.0F});
			world->addComponent<Appearance>(bioPile, Appearance{"Misc_BioPile", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			LOG_INFO(
				Engine,
				"[Action] Spawned Bio Pile at (%.1f, %.1f)",
				action.targetPosition.x,
				action.targetPosition.y
			);
		}

		// Clear the action and task
		action.clear();
		task.clear();
		task.timeSinceEvaluation = 0.0F;
	}

} // namespace ecs
