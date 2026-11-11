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

	/// What a root still needs of an item that no other task is bringing: `need`, its remaining need,
	/// less the in-flight claims of every task `exclude` leaves out (task-claims.md, Availability). The
	/// AI passes the need its update cached; the deposit leg reads it from the world.
	[[nodiscard]] uint32_t
	rootAvailable(uint32_t need, const ClaimIndex& claims, uint64_t rootGoalId, uint32_t itemDefNameId, ClaimExclusion exclude);

	/// The basis work toward `root` counts against: cutting to stock a box counts toward its minimum,
	/// everything else toward its stock target.
	[[nodiscard]] NeedBasis harvestBasis(const GoalTask& root);

	/// Units of `itemDefNameId` a claim list carries toward `rootGoalId`: its Inbound once it holds no
	/// source claim (a delivery leg, or a cut carrying what earlier cuts landed), when its InFlight
	/// names that root.
	template <uint8_t Capacity>
	[[nodiscard]] uint32_t cargoOnClaims(const BasicClaimList<Capacity>& claims, uint64_t rootGoalId, uint32_t itemDefNameId) {
		uint32_t inbound = 0;
		bool	 towardRoot = false;
		for (const Claim& claim : claims) {
			if (claim.kind() == ClaimKind::SourceQuantity) {
				return 0;
			}
			if (claim.itemDefNameId() != itemDefNameId) {
				continue;
			}
			if (claim.kind() == ClaimKind::Inbound) {
				inbound += claim.quantity();
			} else if (claim.kind() == ClaimKind::InFlight && claim.target() == rootGoalId) {
				towardRoot = true;
			}
		}
		return towardRoot ? inbound : 0U;
	}

	/// The goods a colonist's task, current or suspended, carries toward `rootGoalId`.
	[[nodiscard]] uint32_t claimedCargo(const Task& task, uint64_t rootGoalId, uint32_t itemDefNameId);

	/// The cargo claims `task` hands to the next leg of the same gathering (a further cut, or the
	/// carry-in), with its instance id and sequence, so goods carried between two cuts are never
	/// uncommitted (task-claims.md, Batching). Nothing when it carries none of the item toward the root.
	[[nodiscard]] std::optional<SuspendedWork> handOverCargo(const Task& task, uint64_t rootGoalId, uint32_t itemDefNameId);

	/// Record the claims of the chain `task` was just accepted for. Reads the task's own fields, so it
	/// runs after selection has filled them. `task.claims` holds nothing, or only the cargo claims it
	/// took over (handOverCargo, or an adopted suspended record), which a cut counts as carried and a
	/// carry-in resizes to what it delivers.
	void recordChainClaims(
		World& world, RemainingNeedCache& needs, const ClaimIndex& claims, const assets::AssetRegistry& registry, const Memory& memory,
		const Inventory& inventory, Task& task
	);

	/// A cut has landed: the harvest's Use drops, and it brings `quantity` of `itemDefNameId` to
	/// `destination` toward `rootGoalId` (In-flight and Inbound), as its own deposit leg.
	void harvestToDeliveryLeg(Task& task, uint64_t rootGoalId, EntityID destination, uint32_t itemDefNameId, uint32_t quantity);

	/// The item a task's chain carries for its root: a haul's item, or a cut's yield. 0 for anything else.
	[[nodiscard]] uint32_t chainItem(const assets::AssetRegistry& registry, const Task& task);

	/// The chain has taken what it will take from its source (a pickup, a withdraw, or arriving with the
	/// goods already in hand): Use and SourceQuantity drop, Inbound and InFlight become `carried`, capped
	/// at what they were. A claim left at 0 drops.
	void claimsToDeliveryLeg(Task& task, uint32_t carried);

	/// The suspended record a need would leave of `task`, a haul or a cut carrying goods the colonist
	/// keeps: its Inbound and InFlight capped at `held`, the units of the item it still carries, and at
	/// what it carries (a cut's yield still on the plant is not held). Nothing when the haul has not
	/// reached its source yet, or nothing is held.
	[[nodiscard]] std::optional<SuspendedWork> suspendableWork(const assets::AssetRegistry& registry, const Task& task, uint32_t held);

	/// Keep `task`'s suspended record true to what the colonist still holds: gone when its root goal is
	/// gone or none of the goods are left (eaten, used, dropped), else its claims capped at what is held.
	/// Called before the claim index is rebuilt, so the index never counts goods that are not there.
	void refreshSuspended(const assets::AssetRegistry& registry, const Inventory& inventory, Task& task);

	/// Does `task`, just accepted, take the goods of its suspended record on (same root, item, and
	/// destination): their carry-in, or a further cut that carries them along?
	[[nodiscard]] bool adoptsSuspended(const assets::AssetRegistry& registry, const Task& task);

	/// Take over `cargo`, a suspended record or a handed-over gathering: its instance id, sequence,
	/// and claims, which recordChainClaims then sizes to the new task.
	void takeOverCargo(Task& task, const SuspendedWork& cargo);

} // namespace ecs
