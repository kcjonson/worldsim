#include "../ActionSystem.h"

#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Appearance.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Packaged.h"
#include "../../components/StructureBlueprint.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"

#include "assets/AssetRegistry.h"

#include <utils/Log.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace ecs {

	void ActionSystem::applyDepositEffect(const Action& action, Task& task, Inventory& inventory) {
		const auto& depEff = action.depositEffect();

		auto&	   registry = engine::assets::AssetRegistry::Get();
		const bool twoHand	= ecs::itemIsTwoHand(registry, depEff.itemDefName);

		// Craft-material delivery: the target is a crafting station with no storage
		// container. The harvested materials stay in the colonist's inventory (the Craft
		// action consumes them from there); arriving here just marks them delivered so the
		// parent Craft goal advances out of Blocked.
		if (depEff.deliverToCraftStation) {
			uint32_t carried = ecs::availableQuantity(inventory, depEff.itemDefName);
			uint32_t delivered = std::min(carried, depEff.quantity);
			if (delivered > 0 && task.type == TaskType::Haul && task.haulGoalId != 0) {
				auto& goalRegistry = GoalTaskRegistry::Get();
				goalRegistry.recordDelivery(task.haulGoalId, delivered);

				const auto* goal = goalRegistry.getGoal(task.haulGoalId);
				if (goal != nullptr && goal->availableCapacity() == 0) {
					goalRegistry.removeGoal(task.haulGoalId);
				}
				LOG_INFO(
					Engine,
					"[Action] Delivered %u x %s to crafting station %llu (kept in inventory for craft)",
					delivered,
					depEff.itemDefName.c_str(),
					static_cast<unsigned long long>(depEff.storageEntityId)
				);
			} else {
				LOG_WARNING(
					Engine,
					"[Action] Craft delivery of %s found nothing carried (carried=%u)",
					depEff.itemDefName.c_str(),
					carried
				);
			}
		} else {

		// Remove item from the colonist: two-hand goods come out of the hands, the rest the pack.
		uint32_t removed = twoHand ? ecs::removeFromHands(inventory, depEff.itemDefName, depEff.quantity)
								   : inventory.removeItem(depEff.itemDefName, depEff.quantity);
		if (removed > 0) {
			const auto storageEntity = static_cast<EntityID>(depEff.storageEntityId);

			// A build site (blueprint) holds no Inventory: the material is recorded straight onto
			// the blueprint's delivered[] manifest, capped at its requirement. A storage container
			// owns an Inventory and the goods land there. These are the only two deposit targets.
			if (auto* blueprint = world->getComponent<StructureBlueprint>(storageEntity)) {
				uint32_t recorded = blueprint->recordDelivery(depEff.itemDefName, removed);
				if (recorded < removed) {
					// Over the requirement: bounce the surplus back to the colonist (mirrored armful
					// for two-hand goods, backpack otherwise) rather than overfilling the manifest.
					uint32_t leftover = removed - recorded;
					if (twoHand) {
						ecs::addArmful(inventory, registry, depEff.itemDefName, leftover);
					} else {
						inventory.addItem(depEff.itemDefName, leftover);
					}
				}
				LOG_INFO(
					Engine,
					"[Action] Delivered %u x %s to build site %llu (recorded on manifest)",
					recorded,
					depEff.itemDefName.c_str(),
					static_cast<unsigned long long>(depEff.storageEntityId)
				);

				if (recorded > 0 && task.type == TaskType::Haul && task.haulGoalId != 0) {
					auto& goalRegistry = GoalTaskRegistry::Get();
					goalRegistry.recordDelivery(task.haulGoalId, recorded);

					const auto* goal = goalRegistry.getGoal(task.haulGoalId);
					if (goal != nullptr && goal->availableCapacity() == 0) {
						goalRegistry.removeGoal(task.haulGoalId);
					}
				}
			} else if (auto* storageInv = world->getComponent<Inventory>(storageEntity)) {
				uint32_t added = storageInv->addItem(depEff.itemDefName, removed);
				if (added < removed) {
					// Storage full - put remaining back in colonist inventory
					uint32_t leftover = removed - added;
					if (twoHand) {
						ecs::addArmful(inventory, registry, depEff.itemDefName, leftover);
					} else {
						inventory.addItem(depEff.itemDefName, leftover);
					}
					LOG_WARNING(Engine, "[Action] Storage full: deposited %u of %u x %s", added, removed, depEff.itemDefName.c_str());
				} else {
					LOG_INFO(
						Engine,
						"[Action] Deposited %u x %s into storage %llu",
						added,
						depEff.itemDefName.c_str(),
						static_cast<unsigned long long>(depEff.storageEntityId)
					);
				}

				if (task.type == TaskType::Haul && task.haulGoalId != 0) {
					auto& goalRegistry = GoalTaskRegistry::Get();
					// Credit only what actually landed. A full storage (partial or zero add) credits
					// nothing more.
					if (added > 0) {
						goalRegistry.recordDelivery(task.haulGoalId, added);
					}

					const auto* goal = goalRegistry.getGoal(task.haulGoalId);
					// A storage goal's targetAmount is a slot count and its deliveredAmount sums
					// across item types, so availableCapacity() is the wrong "done" signal -- it
					// would retire after a single stack. Retire it on the storage's LIVE state
					// instead: done once no free slot remains, whether or not this deposit landed
					// anything (StorageGoalSystem re-confirms each tick). Other goals that deposit
					// into a storage Inventory keep using their counter.
					const bool done = (goal != nullptr) &&
									  (goal->owner == GoalOwner::StorageGoalSystem ? !storageInv->hasSpace()
																				   : goal->availableCapacity() == 0);
					if (done) {
						goalRegistry.removeGoal(task.haulGoalId);
					}
				}
			} else {
				// Target is neither a build site nor a storage container (destroyed/gone) - put
				// items back, credit nothing.
				if (twoHand) {
					ecs::addArmful(inventory, registry, depEff.itemDefName, removed);
				} else {
					inventory.addItem(depEff.itemDefName, removed);
				}
				LOG_WARNING(
					Engine,
					"[Action] Deposit target %llu not found, items returned to inventory",
					static_cast<unsigned long long>(depEff.storageEntityId)
				);
			}
		} else {
			LOG_WARNING(Engine, "[Action] Deposit failed: %s not in inventory", depEff.itemDefName.c_str());
		}
		} // end storage-deposit branch
	}

	void ActionSystem::applyPlacePackagedEffect(const Action& action, Inventory& inventory) {
		const auto& placeEff = action.placePackagedEffect();

		// Get the packaged entity
		auto packagedEntity = static_cast<EntityID>(placeEff.packagedEntityId);

		// Clear the colonist's carrying state and hands
		// Log warning if hands were already empty (indicates pickup phase may have failed)
		if (!inventory.leftHand.has_value() && !inventory.rightHand.has_value()) {
			LOG_WARNING(
				Engine,
				"[Action] Colonist completed PlacePackaged but hands were already empty - pickup phase may have failed"
			);
		}
		inventory.carryingPackagedEntity.reset();
		inventory.leftHand.reset();
		inventory.rightHand.reset();

		// Verify entity still exists before manipulating it
		if (!world->isAlive(packagedEntity)) {
			LOG_WARNING(
				Engine,
				"[Action] Packaged entity %llu no longer alive at placement time",
				static_cast<unsigned long long>(placeEff.packagedEntityId)
			);
		} else {
			// Move entity to target position
			auto* entityPos = world->getComponent<Position>(packagedEntity);
			if (entityPos != nullptr) {
				entityPos->value = placeEff.targetPosition;
				LOG_INFO(
					Engine,
					"[Action] Placed entity %llu at (%.1f, %.1f)",
					static_cast<unsigned long long>(placeEff.packagedEntityId),
					placeEff.targetPosition.x,
					placeEff.targetPosition.y
				);
			}

			// Remove Packaged component - entity is now placed
			world->removeComponent<Packaged>(packagedEntity);
		}
	}

	void ActionSystem::startHaulAction(Task& task, Action& action, const Position& position, Memory& memory) {
		auto& registry = engine::assets::AssetRegistry::Get();

		constexpr float kPositionTolerance = 0.5F;

		// Inventory-source haul: the colonist already carries the harvested items, so there is
		// no ground pickup. It walks to the destination and deposits. Single-phase. The deposit
		// mode depends on the destination: a build site (blueprint) records the material onto its
		// manifest for real; a crafting station has no blueprint, so "delivery" just credits the
		// goal and the items stay in inventory for the Craft action to consume.
		if (task.haulFromInventory) {
			const bool targetIsBuildSite =
				world->hasComponent<StructureBlueprint>(static_cast<EntityID>(task.haulTargetStorageId));
			action = Action::Deposit(
				task.haulItemDefName,
				task.haulQuantity,
				task.haulTargetStorageId,
				task.haulTargetPosition,
				/*deliverToCraftStation=*/!targetIsBuildSite
			);
			LOG_DEBUG(
				Engine,
				"[Action] Haul-from-inventory: deliver %u x %s to %s %llu",
				task.haulQuantity,
				task.haulItemDefName.c_str(),
				targetIsBuildSite ? "build site" : "crafting station",
				static_cast<unsigned long long>(task.haulTargetStorageId)
			);
			return;
		}

		// Standard haul is a two-phase task:
		// Phase 1: At source position → Pickup the item
		// Phase 2: At storage position → Deposit the item
		// We determine which phase by checking which position we're closer to

		glm::vec2 diffToSource = position.value - task.haulSourcePosition;
		float	  distSqToSource = diffToSource.x * diffToSource.x + diffToSource.y * diffToSource.y;
		bool	  atSource = distSqToSource <= kPositionTolerance * kPositionTolerance;

		glm::vec2 diffToTarget = position.value - task.haulTargetPosition;
		float	  distSqToTarget = diffToTarget.x * diffToTarget.x + diffToTarget.y * diffToTarget.y;
		bool	  atTarget = distSqToTarget <= kPositionTolerance * kPositionTolerance;

		if (atSource && !atTarget) {
			// Phase 1: At source - do Pickup
			// Look for a carryable entity at the source position matching the item we want to haul
			std::optional<std::pair<glm::vec2, uint32_t>> staleAtSource;
			for (const auto& [key, entity] : memory.knownWorldEntities) {
				// Check if entity is at the source position
				glm::vec2 diff = entity.position - task.haulSourcePosition;
				float	  distSq = diff.x * diff.x + diff.y * diff.y;
				if (distSq > kPositionTolerance * kPositionTolerance) {
					continue;
				}

				const auto& defName = registry.getDefName(entity.defNameId);

				// Check if this is the item we want to haul
				if (defName != task.haulItemDefName) {
					continue;
				}

				const auto* def = registry.getDefinition(defName);
				if (def != nullptr && def->capabilities.carryable.has_value()) {
					const auto& carryableCap = def->capabilities.carryable.value();
					action = Action::Pickup(defName, carryableCap.quantity, entity.position, defName);
					LOG_DEBUG(
						Engine,
						"[Action] Haul phase 1: Pickup %s at (%.1f, %.1f)",
						defName.c_str(),
						entity.position.x,
						entity.position.y
					);
					return;
				}

				// Matches by name but isn't actually carryable anymore - stale memory entry
				staleAtSource = std::make_pair(entity.position, entity.defNameId);
			}

			// Pickup target gone - forget the stale entry so AIDecision stops re-selecting it.
			if (staleAtSource.has_value()) {
				memory.forgetWorldEntity(staleAtSource->first, staleAtSource->second);
			}

			LOG_WARNING(
				Engine,
				"[Action] Haul failed: item %s not found at (%.1f, %.1f)%s",
				task.haulItemDefName.c_str(),
				task.haulSourcePosition.x,
				task.haulSourcePosition.y,
				staleAtSource.has_value() ? " - forgot stale entry" : ""
			);
			action.clear();
		} else if (atTarget) {
			// Phase 2: At storage target - do Deposit (use same quantity as pickup)
			// First validate storage is still valid (not packaged/being moved)
			auto storageEntity = static_cast<EntityID>(task.haulTargetStorageId);
			if (world->hasComponent<Packaged>(storageEntity)) {
				LOG_WARNING(
					Engine,
					"[Action] Haul aborted: storage %llu is packaged (being moved)",
					static_cast<unsigned long long>(task.haulTargetStorageId)
				);
				task.clear();
				action.clear();
				return;
			}

			action = Action::Deposit(task.haulItemDefName, task.haulQuantity, task.haulTargetStorageId, task.haulTargetPosition);
			LOG_DEBUG(
				Engine,
				"[Action] Haul phase 2: Deposit %u x %s into storage %llu",
				task.haulQuantity,
				task.haulItemDefName.c_str(),
				static_cast<unsigned long long>(task.haulTargetStorageId)
			);
		} else {
			LOG_WARNING(Engine, "[Action] Haul started but not at source or target position");
			action.clear();
		}
	}

	void ActionSystem::startPlacePackagedAction(
		Task& task,
		Action& action,
		const Position& position,
		const Inventory& inventory
	) {
		// PlacePackaged is a two-phase task:
		// Phase 1: At source position → PickupPackaged (clear hands + pick up 2-handed item)
		// Phase 2: At target position → PlacePackaged (put down item at destination)
		//
		// Primary phase detection uses position, but we also check inventory state for
		// robustness in edge cases (e.g., colonist not exactly at expected position).

		constexpr float kPlacementPositionTolerance = 0.5F;

		glm::vec2 diffToSource = position.value - task.placeSourcePosition;
		float	  distSqToSource = diffToSource.x * diffToSource.x + diffToSource.y * diffToSource.y;
		bool	  atSource = distSqToSource <= kPlacementPositionTolerance * kPlacementPositionTolerance;

		glm::vec2 diffToTarget = position.value - task.placeTargetPosition;
		float	  distSqToTarget = diffToTarget.x * diffToTarget.x + diffToTarget.y * diffToTarget.y;
		bool	  atTarget = distSqToTarget <= kPlacementPositionTolerance * kPlacementPositionTolerance;

		// Check inventory to determine if we're already carrying the item (phase 2)
		bool alreadyCarrying = inventory.carryingPackagedEntity.has_value() &&
							   inventory.carryingPackagedEntity.value() == task.placePackagedEntityId;

		if (atSource && !atTarget && !alreadyCarrying) {
			// Phase 1: At source and not yet carrying - do PickupPackaged
			action = Action::PickupPackaged(task.placePackagedEntityId, task.placeSourcePosition);
			LOG_DEBUG(
				Engine,
				"[Action] PlacePackaged phase 1: PickupPackaged entity %llu at (%.1f, %.1f)",
				static_cast<unsigned long long>(task.placePackagedEntityId),
				task.placeSourcePosition.x,
				task.placeSourcePosition.y
			);
		} else if (atTarget || alreadyCarrying) {
			// Phase 2: At target OR already carrying - do PlacePackaged
			// If already carrying but not at target, this will still start the action
			// (movement system should get us to target)
			action = Action::PlacePackaged(task.placePackagedEntityId, task.placeTargetPosition);
			LOG_DEBUG(
				Engine,
				"[Action] PlacePackaged phase 2: PlacePackaged entity %llu at (%.1f, %.1f)%s",
				static_cast<unsigned long long>(task.placePackagedEntityId),
				task.placeTargetPosition.x,
				task.placeTargetPosition.y,
				alreadyCarrying && !atTarget ? " (carrying, not yet at target)" : ""
			);
		} else {
			// Fallback: Not at either position and not carrying
			// Default to phase 1 (pickup) since that's the logical starting point
			LOG_WARNING(
				Engine,
				"[Action] PlacePackaged: not at source or target, defaulting to phase 1 (pickup)"
			);
			action = Action::PickupPackaged(task.placePackagedEntityId, task.placeSourcePosition);
		}
	}

	void ActionSystem::clearHandItem(
		std::optional<ItemStack>& handSlot,
		Inventory&				  inventory,
		glm::vec2				  dropPosition,
		const char*				  handName
	) {
		if (!handSlot.has_value()) {
			return;
		}

		const auto& itemName = handSlot->defName;
		uint32_t	quantity = handSlot->quantity;

		// Check if item can go in backpack (1-handed items only)
		bool canBackpack = !ecs::itemIsTwoHand(engine::assets::AssetRegistry::Get(), itemName);

		if (canBackpack) {
			// One-hand item: try the belt first (quick-draw tool slot), then the backpack, then drop.
			// A single belt slot holds one item; only attempt it for the common quantity-1 hand stack.
			if (quantity == 1 && inventory.stowToBelt(itemName)) {
				LOG_DEBUG(Engine, "[Action] Stowed %s from %s hand to belt", itemName.c_str(), handName);
				handSlot.reset();
				return;
			}

			// Try to stow in backpack
			uint32_t added = inventory.addItem(itemName, quantity);
			if (added == quantity) {
				LOG_DEBUG(Engine, "[Action] Stowed %u x %s from %s hand to backpack", quantity, itemName.c_str(), handName);
			} else {
				// Backpack full - drop what couldn't fit
				uint32_t toDrop = quantity - added;
				if (m_onDropItem) {
					for (uint32_t i = 0; i < toDrop; ++i) {
						m_onDropItem(itemName, dropPosition.x, dropPosition.y);
					}
					LOG_DEBUG(Engine, "[Action] Dropped %u x %s from %s hand (backpack full)", toDrop, itemName.c_str(), handName);
				} else {
					LOG_WARNING(
						Engine,
						"[Action] Cannot drop %u x %s from %s hand - drop callback not configured (items lost)",
						toDrop,
						itemName.c_str(),
						handName
					);
				}
			}
		} else {
			// 2-handed item in hand - must drop
			if (m_onDropItem) {
				for (uint32_t i = 0; i < quantity; ++i) {
					m_onDropItem(itemName, dropPosition.x, dropPosition.y);
				}
				LOG_DEBUG(Engine, "[Action] Dropped %u x %s from %s hand (2-handed item)", quantity, itemName.c_str(), handName);
			} else {
				LOG_WARNING(
					Engine,
					"[Action] Cannot drop %u x %s from %s hand - drop callback not configured (items lost)",
					quantity,
					itemName.c_str(),
					handName
				);
			}
		}

		handSlot.reset();
	}

	void ActionSystem::clearHandsForTwoHandedPickup(Inventory& inventory, glm::vec2 dropPosition) {
		// A two-hand armful mirrors one stack across both hands; clearing each hand
		// independently would drop it twice. Handle the mirror once: drop the whole
		// armful as a single loose pile (haulable, not per-unit packaged), free both hands.
		if (inventory.leftHand.has_value() && inventory.rightHand.has_value()
			&& inventory.leftHand->defName == inventory.rightHand->defName
			&& ecs::itemIsTwoHand(engine::assets::AssetRegistry::Get(), inventory.leftHand->defName)) {
			const std::string defName  = inventory.leftHand->defName;
			const uint32_t	  quantity = inventory.leftHand->quantity;
			if (m_onDropResource) {
				m_onDropResource(defName, dropPosition.x, dropPosition.y, quantity);
			} else if (m_onDropItem) {
				for (uint32_t i = 0; i < quantity; ++i) {
					m_onDropItem(defName, dropPosition.x, dropPosition.y);
				}
			}
			inventory.leftHand.reset();
			inventory.rightHand.reset();
			return;
		}
		clearHandItem(inventory.leftHand, inventory, dropPosition, "left");
		clearHandItem(inventory.rightHand, inventory, dropPosition, "right");
	}

} // namespace ecs
