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

/// Task types that colonists can perform. Arbitration tiers (assigned in priority-tuning.xml's
/// <TaskTiers> + classifyTier promotions): see colonist-task-arbitration.md.
enum class TaskType : uint8_t {
	None = 0,
	FulfillNeed,   // Need fulfillment: tier 2 (critical), 5 (actionable), or 7 (gather-food sentinel)
	Harvest,	   // Harvesting resources: tier 4 if serving a work order, else 6 (opportunistic)
	Craft,		   // Crafting at a station: tier 4 (active work order)
	Haul,		   // Moving loose items to storage: tier 4 if serving a work order, else 6
	PlacePackaged, // Carrying packaged items to placement: tier 4 if serving/carrying, else 6
	Build,		   // Advancing construction on a blueprint: tier 4 (active work order)
	Deconstruct,   // Tearing down a structure: tier 4 (active work order)
	Wander		   // Random exploration: tier 7 (idle)
};

/// Task state machine
enum class TaskState : uint8_t {
	Pending, // Task assigned, not yet started movement
	Moving,	 // Moving toward target position
	Arrived	 // Reached target (ready for Actions System)
};

/// Navigation belief state - what the colonist believes about its route.
/// Drives the info panel vocabulary ("Going to", "Re-routing", "Can't find a way").
/// SearchingLKP and LookingForWayIn are reserved for deferred search behaviors.
enum class NavState : uint8_t {
	Traveling,      // Route planned and being followed
	Rerouting,      // Replanning because a new wall/opening was discovered
	SearchingLKP,   // (reserved) searching last-known position
	LookingForWayIn, // (reserved) searching for an entrance to a building
	CantFindWayTo   // Believed route denied; colonist is stopped
};

/// Task component - tracks a colonist's current activity
struct Task {
	TaskType  type = TaskType::None;
	TaskState state = TaskState::Pending;
	NavState  navState = NavState::Traveling;
	// Rerouting is momentary: set to ~30 ticks when a repath fires so the player sees a brief
	// "Re-routing" beat before the panel reverts to "Going to". Decremented each update tick;
	// zero means the Rerouting display window has elapsed (revert to Traveling).
	std::uint8_t navStateHold = 0;

	/// Target position to move to
	glm::vec2 targetPosition{0.0F, 0.0F};

	/// For FulfillNeed tasks: which need is being addressed
	NeedType needToFulfill = NeedType::Count;

	/// For Harvest tasks: target to harvest and goal context
	uint64_t harvestTargetEntityId = 0;	   // Entity ID of the harvestable (tree, bush, etc.)
	uint64_t harvestGoalId = 0;			   // Goal ID this harvest is contributing to
	uint32_t harvestYieldDefNameId = 0;	   // What item type will be yielded

	/// For Craft tasks: recipe to craft and station entity ID
	std::string craftRecipeDefName;
	uint64_t targetStationId = 0;

	/// For Haul tasks: item to haul and storage container target
	std::string haulItemDefName;				   // Item being hauled
	uint32_t	haulQuantity = 1;				   // Quantity to haul
	uint64_t	haulSourceStorageId = 0;		   // Entity ID of the source box for a storage->storage pull (0 = loose/inventory source)
	uint64_t	haulTargetStorageId = 0;		   // Entity ID of the storage container (destination)
	uint64_t	haulGoalId = 0;					   // Goal ID being fulfilled (for cleanup on completion)
	glm::vec2	haulSourcePosition{0.0F, 0.0F};	   // Position of the source item
	glm::vec2	haulTargetPosition{0.0F, 0.0F};	   // Position of the storage container

	/// For craft-material hauls: the colonist already carries the harvested items in its
	/// inventory, so the haul skips the ground-pickup phase and just carries them to the
	/// crafting station. The station has no storage container; "delivery" means the items
	/// are present at the station, ready for the subsequent Craft action to consume from
	/// inventory. The haul records its delivery (crediting the parent Craft goal) on arrival.
	bool haulFromInventory = false;

	/// For Build/Deconstruct tasks: the blueprint entity whose StructureBlueprint is
	/// advanced. The target position (build work slot) is carried in targetPosition.
	uint64_t buildBlueprintEntityId = 0;

	/// For PlacePackaged tasks: packaged entity to carry and placement target
	uint64_t  placePackagedEntityId = 0;		   // Entity ID of the packaged item to carry
	glm::vec2 placeSourcePosition{0.0F, 0.0F};	   // Where the packaged item currently is
	glm::vec2 placeTargetPosition{0.0F, 0.0F};	   // Where to place it (from Packaged.targetPosition)

	/// Task chain tracking (for multi-step tasks like pickup→deposit).
	/// A mid-chain provisioning task (servesActiveWorkOrder) is classified at tier 4 in the
	/// (tier, score) arbitration, which is what keeps a colonist on its provisioning chain (the
	/// former +2000 chain score bonus is gone; see colonist-task-arbitration.md).
	std::optional<uint64_t> chainId;  // Which chain this task belongs to (nullopt = not part of chain)
	uint8_t chainStep = 0;			  // Step index within the chain (0 = first step)

	/// Time since last decision re-evaluation (seconds)
	float timeSinceEvaluation = 0.0F;

	/// Arbitration tier of this task when selected (lower = higher priority). Paired with `priority`
	/// (the within-tier score) to form the (tier, score) key. The action-interruption gate compares
	/// tiers first: a strictly higher-tier challenger interrupts; a same-tier challenger must beat
	/// the within-tier score by the switch threshold.
	int priorityTier = 7;

	/// Within-tier score when this task was selected (used for the same-tier switch threshold).
	float priority = 0.0F;

	/// Debug reason for task selection (e.g., "Hunger at 45%")
	std::string reason;

	/// Check if a task is currently assigned
	[[nodiscard]] bool isActive() const { return type != TaskType::None; }

	/// Reset task to default state (caller responsible for resetting timeSinceEvaluation)
	void clear() {
		type = TaskType::None;
		state = TaskState::Pending;
		navState = NavState::Traveling;
		navStateHold = 0;
		targetPosition = glm::vec2{0.0F, 0.0F};
		needToFulfill = NeedType::Count;
		harvestTargetEntityId = 0;
		harvestGoalId = 0;
		harvestYieldDefNameId = 0;
		craftRecipeDefName.clear();
		targetStationId = 0;
		haulItemDefName.clear();
		haulQuantity = 1;
		haulSourceStorageId = 0;
		haulTargetStorageId = 0;
		haulGoalId = 0;
		haulSourcePosition = glm::vec2{0.0F, 0.0F};
		haulTargetPosition = glm::vec2{0.0F, 0.0F};
		haulFromInventory = false;
		buildBlueprintEntityId = 0;
		placePackagedEntityId = 0;
		placeSourcePosition = glm::vec2{0.0F, 0.0F};
		placeTargetPosition = glm::vec2{0.0F, 0.0F};
		chainId.reset();
		chainStep = 0;
		priorityTier = 7;
		priority = 0.0F;
		// Note: timeSinceEvaluation NOT reset here - caller handles timer logic
		reason.clear();
	}
};

} // namespace ecs
