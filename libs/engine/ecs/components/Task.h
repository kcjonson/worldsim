#pragma once

// Task Component for Colonist AI Decision System
// Tracks the current task a colonist is performing.
// See /docs/design/game-systems/colonists/ai-behavior.md for design details.

#include "Needs.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>
#include <string>

namespace ecs {

/// Task types that colonists can perform
enum class TaskType : uint8_t {
	None = 0,
	FulfillNeed,   // Tier 3/5: Moving to target for need fulfillment
	Harvest,	   // Tier 6.7: Harvesting resources (cutting trees, harvesting bushes)
	Gather,		   // Tier 6.6: Gathering materials for crafting
	Craft,		   // Tier 6.5: Crafting at a station
	Haul,		   // Tier 6.4: Moving loose items to storage containers
	PlacePackaged, // Tier 6.35: Carrying packaged items to placement locations
	Wander		   // Tier 7: Random exploration
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

	/// For Harvest tasks: target to harvest and goal context
	uint64_t harvestTargetEntityId = 0;	   // Entity ID of the harvestable (tree, bush, etc.)
	uint64_t harvestGoalId = 0;			   // Goal ID this harvest is contributing to
	uint32_t harvestYieldDefNameId = 0;	   // What item type will be yielded

	/// For Gather tasks: item to collect and target entity
	std::string gatherItemDefName;
	uint64_t gatherTargetEntityId = 0;

	/// For Craft tasks: recipe to craft and station entity ID
	std::string craftRecipeDefName;
	uint64_t targetStationId = 0;

	/// For Haul tasks: item to haul and storage container target
	std::string haulItemDefName;				   // Item being hauled
	uint32_t	haulQuantity = 1;				   // Quantity to haul
	uint64_t	haulTargetStorageId = 0;		   // Entity ID of the storage container (destination)
	uint64_t	haulGoalId = 0;					   // Goal ID being fulfilled (for cleanup on completion)
	glm::vec2	haulSourcePosition{0.0F, 0.0F};	   // Position of the source item
	glm::vec2	haulTargetPosition{0.0F, 0.0F};	   // Position of the storage container

	/// For PlacePackaged tasks: packaged entity to carry and placement target
	uint64_t  placePackagedEntityId = 0;		   // Entity ID of the packaged item to carry
	glm::vec2 placeSourcePosition{0.0F, 0.0F};	   // Where the packaged item currently is
	glm::vec2 placeTargetPosition{0.0F, 0.0F};	   // Where to place it (from Packaged.targetPosition)

	/// Task chain tracking (for multi-step tasks like pickup→deposit) - Phase 5 infrastructure
	/// Planned behavior: when a colonist completes a chain step and selects the next task,
	/// if the candidate is the next step in the same chain, it receives a +2000 priority bonus.
	std::optional<uint64_t> chainId;  // Which chain this task belongs to (nullopt = not part of chain)
	uint8_t chainStep = 0;			  // Step index within the chain (0 = first step)

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
		harvestTargetEntityId = 0;
		harvestGoalId = 0;
		harvestYieldDefNameId = 0;
		gatherItemDefName.clear();
		gatherTargetEntityId = 0;
		craftRecipeDefName.clear();
		targetStationId = 0;
		haulItemDefName.clear();
		haulQuantity = 1;
		haulTargetStorageId = 0;
		haulGoalId = 0;
		haulSourcePosition = glm::vec2{0.0F, 0.0F};
		haulTargetPosition = glm::vec2{0.0F, 0.0F};
		placePackagedEntityId = 0;
		placeSourcePosition = glm::vec2{0.0F, 0.0F};
		placeTargetPosition = glm::vec2{0.0F, 0.0F};
		chainId.reset();
		chainStep = 0;
		priority = 0.0F;
		// Note: timeSinceEvaluation NOT reset here - caller handles timer logic
		reason.clear();
	}
};

} // namespace ecs
