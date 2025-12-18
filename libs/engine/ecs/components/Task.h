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
	Gather,		 // Tier 6.6: Gathering materials for crafting
	Craft,		 // Tier 6.5: Crafting at a station
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

	/// For Gather tasks: item to collect and target entity
	std::string gatherItemDefName;
	uint64_t gatherTargetEntityId = 0;

	/// For Craft tasks: recipe to craft and station entity ID
	std::string craftRecipeDefName;
	uint64_t targetStationId = 0;

	/// Time since last decision re-evaluation (seconds)
	float timeSinceEvaluation = 0.0F;

	/// Priority score when this task was selected (used for switch threshold comparison)
	float priority = 0.0F;

	/// Debug reason for task selection (e.g., "Hunger at 45%")
	std::string reason;

	/// Check if a task is currently assigned
	[[nodiscard]] bool isActive() const { return type != TaskType::None; }

	/// Reset task to default state (caller responsible for resetting timeSinceEvaluation)
	void clear() {
		type = TaskType::None;
		state = TaskState::Pending;
		targetPosition = glm::vec2{0.0F, 0.0F};
		needToFulfill = NeedType::Count;
		gatherItemDefName.clear();
		gatherTargetEntityId = 0;
		craftRecipeDefName.clear();
		targetStationId = 0;
		priority = 0.0F;
		// Note: timeSinceEvaluation NOT reset here - caller handles timer logic
		reason.clear();
	}
};

} // namespace ecs
