#pragma once

// The claims each task chain holds, stage by stage. See /docs/technical/task-claims.md (Chain claims).
// AIDecisionSystem records a chain's claims at acceptance; ActionSystem rewrites them as the chain
// advances; a cleared task drops them with it.

#include "../components/Task.h"

#include <cstdint>
#include <optional>

namespace assets {
	class AssetRegistry;
}

namespace ecs {

	struct Inventory;
	struct Memory;

	/// Record the claims of the chain `task` was just accepted for. Reads the task's own fields, so it
	/// runs after selection has filled them; `task.claims` must be empty.
	void recordChainClaims(const assets::AssetRegistry& registry, const Memory& memory, const Inventory& inventory, Task& task);

	/// The chain has taken what it will take from its source (a pickup, a withdraw, or arriving with the
	/// goods already in hand): Use and SourceQuantity drop, Inbound and InFlight become `carried`, capped
	/// at what they were. A claim left at 0 drops.
	void claimsToDeliveryLeg(Task& task, uint32_t carried);

	/// The suspended record a need would leave of `task`, a haul whose goods the colonist keeps: its
	/// Inbound and InFlight capped at `held`, the units of the item it still carries. Nothing when the
	/// haul has not reached its source yet, or nothing is held.
	[[nodiscard]] std::optional<SuspendedWork> suspendableWork(const assets::AssetRegistry& registry, const Task& task, uint32_t held);

	/// Does `task`, just accepted, carry in the goods of its suspended record (same root and item)?
	[[nodiscard]] bool adoptsSuspended(const assets::AssetRegistry& registry, const Task& task);

	/// Take over the suspended record: its instance id, sequence, and claims, capped at what this
	/// carry-in delivers. The record is gone afterwards.
	void adoptSuspended(Task& task);

} // namespace ecs
