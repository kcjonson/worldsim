#pragma once

// GoalTaskRegistry - Central catalog of goal-level work
//
// Tasks exist at the GOAL level (storage wants items, crafting needs materials),
// not at the ITEM level. This makes task counts bounded by O(goals) ~200 instead
// of O(discovered entities) ~100,000.
//
// Design: docs/design/game-systems/colonists/task-registry.md
// Architecture: docs/technical/task-generation-architecture.md

#include "EntityID.h"
#include "components/Task.h"

#include <glm/vec2.hpp>

#include <assets/AssetDefinition.h>
#include <polygon/Polygon.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace assets {
	class AssetRegistry;
}

namespace ecs {

	class World;

	/// What a storage root's need is measured against: the stock colonists may haul in (the item's
	/// configured max, or physical room when none is set), or what cutting stocks it toward (the
	/// configured minimum). Craft and construction roots have one target and ignore it.
	enum class NeedBasis : uint8_t { Stock, Harvest };

	/// Goal status for task hierarchy and dependency tracking
	enum class GoalStatus : uint8_t {
		Available,		 // Can be worked on now
		Blocked,		 // Craft waiting for all materials to be delivered
		NoSource		 // Need known, but no source (stock or harvestable) is known yet
	};

	/// Systems that can own/create goals
	/// Used to track which system is responsible for a goal's lifecycle
	enum class GoalOwner : uint8_t {
		StorageGoalSystem,	// Haul goals for storage containers
		CraftingGoalSystem, // Craft + child Harvest/Haul goals
		BuildGoalSystem,	// PlacePackaged goals
		ConstructionGoalSystem, // Foundation lifecycle: clear Harvest, deliver Harvest/Haul, Build goals
	};

	/// A blueprint's footprint: the foundation's ring (CCW, integer mm, no holes) and its bounding box in
	/// world meters. contains() is the one definition of "inside the footprint": the box, the same
	/// inclusive test SpatialIndex::queryRect applies, then the ring, its boundary counted inside. A thing
	/// is inside when its position is; nothing here has a body radius.
	struct FootprintBounds {
		explicit FootprintBounds(geometry::Ring footprintRing);

		geometry::Ring ring;
		glm::vec2	   min{0.0F, 0.0F};
		glm::vec2	   max{0.0F, 0.0F};

		[[nodiscard]] bool contains(glm::vec2 position) const;
		/// A point strictly inside the ring (an L's box centre can lie in its notch).
		[[nodiscard]] glm::vec2 interiorPoint() const;
		/// `position` itself when it lies outside; else the point `margin` past the ring's nearest edge, along the
		/// boundary's outward direction at the spot on it nearest `position`: the edge's normal, or at a vertex
		/// the bisector of the two edges meeting there. When that point lands back inside (a notch narrower
		/// than the margin), the next nearest edge is tried; when every one does, the nearest edge's point is
		/// given and the drop's own search for ground takes it from there.
		[[nodiscard]] glm::vec2 nearestOutside(glm::vec2 position, float margin) const;

		/// A thing of `defNameId` at `position` blocks the site: a harvestable inside the footprint. The clear
		/// goal is served by exactly the blockers a colonist knows, and the build gate holds while any
		/// blocker stands in the world, so a site is clear exactly when no clear work remains.
		[[nodiscard]] bool blockedBy(const assets::AssetRegistry& registry, uint32_t defNameId, glm::vec2 position) const;
	};

	/// A goal-level task (e.g., "Storage wants rocks", "Crafting needs wood")
	///
	/// Key difference from old item-level tasks:
	/// - Old: One task per discovered loose item (O(discovered items) ~100,000)
	/// - New: One task per GOAL - storage, crafting station, etc. (O(goals) ~200)
	///
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
		assets::ItemCategory acceptedCategory = assets::ItemCategory::None;

		// Metadata
		float	  createdAt = 0.0F;
		GoalOwner owner{}; // Which system created/owns this goal

		// Parent-child hierarchy (for craft → harvest/haul relationships)
		std::optional<uint64_t> parentGoalId;	 // Parent goal (e.g., Harvest/Haul → Craft)
		GoalStatus				status = GoalStatus::Available;

		// For Harvest goals: what item type is yielded when harvesting completes
		uint32_t yieldDefNameId = 0; // e.g., Tree yields Wood

		// Construction clear Harvest: any harvestable inside this footprint serves it, whatever it
		// yields, and the harvest removes it (clearing removes what stands there, regrowing or not).
		std::optional<FootprintBounds> clearFootprint;

		// For Craft goals: identity of the recipe this goal's child hierarchy was built for.
		// Hash of the recipe defName. When a station's next job switches to a different
		// recipe, this changes and the child Harvest/Haul goals are rebuilt. 0 = not a Craft
		// goal (or no recipe).
		uint32_t recipeNameHash = 0;
	};

	/// Global registry of goal-level tasks
	/// Singleton - access via GoalTaskRegistry::Get()
	///
	/// THREAD SAFETY: This registry is NOT thread-safe. All methods must be called
	/// from the main game thread only.
	class GoalTaskRegistry {
	  public:
		// Singleton access
		static GoalTaskRegistry& Get();

		// Clear all goals and restart the id counters (for game restart)
		void clear();

		/// The next claim sequence number, unique until the next clear(). A task takes one at acceptance as
		/// both its claim instance id and its claim sequence (task-claims.md); 0 is never handed out. It
		/// lives here because it is colony state a save must keep.
		uint64_t allocateClaimSeq() { return nextClaimSeq++; }

		// --- Goal Management (called by goal systems: StorageGoalSystem, CraftingGoalSystem, etc.) ---

		/// Create a new goal task
		/// @param goal Goal parameters (id field will be overwritten)
		/// @return The assigned goal ID
		uint64_t createGoal(GoalTask goal);

		/// Update an existing goal in place (e.g., capacity changed).
		///
		/// CONTRACT: the updater may only mutate accepted-type and status fields
		/// (acceptedDefNameIds, acceptedCategory, status, ...).
		/// It must NOT change indexed identity fields (type, owner, destinationEntity,
		/// parentGoalId); only the type index is reconciled here. To change
		/// identity, remove and recreate the goal.
		///
		/// @param goalId The goal to update
		/// @param updater Function to modify the goal
		void updateGoal(uint64_t goalId, const std::function<void(GoalTask&)>& updater);

		/// Remove a goal (e.g., storage destroyed)
		void removeGoal(uint64_t goalId);

		/// Remove goal by destination entity (convenience for entity destruction)
		void removeGoalByDestination(EntityID destinationEntity);

		// --- Queries ---

		/// Get a goal by ID
		[[nodiscard]] const GoalTask* getGoal(uint64_t goalId) const;

		/// Get mutable goal by ID (for systems that need to update goals)
		[[nodiscard]] GoalTask* getGoalMutable(uint64_t goalId);

		/// Get goal by destination entity
		[[nodiscard]] const GoalTask* getGoalByDestination(EntityID destinationEntity) const;

		/// Get all goals of a specific type
		[[nodiscard]] std::vector<const GoalTask*> getGoalsOfType(TaskType type) const;

		/// Call fn(const GoalTask&) for every goal of a type, in the order getGoalsOfType lists them,
		/// without building a list.
		template <typename Fn>
		void forEachGoalOfType(TaskType type, Fn&& fn) const {
			const auto it = typeToGoals.find(type);
			if (it == typeToGoals.end()) {
				return;
			}
			for (uint64_t goalId : it->second) {
				if (const auto goalIt = goals.find(goalId); goalIt != goals.end()) {
					fn(goalIt->second);
				}
			}
		}

		/// Get total count of goals
		[[nodiscard]] size_t goalCount() const { return goals.size(); }

		/// Get count of goals by type
		[[nodiscard]] size_t goalCount(TaskType type) const;

		/// Get all goals owned by a specific system
		[[nodiscard]] std::vector<const GoalTask*> getGoalsByOwner(GoalOwner owner) const;

		/// Get count of goals by owner
		[[nodiscard]] size_t goalCount(GoalOwner owner) const;

		// --- Hierarchy queries ---

		/// Get all child goals of a parent goal
		[[nodiscard]] std::vector<const GoalTask*> getChildGoals(uint64_t parentId) const;

		/// The root of `goalId`'s hierarchy (itself when it has no parent): the goal in-flight claims
		/// count toward. 0 when `goalId` is not a goal.
		[[nodiscard]] uint64_t rootOf(uint64_t goalId) const;

		/// How many more of an item root goal `rootGoalId` needs, in items: its target minus what has
		/// landed at its destination (task-claims.md, Remaining need). A craft's target is the bill of
		/// materials of its station's next job against the station store; a construction umbrella's is
		/// the blueprint manifest against delivered[], while the umbrella accepts the item; a storage
		/// umbrella's is per `basis` against what the box holds, never past its physical room. Read
		/// from the world on each call, so a deposit or a consumed input shows at once. In-flight is
		/// not subtracted here (ClaimIndex::inFlight is). 0 for anything else.
		[[nodiscard]] uint32_t remainingNeed(World& world, uint64_t rootGoalId, uint32_t itemDefNameId, NeedBasis basis) const;

		/// Remove a goal and all its children (cascade delete)
		void removeGoalWithChildren(uint64_t goalId);

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

		// Index: parentGoalId → set of child goalIds
		std::unordered_map<uint64_t, std::unordered_set<uint64_t>> parentToChildren;

		// Next goal ID
		uint64_t nextGoalId = 1;

		uint64_t nextClaimSeq = 1;

		// Internal helpers
		void addToIndices(const GoalTask& goal);
		void removeFromIndices(const GoalTask& goal);
	};

	/// How many more of an item a crafting station's next job needs: the recipe's count less what the
	/// station store holds, 0 when the item is no input of it or nothing is queued. A craft root's
	/// remaining need, and what a deposit into the station is metered to.
	[[nodiscard]] uint32_t craftStationNeed(World& world, EntityID station, uint32_t itemDefNameId);

	/// GoalTaskRegistry::remainingNeed answered once per (root, item, basis) for one pass, filled the
	/// first time each is asked. A pass is one AI update. Deposits and consumed inputs land in
	/// ActionSystem or a goal system, outside it; the one thing inside it that changes a need is a
	/// colonist putting goods away in a box within reach as it drops a chain (storeOrDrop), and the AI
	/// then refreshes that box's root. Callers outside the pass (the deposit leg, a deposit) ask
	/// remainingNeed itself. The table keeps its storage across passes, so a warm pass allocates nothing.
	class RemainingNeedCache {
	  public:
		void invalidate();

		/// Work out again, from the world, every entry of this pass for `rootGoalId`.
		void refresh(World& world, uint64_t rootGoalId);

		[[nodiscard]] uint32_t get(World& world, uint64_t rootGoalId, uint32_t itemDefNameId, NeedBasis basis);

#ifdef DEVELOPMENT_BUILD
		/// Needs worked out since construction; tests read it to show each is worked out once per pass.
		uint64_t computed = 0;
#endif

	  private:
		struct Slot {
			uint64_t  root = 0;
			uint32_t  item = 0;
			uint32_t  value = 0;
			uint32_t  pass = 0;
			NeedBasis basis = NeedBasis::Stock;
		};
		void grow();

		std::vector<Slot> m_slots;
		uint32_t		  m_pass = 1;
		uint32_t		  m_used = 0;
	};

} // namespace ecs
