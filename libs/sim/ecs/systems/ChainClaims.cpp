#include "ChainClaims.h"

#include "../ClaimIndex.h"
#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"

#include "assets/AssetRegistry.h"

#include <algorithm>

namespace ecs {

	namespace {

		void push(Task& task, const Claim& claim) {
			[[maybe_unused]] const bool pushed = task.claims.push(claim);
		}

		/// The most one cut of the remembered harvestable gives, capped by what the colonist can still
		/// carry of the yield. Over-estimating only makes another colonist briefly see less available;
		/// under-estimating would let it re-cut.
		uint32_t mostPerCut(const assets::AssetRegistry& registry, const Memory& memory, const Inventory& inventory, const Task& task) {
			const KnownWorldEntity* source = memory.getWorldEntity(task.harvestTargetEntityId);
			if (source == nullptr) {
				return 0;
			}
			const auto* def = registry.getDefinitionById(source->defNameId);
			if (def == nullptr || !def->capabilities.harvestable.has_value()) {
				return 0;
			}
			const std::string& yieldDefName = registry.getDefName(task.harvestYieldDefNameId);
			const uint32_t	   room = ecs::pickupUnitsThatFit(inventory, registry, yieldDefName, ecs::carriedCargoMassKg(inventory, registry));
			return std::min(def->capabilities.harvestable->amountMax, room);
		}

		/// The Inbound quantity of `item` the task took over with its cargo, removing that cargo's
		/// claims so the chain records them afresh.
		uint32_t takeCargoClaims(Task& task, uint32_t item) {
			uint32_t carried = 0;
			for (const Claim& claim : task.claims) {
				if (claim.kind() == ClaimKind::Inbound && claim.itemDefNameId() == item) {
					carried += claim.quantity();
				}
			}
			task.claims.eraseIf([](const Claim& claim) { return claim.kind() == ClaimKind::Inbound || claim.kind() == ClaimKind::InFlight; });
			return carried;
		}

	} // namespace

	uint32_t claimedCargo(const Task& task, uint64_t rootGoalId, uint32_t itemDefNameId) {
		uint32_t cargo = task.type == TaskType::FulfillNeed ? 0U : cargoOnClaims(task.claims, rootGoalId, itemDefNameId);
		if (task.suspended && task.suspended->rootGoalId == rootGoalId && task.suspended->itemDefNameId == itemDefNameId) {
			cargo += cargoOnClaims(task.suspended->claims, rootGoalId, itemDefNameId);
		}
		return cargo;
	}

	uint32_t cargoToward(const Task& task, uint64_t rootGoalId, uint32_t itemDefNameId, uint32_t held, bool edible) {
		return edible ? std::min(held, claimedCargo(task, rootGoalId, itemDefNameId)) : held;
	}

	uint32_t haulCargo(const Task& task, uint32_t held, bool edible) {
		if (!edible) {
			return held;
		}
		uint32_t inbound = 0;
		for (const Claim& claim : task.claims) {
			if (claim.kind() == ClaimKind::SourceQuantity) {
				return 0;
			}
			inbound += claim.kind() == ClaimKind::Inbound ? claim.quantity() : 0U;
		}
		return std::min(held, inbound);
	}

	std::optional<SuspendedWork> handOverCargo(const Task& task, uint64_t rootGoalId, uint32_t itemDefNameId) {
		const uint32_t cargo = cargoOnClaims(task.claims, rootGoalId, itemDefNameId);
		if (cargo == 0 || task.instanceId == 0) {
			return std::nullopt;
		}
		SuspendedWork handed{.instanceId = task.instanceId, .claimSeq = task.claimSeq, .rootGoalId = rootGoalId, .itemDefNameId = itemDefNameId};
		for (const Claim& claim : task.claims) {
			if (claim.kind() == ClaimKind::Inbound && claim.itemDefNameId() == itemDefNameId) {
				[[maybe_unused]] const bool pushed = handed.claims.push(Claim::inbound(claim.target(), itemDefNameId, cargo));
				[[maybe_unused]] const bool paired = handed.claims.push(Claim::inFlight(rootGoalId, itemDefNameId, cargo));
				break;
			}
		}
		return handed;
	}

	uint32_t chainItem(const assets::AssetRegistry& registry, const Task& task) {
		switch (task.type) {
			case TaskType::Haul:
				return registry.getDefNameId(task.haulItemDefName);
			case TaskType::Harvest:
				return task.harvestYieldDefNameId;
			default:
				return 0;
		}
	}

	uint32_t rootAvailable(uint32_t need, const ClaimIndex& claims, uint64_t rootGoalId, uint32_t itemDefNameId, ClaimExclusion exclude) {
		const uint32_t inFlight = need > 0 ? claims.inFlight(rootGoalId, itemDefNameId, exclude) : 0U;
		return need > inFlight ? need - inFlight : 0U;
	}

	NeedBasis harvestBasis(const GoalTask& root) {
		return root.owner == GoalOwner::StorageGoalSystem ? NeedBasis::Harvest : NeedBasis::Stock;
	}

	void recordChainClaims(
		World& world, RemainingNeedCache& needs, const ClaimIndex& claims, const assets::AssetRegistry& registry, const Memory& memory,
		const Inventory& inventory, Task& task
	) {
		const auto& goals = GoalTaskRegistry::Get();
		switch (task.type) {
			case TaskType::Harvest: {
				const uint32_t item = task.harvestYieldDefNameId;
				const uint32_t handedOver = takeCargoClaims(task, item);
				push(task, Claim::use(TargetKind::WorldKey, task.harvestTargetEntityId));
				// A footprint clear removes what stands there and brings nothing toward a need.
				const GoalTask* goal = goals.getGoal(task.harvestGoalId);
				if (goal == nullptr || goal->clearFootprint.has_value() || item == 0) {
					break;
				}
				const GoalTask* root = goals.getGoal(goals.rootOf(goal->id));
				if (root == nullptr) {
					break;
				}
				// The cut's in-flight covers what the colonist already carries toward the root as well as
				// what this cut lands, so its goods stay committed between cuts and nobody re-cuts them.
				// Food it carries that no earlier cut handed on is its own provisions.
				const uint32_t held = ecs::availableQuantity(inventory, registry.getDefName(item));
				const auto*	   itemDef = registry.getDefinitionById(item);
				const bool	   edible = itemDef != nullptr && itemDef->isEdible();
				const uint32_t wanted =
					rootAvailable(needs.get(world, root->id, item, harvestBasis(*root)), claims, root->id, item, ClaimExclusion::of(task));
				const uint32_t carried = std::min({handedOver > 0 ? handedOver : edible ? 0U : held, held, wanted});
				const uint32_t expected = std::min(mostPerCut(registry, memory, inventory, task), wanted - carried);
				if (carried > 0) {
					push(task, Claim::inbound(static_cast<uint64_t>(root->destinationEntity), item, carried));
				}
				if (carried + expected > 0) {
					push(task, Claim::inFlight(root->id, item, carried + expected));
				}
				break;
			}
			case TaskType::Haul: {
				const uint32_t item = registry.getDefNameId(task.haulItemDefName);
				const uint64_t root = goals.rootOf(task.haulGoalId);
				takeCargoClaims(task, item);
				if (item == 0 || root == 0 || task.haulQuantity == 0) {
					break;
				}
				if (!task.haulFromInventory) {
					if (task.haulSourceStorageId != 0) {
						push(task, Claim::sourceQuantity(TargetKind::Entity, task.haulSourceStorageId, item, task.haulQuantity));
					} else {
						push(task, Claim::sourceQuantity(TargetKind::WorldKey, hashWorldEntity(task.haulSourcePosition, item), item, task.haulQuantity));
					}
				}
				push(task, Claim::inbound(task.haulTargetStorageId, item, task.haulQuantity));
				push(task, Claim::inFlight(root, item, task.haulQuantity));
				break;
			}
			case TaskType::PlacePackaged:
				// One already in this colonist's hands is possessed; nobody else can take it.
				if (inventory.carryingPackagedEntity != task.placePackagedEntityId) {
					push(task, Claim::use(TargetKind::Entity, task.placePackagedEntityId));
				}
				break;
			case TaskType::Craft:
				push(task, Claim::slot(task.targetStationId));
				break;
			case TaskType::FulfillNeed:
				// Food the colonist walks to; food in its own pack is possessed, and water is unbounded.
				if (task.needToFulfill == NeedType::Hunger && task.needSourceKey != 0) {
					push(task, Claim::use(TargetKind::WorldKey, task.needSourceKey));
				}
				break;
			case TaskType::Build:
			case TaskType::Deconstruct:
			case TaskType::Wander:
			case TaskType::None:
				break;
		}
	}

	void claimsToDeliveryLeg(Task& task, uint32_t carried) {
		for (Claim& claim : task.claims) {
			if (claim.kind() == ClaimKind::Inbound || claim.kind() == ClaimKind::InFlight) {
				claim.setQuantity(std::min(claim.quantity(), carried));
			}
		}
		task.claims.eraseIf([](const Claim& claim) {
			return claim.kind() == ClaimKind::Use || claim.kind() == ClaimKind::SourceQuantity || claim.quantity() == 0;
		});
		if (task.type == TaskType::Haul) {
			task.haulQuantity = std::min(task.haulQuantity, carried);
		}
	}

	void harvestToDeliveryLeg(Task& task, uint64_t rootGoalId, EntityID destination, uint32_t itemDefNameId, uint32_t quantity) {
		task.claims.clear();
		push(task, Claim::inbound(static_cast<uint64_t>(destination), itemDefNameId, quantity));
		push(task, Claim::inFlight(rootGoalId, itemDefNameId, quantity));
	}

	std::optional<SuspendedWork> suspendableWork(const assets::AssetRegistry& registry, const Task& task, uint32_t held) {
		if ((task.type != TaskType::Haul && task.type != TaskType::Harvest) || held == 0 || task.instanceId == 0) {
			return std::nullopt;
		}
		const uint32_t item = chainItem(registry, task);
		uint64_t	   root = 0;
		for (const Claim& claim : task.claims) {
			if (claim.kind() == ClaimKind::InFlight && claim.itemDefNameId() == item) {
				root = claim.target();
			}
		}
		const uint32_t cargo = std::min(cargoOnClaims(task.claims, root, item), held);
		if (root == 0 || cargo == 0) {
			return std::nullopt; // the goods held are not this chain's yet
		}
		auto record = handOverCargo(task, root, item);
		for (Claim& claim : record->claims) {
			claim.setQuantity(cargo);
		}
		return record;
	}

	void refreshSuspended(const assets::AssetRegistry& registry, const Inventory& inventory, Task& task) {
		if (!task.suspended) {
			return;
		}
		SuspendedWork& record = *task.suspended;
		const uint32_t held = ecs::availableQuantity(inventory, registry.getDefName(record.itemDefNameId));
		if (held == 0 || GoalTaskRegistry::Get().getGoal(record.rootGoalId) == nullptr) {
			task.discardSuspended();
			return;
		}
		for (Claim& claim : record.claims) {
			claim.setQuantity(std::min(claim.quantity(), held));
		}
	}

	bool adoptsSuspended(const assets::AssetRegistry& registry, const Task& task) {
		if (!task.suspended.has_value()) {
			return false;
		}
		const auto&		goals = GoalTaskRegistry::Get();
		const uint64_t	goalId = task.type == TaskType::Haul ? task.haulGoalId : task.harvestGoalId;
		const GoalTask* root = goals.getGoal(goals.rootOf(goalId));
		const bool		carries = (task.type == TaskType::Haul && task.haulFromInventory) || task.type == TaskType::Harvest;
		if (!carries || root == nullptr || root->id != task.suspended->rootGoalId || chainItem(registry, task) != task.suspended->itemDefNameId) {
			return false;
		}
		const uint64_t destination = task.type == TaskType::Haul ? task.haulTargetStorageId : static_cast<uint64_t>(root->destinationEntity);
		return std::any_of(task.suspended->claims.begin(), task.suspended->claims.end(), [&](const Claim& claim) {
			return claim.kind() == ClaimKind::Inbound && claim.target() == destination;
		});
	}

	void takeOverCargo(Task& task, const SuspendedWork& cargo) {
		task.instanceId = cargo.instanceId;
		task.claimSeq = cargo.claimSeq;
		task.claims.clear();
		for (const Claim& claim : cargo.claims) {
			push(task, claim);
		}
	}

} // namespace ecs
