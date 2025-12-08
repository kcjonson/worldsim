#include "ActionSystem.h"

#include "../World.h"
#include "../components/Action.h"
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

/// Find nutrition value for the nearest edible entity at target position
[[nodiscard]] float findNutritionAtTarget(const Memory& memory, const glm::vec2& targetPos) {
	auto& registry = engine::assets::AssetRegistry::Get();

	// Look through edible entities to find one at the target position
	const auto& edibleKeys = memory.getEntitiesWithCapability(engine::assets::CapabilityType::Edible);

	constexpr float kPositionTolerance = 0.5F;

	for (uint64_t key : edibleKeys) {
		const auto* entity = memory.getWorldEntity(key);
		if (entity == nullptr) {
			continue;
		}

		// Check if this entity is at the target position
		float dx = entity->position.x - targetPos.x;
		float dy = entity->position.y - targetPos.y;
		if ((dx * dx + dy * dy) < (kPositionTolerance * kPositionTolerance)) {
			// Found entity at target - get its nutrition value
			const std::string& defName = registry.getDefName(entity->defNameId);
			const auto* assetDef = registry.getDefinition(defName);
			if (assetDef != nullptr && assetDef->capabilities.edible.has_value()) {
				return assetDef->capabilities.edible->nutrition;
			}
		}
	}

	return kDefaultNutrition;
}

} // namespace

void ActionSystem::update(float deltaTime) {
	// Process all colonists with the required components
	for (auto [entity, position, task, action, needs, memory] :
		 world->view<Position, Task, Action, NeedsComponent, Memory>()) {

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
			startAction(task, action, position, memory);
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
	const Memory& memory
) {
	switch (task.needToFulfill) {
		case NeedType::Hunger: {
			// Find nutrition value from the target entity
			float nutrition = findNutritionAtTarget(memory, task.targetPosition);
			action = Action::Eat(nutrition);
			break;
		}

		case NeedType::Thirst: {
			// Water is tile-based, use default quality
			action = Action::Drink(kDefaultWaterQuality);
			break;
		}

		case NeedType::Energy: {
			// Check if at current position (ground fallback) or at a bed
			bool isGroundFallback = (task.targetPosition == position.value);
			float quality = isGroundFallback ? kGroundSleepQuality : 1.0F;
			action = Action::Sleep(quality);
			break;
		}

		case NeedType::Bladder: {
			// Use current position for Bio Pile spawn location
			action = Action::Toilet(position.value);
			break;
		}

		case NeedType::Count:
			// Invalid need type - shouldn't happen
			LOG_ERROR(Engine, "[Action] Invalid need type in task");
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

	// Handle spawn effects (e.g., Toilet creates Bio Pile)
	if (action.type == ActionType::Toilet) {
		// TODO: Spawn Bio Pile entity at action.targetPosition
		// For MVP, just log that it would happen
		LOG_DEBUG(
			Engine,
			"[Action] Would spawn Bio Pile at (%.1f, %.1f)",
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
