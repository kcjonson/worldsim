#pragma once

// GoalTaskRegistry - Central catalog of goal-level work
//
// Tasks exist at the GOAL level (storage wants items, crafting needs materials),
// not at the ITEM level. This makes task counts bounded by O(goals) ~200 instead
// of O(discovered entities) ~100,000.
//
// See /docs/design/game-systems/colonists/task-registry.md for design details.

#include "EntityID.h"
#include "components/Task.h"

#include <glm/vec2.hpp>

#include <assets/AssetDefinition.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ecs {

	/// Goal status for task hierarchy and dependency tracking
	enum class GoalStatus : uint8_t {
		Available,		 // Can be worked on now
		InProgress,		 // Colonist(s) actively working
		WaitingForItems, // Haul waiting for harvest to create items
		Blocked,		 // Craft waiting for all materials to be delivered
		Complete		 // Done
	};

	/// Systems that can own/create goals
	/// Used to track which system is responsible for a goal's lifecycle
	enum class GoalOwner : uint8_t {
		None = 0,			// Unowned (legacy compatibility)
		StorageGoalSystem,	// Haul goals for storage containers
		CraftingGoalSystem, // Craft + child Harvest/Haul goals
		BuildGoalSystem,	// PlacePackaged goals
	};

	/// A goal-level task (e.g., "Storage wants rocks", "Crafting needs wood")
	///
	/// Key difference from old item-level tasks:
	/// - Old: One task per discovered loose item (O(discovered items) ~100,000)
	/// - New: One task per GOAL - storage, crafting station, etc. (O(goals) ~200)
	///
	/// Item-level reservations happen INSIDE the goal, not as separate tasks.
	struct GoalTask {
		// Identity
		uint64_t id = 0;	// Unique goal ID
		TaskType type = TaskType::None;

		// Destination (where items go / work happens)
		EntityID  destinationEntity; // Storage, crafting station, build site
		glm::vec2 destinationPosition{0.0F, 0.0F};
		uint32_t  destinationDefNameId = 0; // For display

		// What this goal accepts (for Haul/Gather tasks)
		std::vector<uint32_t>		 acceptedDefNameIds; // Specific items accepted
		engine::assets::ItemCategory acceptedCategory = engine::assets::ItemCategory::None;

		// Item-level reservations: which items are being hauled TO this goal
		// Key = worldEntityKey (hash of item position + defNameId)
		// Value = colonist hauling it
		std::unordered_map<uint64_t, EntityID> itemReservations;

		// Progress tracking
		uint32_t targetAmount = 0;	  // How many items the goal wants
		uint32_t deliveredAmount = 0; // How many have been delivered

		// Metadata
		float	  createdAt = 0.0F;
		GoalOwner owner = GoalOwner::None; // Which system created/owns this goal

		// Parent-child hierarchy (for craft → harvest/haul relationships)
		std::optional<uint64_t> parentGoalId;	 // Parent goal (e.g., Harvest/Haul → Craft)
		std::optional<uint64_t> dependsOnGoalId; // Must complete before this can start (Haul → Harvest)
		GoalStatus				status = GoalStatus::Available;

		// For Harvest goals: what item type is yielded when harvesting completes
		uint32_t yieldDefNameId = 0; // e.g., Tree yields Wood

		// Task chain ID for continuity bonus (cutter gets priority for linked haul)
		std::optional<uint64_t> chainId;

		// Check if an item is reserved (by anyone)
		[[nodiscard]] bool isItemReserved(uint64_t worldEntityKey) const {
			return itemReservations.find(worldEntityKey) != itemReservations.end();
		}

		// Check if an item is reserved by a specific colonist
		[[nodiscard]] bool isItemReservedBy(uint64_t worldEntityKey, EntityID colonist) const {
			auto it = itemReservations.find(worldEntityKey);
			return it != itemReservations.end() && it->second == colonist;
		}

		// Check if goal is complete
		[[nodiscard]] bool isComplete() const { return targetAmount > 0 && deliveredAmount >= targetAmount; }

		// Get available capacity (target - delivered - in-progress)
		[[nodiscard]] uint32_t availableCapacity() const {
			uint32_t inProgress = static_cast<uint32_t>(itemReservations.size());
			if (deliveredAmount + inProgress >= targetAmount) {
				return 0;
			}
			return targetAmount - deliveredAmount - inProgress;
		}
	};

	/// Filter predicate for goal queries
	using GoalFilter = std::function<bool(const GoalTask&)>;

	/// Global registry of goal-level tasks
	/// Singleton - access via GoalTaskRegistry::Get()
	///
	/// THREAD SAFETY: This registry is NOT thread-safe. All methods must be called
	/// from the main game thread only.
	class GoalTaskRegistry {
	  public:
		// Singleton access
		static GoalTaskRegistry& Get();

		// Clear all goals (for game restart)
		void clear();

		// --- Goal Management (called by goal systems: StorageGoalSystem, CraftingGoalSystem, etc.) ---

		/// Create a new goal task
		/// @param goal Goal parameters (id field will be overwritten)
		/// @return The assigned goal ID
		uint64_t createGoal(GoalTask goal);

		/// Update an existing goal (e.g., capacity changed)
		/// @param goalId The goal to update
		/// @param updater Function to modify the goal
		void updateGoal(uint64_t goalId, const std::function<void(GoalTask&)>& updater);

		/// Remove a goal (e.g., storage destroyed)
		void removeGoal(uint64_t goalId);

		/// Remove goal by destination entity (convenience for entity destruction)
		void removeGoalByDestination(EntityID destinationEntity);

		// --- Item Reservations (called by colonists when they claim work) ---

		/// Reserve an item for hauling to a goal
		/// @param goalId The goal the item will be delivered to
		/// @param worldEntityKey Hash key of the item being hauled
		/// @param colonist The colonist claiming this item
		/// @return true if reservation succeeded
		bool reserveItem(uint64_t goalId, uint64_t worldEntityKey, EntityID colonist);

		/// Release an item reservation (colonist finished or abandoned)
		void releaseItem(uint64_t goalId, uint64_t worldEntityKey);

		/// Release all reservations held by a colonist (colonist died, changed task, etc.)
		void releaseAllForColonist(EntityID colonist);

		/// Record delivery of an item to a goal
		/// @param goalId The goal that received the item
		/// @param worldEntityKey The item that was delivered (releases reservation too)
		void recordDelivery(uint64_t goalId, uint64_t worldEntityKey);

		// --- Queries ---

		/// Get a goal by ID
		[[nodiscard]] const GoalTask* getGoal(uint64_t goalId) const;

		/// Get mutable goal by ID (for systems that need to update goals)
		[[nodiscard]] GoalTask* getGoalMutable(uint64_t goalId);

		/// Get goal by destination entity
		[[nodiscard]] const GoalTask* getGoalByDestination(EntityID destinationEntity) const;

		/// Get all goals of a specific type
		[[nodiscard]] std::vector<const GoalTask*> getGoalsOfType(TaskType type) const;

		/// Get all goals matching a filter
		[[nodiscard]] std::vector<const GoalTask*> getGoalsMatching(const GoalFilter& filter) const;

		/// Get goals within radius of a position
		[[nodiscard]] std::vector<const GoalTask*> getGoalsInRadius(const glm::vec2& center, float radius) const;

		/// Get total count of goals
		[[nodiscard]] size_t goalCount() const { return goals.size(); }

		/// Get count of goals by type
		[[nodiscard]] size_t goalCount(TaskType type) const;

		/// Get all goals owned by a specific system
		[[nodiscard]] std::vector<const GoalTask*> getGoalsByOwner(GoalOwner owner) const;

		/// Get count of goals by owner
		[[nodiscard]] size_t goalCount(GoalOwner owner) const;

		/// Check if an item is reserved by any goal
		/// @return The goal ID if reserved, nullopt if not
		[[nodiscard]] std::optional<uint64_t> findItemReservation(uint64_t worldEntityKey) const;

		// --- Hierarchy queries ---

		/// Get all child goals of a parent goal
		[[nodiscard]] std::vector<const GoalTask*> getChildGoals(uint64_t parentId) const;

		/// Get all goals that depend on a given goal
		[[nodiscard]] std::vector<const GoalTask*> getDependentGoals(uint64_t goalId) const;

		/// Remove a goal and all its children (cascade delete)
		void removeGoalWithChildren(uint64_t goalId);

		/// Update status of dependent goals when a goal completes
		/// (e.g., Haul becomes Available when its Harvest dependency completes)
		void notifyGoalCompleted(uint64_t completedGoalId);

	  private:
		GoalTaskRegistry() = default;

		// Goal storage
		std::unordered_map<uint64_t, GoalTask> goals; // goalId → goal

		// Index: destination entity → goalId
		std::unordered_map<EntityID, uint64_t> destinationToGoal;

		// Index: TaskType → set of goalIds
		std::unordered_map<TaskType, std::unordered_set<uint64_t>> typeToGoals;

		// Index: GoalOwner → set of goalIds
		std::unordered_map<GoalOwner, std::unordered_set<uint64_t>> ownerToGoals;

		// Index: worldEntityKey → goalId (for finding which goal has an item reserved)
		std::unordered_map<uint64_t, uint64_t> itemToGoal;

		// Index: parentGoalId → set of child goalIds
		std::unordered_map<uint64_t, std::unordered_set<uint64_t>> parentToChildren;

		// Index: dependsOnGoalId → set of dependent goalIds
		std::unordered_map<uint64_t, std::unordered_set<uint64_t>> goalToDependents;

		// Next goal ID
		uint64_t nextGoalId = 1;

		// Internal helpers
		void addToIndices(const GoalTask& goal);
		void removeFromIndices(const GoalTask& goal);
	};

} // namespace ecs
