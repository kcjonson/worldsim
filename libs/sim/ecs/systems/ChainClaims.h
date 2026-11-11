#pragma once

// The claims each task chain holds, stage by stage. See /docs/technical/task-claims.md (Chain claims).
// AIDecisionSystem records a chain's claims at acceptance; ActionSystem rewrites them as the chain
// advances; a cleared task drops them with it.

#include "../ClaimIndex.h"
#include "../EntityID.h"
#include "../GoalTaskRegistry.h"
#include "../components/Task.h"

#include <cstdint>
#include <optional>

namespace assets {
	class AssetRegistry;
}

namespace ecs {

	class World;
	struct Inventory;
	struct Memory;

	/// What a root still needs of an item that no other task is bringing: its remaining need less
	/// the in-flight claims of every task `exclude` leaves out (task-claims.md, Availability).
	[[nodiscard]] uint32_t
	rootAvailable(World& world, const ClaimIndex& claims, uint64_t rootGoalId, uint32_t itemDefNameId, NeedBasis basis, ClaimExclusion exclude);

	/// The basis work toward `root` counts against: cutting to stock a box counts toward its minimum,
	/// everything else toward its stock target.
	[[nodiscard]] NeedBasis harvestBasis(const GoalTask& root);

	/// Record the claims of the chain `task` was just accepted for. Reads the task's own fields, so it
	/// runs after selection has filled them; `task.claims` must be empty.
	void recordChainClaims(
		World& world, const ClaimIndex& claims, const assets::AssetRegistry& registry, const Memory& memory, const Inventory& inventory, Task& task
	);

	/// A cut has landed: the harvest's Use drops, and it brings `quantity` of `itemDefNameId` to
	/// `destination` toward `rootGoalId` (In-flight and Inbound), as its own deposit leg.
	void harvestToDeliveryLeg(Task& task, uint64_t rootGoalId, EntityID destination, uint32_t itemDefNameId, uint32_t quantity);

	/// The chain has taken what it will take from its source (a pickup, a withdraw, or arriving with the
	/// goods already in hand): Use and SourceQuantity drop, Inbound and InFlight become `carried`, capped
	/// at what they were. A claim left at 0 drops.
	void claimsToDeliveryLeg(Task& task, uint32_t carried);

	/// The suspended record a need would leave of `task`, a haul whose goods the colonist keeps: its
	/// Inbound and InFlight capped at `held`, the units of the item it still carries. Nothing when the
	/// haul has not reached its source yet, or nothing is held.
	[[nodiscard]] std::optional<SuspendedWork> suspendableWork(const assets::AssetRegistry& registry, const Task& task, uint32_t held);

	/// Keep `task`'s suspended record true to what the colonist still holds: gone when its root goal is
	/// gone or none of the goods are left (eaten, used, dropped), else its claims capped at what is held.
	/// Called before the claim index is rebuilt, so the index never counts goods that are not there.
	void refreshSuspended(const assets::AssetRegistry& registry, const Inventory& inventory, Task& task);

	/// Does `task`, just accepted, carry in the goods of its suspended record (same root, item, and
	/// destination)?
	[[nodiscard]] bool adoptsSuspended(const assets::AssetRegistry& registry, const Task& task);

	/// Take over the suspended record: its instance id, sequence, and claims, capped at what this
	/// carry-in delivers. The record is gone afterwards.
	void adoptSuspended(Task& task);

} // namespace ecs
