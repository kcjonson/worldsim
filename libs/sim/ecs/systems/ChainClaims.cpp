#include "ChainClaims.h"

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

		/// What one cut of the remembered harvestable can bring in: its most per cut, capped by what the
		/// colonist can carry of the yield and by what the goal still wants. Over-estimating only makes
		/// another colonist briefly see less available; under-estimating would let it re-cut.
		uint32_t expectedYield(const assets::AssetRegistry& registry, const Memory& memory, const Inventory& inventory, const Task& task,
							   const GoalTask& goal) {
			const KnownWorldEntity* source = memory.getWorldEntity(task.harvestTargetEntityId);
			if (source == nullptr) {
				return 0;
			}
			const auto* def = registry.getDefinition(registry.getDefName(source->defNameId));
			if (def == nullptr || !def->capabilities.harvestable.has_value()) {
				return 0;
			}
			const std::string& yieldDefName = registry.getDefName(task.harvestYieldDefNameId);
			const uint32_t	   room = ecs::pickupUnitsThatFit(inventory, registry, yieldDefName, ecs::carriedCargoMassKg(inventory, registry));
			return std::min({def->capabilities.harvestable->amountMax, room, goal.availableCapacity()});
		}

	} // namespace

	void recordChainClaims(const assets::AssetRegistry& registry, const Memory& memory, const Inventory& inventory, Task& task) {
		const auto& goals = GoalTaskRegistry::Get();
		switch (task.type) {
			case TaskType::Harvest: {
				push(task, Claim::use(TargetKind::WorldKey, task.harvestTargetEntityId));
				// A footprint clear removes what stands there and brings nothing toward a need.
				const GoalTask* goal = goals.getGoal(task.harvestGoalId);
				if (goal == nullptr || goal->clearFootprint.has_value() || task.harvestYieldDefNameId == 0) {
					break;
				}
				if (const uint32_t expected = expectedYield(registry, memory, inventory, task, *goal); expected > 0) {
					push(task, Claim::inFlight(goals.rootOf(goal->id), task.harvestYieldDefNameId, expected));
				}
				break;
			}
			case TaskType::Haul: {
				const uint32_t item = registry.getDefNameId(task.haulItemDefName);
				const uint64_t root = goals.rootOf(task.haulGoalId);
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
	}

	std::optional<SuspendedWork> suspendableWork(const assets::AssetRegistry& registry, const Task& task, uint32_t held) {
		if (task.type != TaskType::Haul || held == 0) {
			return std::nullopt;
		}
		SuspendedWork record{.instanceId = task.instanceId, .claimSeq = task.claimSeq, .itemDefNameId = registry.getDefNameId(task.haulItemDefName)};
		for (Claim claim : task.claims) {
			switch (claim.kind()) {
				case ClaimKind::SourceQuantity:
				case ClaimKind::Use:
				case ClaimKind::Slot:
					return std::nullopt; // the goods held are not this haul's yet
				case ClaimKind::InFlight:
					record.rootGoalId = claim.target();
					[[fallthrough]];
				case ClaimKind::Inbound:
					if (claim.itemDefNameId() == record.itemDefNameId) {
						claim.setQuantity(std::min(claim.quantity(), held));
						if (!record.claims.push(claim)) {
							return std::nullopt;
						}
					}
					break;
			}
		}
		if (record.rootGoalId == 0 || record.instanceId == 0) {
			return std::nullopt;
		}
		return record;
	}

	bool adoptsSuspended(const assets::AssetRegistry& registry, const Task& task) {
		return task.suspended.has_value() && task.type == TaskType::Haul && task.haulFromInventory &&
			   GoalTaskRegistry::Get().rootOf(task.haulGoalId) == task.suspended->rootGoalId &&
			   registry.getDefNameId(task.haulItemDefName) == task.suspended->itemDefNameId;
	}

	void adoptSuspended(Task& task) {
		task.instanceId = task.suspended->instanceId;
		task.claimSeq = task.suspended->claimSeq;
		task.claims.clear();
		for (Claim claim : task.suspended->claims) {
			claim.setQuantity(std::min(claim.quantity(), task.haulQuantity));
			if (claim.quantity() > 0) {
				push(task, claim);
			}
		}
		task.discardSuspended();
	}

} // namespace ecs
