#pragma once

// The colonists behind the task panel's goal rows. See /docs/technical/task-claims.md (Player-facing
// output).

#include "../ClaimIndex.h"
#include "../EntityID.h"
#include "../GoalTaskRegistry.h"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

namespace ecs {

	class World;

	/// What a goal's root still needs of the goal's one item beyond what is on its way, and what is.
	struct GoalItemCount {
		uint32_t needed = 0;
		uint32_t onTheWay = 0;
	};

	/// The colonists behind the task panel's goal rows, read in one pass over every colonist's current
	/// task and last decision trace, so each row is a few lookups. Offers are as stale as the traces.
	class ColonyGoalWork {
	  public:
		/// Rebuilds `claims` from the world; the caller keeps it across passes so its storage stays warm,
		/// and it must outlive this object.
		ColonyGoalWork(World& world, ClaimIndex& claims);

		/// Colonists whose current task serves the goal, in view order. A cut or a haul serves the goal it
		/// was picked through (a carry-in and a cut's deposit leg name their root); a build, a craft, or a
		/// placement serves the goal whose destination it works, and a Build, Craft, or PlacePackaged goal
		/// is served by those first, else by whoever delivers toward it. A suspended record is not current
		/// work, so a colonist seeing to a need serves nothing.
		[[nodiscard]] std::span<const EntityID> workersOf(const GoalTask& goal) const;

		/// Colonists whose last decision offered work toward the goal, in view order: an option for it
		/// chosen, available, blocked only by the colonist's own carry (NoRoom), or held by another
		/// colonist (Reserved). A cut or a haul is for the goal it was picked through; a build, a craft, or a
		/// placement is for the goal of that type whose destination it works. A teardown is counted toward
		/// a Deconstruct goal, which the panel and the tab never list.
		[[nodiscard]] std::span<const EntityID> offeredTo(uint64_t goalId) const;

		/// Some colonist's last decision found the goal's source held by another (Reserved), and none had
		/// an option for it chosen, available, blocked by its own load (NoRoom: a free source exists), or
		/// regrowing (the wait is regrowth).
		[[nodiscard]] bool allSourcesClaimed(uint64_t goalId) const;

		/// Whether a goal has anything to do now. A storage umbrella stays while its box has rules, full
		/// or not, and a stocking cut until its goal system retires it, so either has work only while some
		/// colonist is offered work toward it or works it: a box that wants wood when no loose wood is known
		/// has none.
		/// Every other goal exists only while it has work: its goal system retires it.
		[[nodiscard]] bool hasWork(const GoalTask& goal) const;

		/// For a cut or a haul toward one item: what is on its way toward its root (the claim index's
		/// in-flight, what the AI counts as coming) and its root's remaining need of it (the basis its
		/// cuts and hauls count against) less that. A storage haul's need is the box's free room, not a
		/// shortfall, so it reports none. Nothing for a goal with no one item: a build umbrella, a clear,
		/// a craft, a placement, or a box taking several items or a category.
		[[nodiscard]] std::optional<GoalItemCount> itemCount(World& world, const GoalTask& goal) const;

	  private:
		struct Offers {
			std::vector<EntityID> colonists;
			bool				  sourceFree = false;
			bool				  reserved = false;
		};

		const ClaimIndex&									claims;
		std::unordered_map<uint64_t, std::vector<EntityID>> workersByGoal;
		std::unordered_map<uint64_t, std::vector<EntityID>> workersByEntity;
		std::unordered_map<uint64_t, Offers>				offers;
	};

} // namespace ecs
