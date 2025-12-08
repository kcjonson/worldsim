#pragma once

// Action Component for Colonist Need Fulfillment
// Tracks actions being performed when colonist arrives at target.
// Actions have duration and apply effects when complete.
// See /docs/design/game-systems/colonists/needs.md for design details.

#include "Needs.h"

#include <glm/vec2.hpp>

#include <cstdint>

namespace ecs {

/// Action types that colonists can perform
enum class ActionType : uint8_t {
	None = 0,
	Eat,	// Consuming Edible entity (Berry Bush)
	Drink,	// Drinking from water tile (Pond)
	Sleep,	// Sleeping on ground or bed
	Toilet	// Using toilet or ground relief
};

/// Action state machine
enum class ActionState : uint8_t {
	Starting,	// Action just began, initialization frame
	InProgress, // Action is ongoing
	Complete	// Action finished, ready for cleanup
};

/// Action component - tracks a colonist's current action
struct Action {
	ActionType	type = ActionType::None;
	ActionState state = ActionState::Starting;

	/// The need this action is fulfilling
	NeedType needToFulfill = NeedType::Count;

	/// Duration of the action in seconds (varies by action type)
	float duration = 0.0F;

	/// Elapsed time in this action (seconds)
	float elapsed = 0.0F;

	/// Amount to restore the need by (calculated from entity nutrition, etc.)
	float restoreAmount = 0.0F;

	/// Side effect: amount to add to another need (e.g., drinking adds to bladder)
	float sideEffectAmount = 0.0F;

	/// Target position for actions that spawn entities (Bio Pile)
	glm::vec2 spawnPosition{0.0F, 0.0F};

	/// Check if an action is currently in progress
	[[nodiscard]] bool isActive() const { return type != ActionType::None; }

	/// Check if action is complete
	[[nodiscard]] bool isComplete() const { return state == ActionState::Complete; }

	/// Get progress as 0.0 - 1.0
	[[nodiscard]] float progress() const {
		if (duration <= 0.0F) {
			return 1.0F;
		}
		return elapsed / duration;
	}

	/// Reset action to default state
	void clear() {
		type = ActionType::None;
		state = ActionState::Starting;
		needToFulfill = NeedType::Count;
		duration = 0.0F;
		elapsed = 0.0F;
		restoreAmount = 0.0F;
		sideEffectAmount = 0.0F;
		spawnPosition = glm::vec2{0.0F, 0.0F};
	}

	/// Factory: Eat action
	/// @param nutrition Amount of hunger to restore (0.0-1.0 maps to 0-100%)
	static Action Eat(float nutrition) {
		Action action;
		action.type = ActionType::Eat;
		action.state = ActionState::Starting;
		action.needToFulfill = NeedType::Hunger;
		action.duration = 2.0F; // 2 seconds to eat
		action.restoreAmount = nutrition * 100.0F;
		return action;
	}

	/// Factory: Drink action
	/// @param quality Water quality (affects thirst restoration)
	static Action Drink(float quality = 1.0F) {
		Action action;
		action.type = ActionType::Drink;
		action.state = ActionState::Starting;
		action.needToFulfill = NeedType::Thirst;
		action.duration = 1.5F;					// 1.5 seconds to drink
		action.restoreAmount = 40.0F * quality; // Base 40% thirst restoration
		action.sideEffectAmount = 15.0F;		// Drinking adds 15% to bladder
		return action;
	}

	/// Factory: Sleep action
	/// @param quality Sleep quality affects restoration rate (0.5 for ground, 1.0 for bed)
	static Action Sleep(float quality = 0.5F) {
		Action action;
		action.type = ActionType::Sleep;
		action.state = ActionState::Starting;
		action.needToFulfill = NeedType::Energy;
		action.duration = 8.0F;					// 8 seconds of sleep (game time scaled)
		action.restoreAmount = 60.0F * quality; // Quality affects restoration
		return action;
	}

	/// Factory: Toilet action
	/// @param position Position to spawn Bio Pile
	static Action Toilet(glm::vec2 position) {
		Action action;
		action.type = ActionType::Toilet;
		action.state = ActionState::Starting;
		action.needToFulfill = NeedType::Bladder;
		action.duration = 3.0F;			 // 3 seconds
		action.restoreAmount = 100.0F;	 // Full bladder relief
		action.spawnPosition = position; // Where to spawn Bio Pile
		return action;
	}
};

/// Get human-readable name for action type (for debug logging)
[[nodiscard]] inline const char* actionTypeName(ActionType type) {
	switch (type) {
		case ActionType::None:
			return "None";
		case ActionType::Eat:
			return "Eat";
		case ActionType::Drink:
			return "Drink";
		case ActionType::Sleep:
			return "Sleep";
		case ActionType::Toilet:
			return "Toilet";
	}
	return "Unknown";
}

} // namespace ecs
