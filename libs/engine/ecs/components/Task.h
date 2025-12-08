#pragma once

// Task Component for Colonist AI Decision System
// Tracks the current task a colonist is performing.
// See /docs/design/game-systems/colonists/ai-behavior.md for design details.

#include "Needs.h"

#include <glm/vec2.hpp>

#include <string>

namespace ecs {

/// Task types that colonists can perform
enum class TaskType : uint8_t {
	None = 0,
	FulfillNeed, // Tier 3/5: Moving to target for need fulfillment
	Wander		 // Tier 7: Random exploration
};

/// Task state machine
enum class TaskState : uint8_t {
	Pending, // Task assigned, not yet started movement
	Moving,	 // Moving toward target position
	Arrived	 // Reached target (ready for Actions System)
};

/// Task component - tracks a colonist's current activity
struct Task {
	TaskType  type = TaskType::None;
	TaskState state = TaskState::Pending;

	/// Target position to move to
	glm::vec2 targetPosition{0.0F, 0.0F};

	/// For FulfillNeed tasks: which need is being addressed
	NeedType needToFulfill = NeedType::Count;

	/// Time since last decision re-evaluation (seconds)
	float timeSinceEvaluation = 0.0F;

	/// Debug reason for task selection (e.g., "Hunger at 45%")
	std::string reason;

	/// Check if a task is currently assigned
	[[nodiscard]] bool isActive() const { return type != TaskType::None; }

	/// Reset task to default state
	void clear() {
		type = TaskType::None;
		state = TaskState::Pending;
		targetPosition = glm::vec2{0.0F, 0.0F};
		needToFulfill = NeedType::Count;
		timeSinceEvaluation = 0.0F;
		reason.clear();
	}
};

} // namespace ecs
