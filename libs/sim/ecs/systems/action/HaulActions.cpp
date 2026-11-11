#include "../ActionSystem.h"

#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../StoreOrDrop.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Appearance.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Movement.h"
#include "../../components/NavPath.h"
#include "../../components/Packaged.h"
#include "../../components/StorageConfiguration.h"
#include "../../components/StructureBlueprint.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"
#include "../../components/WorkQueue.h"
#include "../ChainClaims.h"

#include "assets/AssetRegistry.h"

#include <utils/Log.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace ecs {

	void ActionSystem::applyDepositEffect(const Action& action, Inventory& inventory) {
		const auto& depEff = action.depositEffect();

		auto&	   registry = assets::AssetRegistry::Get();
		const bool twoHand	= ecs::itemIsTwoHand(registry, depEff.itemDefName);

		// Craft-material delivery, mirroring the build-site path: the material is MOVED from the
		// colonist's pack/hands into the crafting station's own Inventory store. The colonist's
		// pack empties; the station holds the staged materials, and the Craft action later
		// consumes them from the station (not from the colonist). What lands in the store is what the
		// Craft root's remaining need reads, so once every input is present the goal system lifts the
		// Craft goal out of Blocked, exactly as construction gates on materialsComplete().
		if (depEff.deliverToCraftStation) {
			const auto stationEntity = static_cast<EntityID>(depEff.storageEntityId);
			auto*	   stationInv = world->getComponent<Inventory>(stationEntity);
			if (stationInv == nullptr) {
				LOG_WARNING(
					Engine,
					"[Action] Craft delivery of %s: station %llu has no Inventory store, items kept in pack",
					depEff.itemDefName.c_str(),
					static_cast<unsigned long long>(depEff.storageEntityId)
				);
				return;
			}

			// Meter the deposit to the station's REMAINING recipe need (same invariant as a build
			// site's StructureBlueprint::remaining()): the store only ever accumulates up to exactly
			// the recipe's bill of materials, never a surplus. A colonist who cut more than the recipe
			// needs (a Reed yields up to 3 Plant Fiber, the recipe wants 1) deposits only the
			// shortfall and keeps the rest on himself. Banking the excess would masquerade as
			// available stock for later crafts and mis-route their provisioning.
			const uint32_t carried = ecs::availableQuantity(inventory, depEff.itemDefName);
			const uint32_t toDeposit =
				std::min({depEff.quantity, carried, craftStationNeed(*world, stationEntity, registry.getDefNameId(depEff.itemDefName))});
			if (toDeposit == 0) {
				// Station already holds the full requirement for this input; keep the surplus carried.
				LOG_DEBUG(Engine, "[Action] Craft deposit skipped: station %llu already has enough %s",
					static_cast<unsigned long long>(stationEntity), depEff.itemDefName.c_str());
				return;
			}
			uint32_t removed = twoHand ? ecs::removeFromHands(inventory, depEff.itemDefName, toDeposit)
									   : inventory.removeItem(depEff.itemDefName, toDeposit);
			if (removed == 0) {
				LOG_WARNING(Engine, "[Action] Craft deposit failed: %s not in inventory", depEff.itemDefName.c_str());
				return;
			}

			// removed is already capped to the remaining recipe need, so the store has room for all
			// of it (createForStorage gives ample slots); add and credit exactly that.
			uint32_t added = stationInv->addItem(depEff.itemDefName, removed);
			if (added < removed) {
				// Defensive: store slot-limited below the metered amount. Keep the surplus carried.
				uint32_t leftover = removed - added;
				if (twoHand) {
					ecs::addArmful(inventory, registry, depEff.itemDefName, leftover);
				} else {
					inventory.addItem(depEff.itemDefName, leftover);
				}
				LOG_WARNING(Engine, "[Action] Craft station full: deposited %u of %u x %s", added, removed, depEff.itemDefName.c_str());
			}

			LOG_INFO(
				Engine,
				"[Action] Delivered %u x %s into crafting station %llu (now in station store)",
				added,
				depEff.itemDefName.c_str(),
				static_cast<unsigned long long>(depEff.storageEntityId)
			);
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
			} else if (auto* storageInv = world->getComponent<Inventory>(storageEntity)) {
				// A box takes what its configuration still wants, never past an item's configured max, and
				// what fits; the colonist keeps the rest.
				uint32_t takes = removed;
				if (const auto* config = world->getComponent<StorageConfiguration>(storageEntity)) {
					const auto* itemDef = registry.getDefinition(depEff.itemDefName);
					takes = std::min(
						removed,
						storageRoomFor(*config, *storageInv, depEff.itemDefName, itemDef != nullptr ? itemDef->category : assets::ItemCategory::None)
					);
				}
				uint32_t added = storageInv->addItem(depEff.itemDefName, takes);
				if (added < removed) {
					uint32_t leftover = removed - added;
					if (twoHand) {
						ecs::addArmful(inventory, registry, depEff.itemDefName, leftover);
					} else {
						inventory.addItem(depEff.itemDefName, leftover);
					}
					LOG_DEBUG(Engine, "[Action] Storage took %u of %u x %s; the rest stays carried", added, removed, depEff.itemDefName.c_str());
				} else {
					LOG_INFO(
						Engine,
						"[Action] Deposited %u x %s into storage %llu",
						added,
						depEff.itemDefName.c_str(),
						static_cast<unsigned long long>(depEff.storageEntityId)
					);
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

		// Release the box from the hands only when this colonist actually carried it. A crafted-
		// furniture box spawns AT its place target (source == target), so startPlacePackagedAction
		// skips the degenerate pickup phase and goes straight to phase 2: the hands never held the
		// box and may hold something else (the axe just crafted at the same station), which must
		// survive the install. carryingPackagedEntity (set only by a real PickupPackaged) is the
		// authoritative carry state.
		const bool carried = inventory.carryingPackagedEntity.has_value()
							 && inventory.carryingPackagedEntity.value() == placeEff.packagedEntityId;
		if (carried) {
			if (!inventory.leftHand.has_value() && !inventory.rightHand.has_value()) {
				LOG_WARNING(
					Engine,
					"[Action] Colonist completed PlacePackaged but hands were already empty - pickup phase may have failed"
				);
			}
			inventory.carryingPackagedEntity.reset();
			inventory.leftHand.reset();
			inventory.rightHand.reset();
		}

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

	void ActionSystem::startHaulAction(EntityID entity, Task& task, Action& action, const Position& position, Memory& memory, const Inventory& inventory) {
		auto& registry = assets::AssetRegistry::Get();

		constexpr float kPositionTolerance = 0.5F;

		// Inventory-source haul: the colonist already carries the harvested items, so there is
		// no ground pickup. It walks to the destination and deposits. Single-phase. The deposit
		// mode depends on the destination: a build site (blueprint) records the material onto its
		// manifest; a crafting station (WorkQueue) moves it into the station's own store. The station
		// is identified by its WorkQueue, since it also carries an Inventory store just like a plain
		// storage container does.
		if (task.haulFromInventory) {
			const auto targetEntity = static_cast<EntityID>(task.haulTargetStorageId);
			const bool targetIsCraftStation = world->hasComponent<WorkQueue>(targetEntity);
			action = Action::Deposit(
				task.haulItemDefName,
				task.haulQuantity,
				task.haulTargetStorageId,
				task.haulTargetPosition,
				/*deliverToCraftStation=*/targetIsCraftStation
			);
			LOG_DEBUG(
				Engine,
				"[Action] Haul-from-inventory: deliver %u x %s to %s %llu",
				task.haulQuantity,
				task.haulItemDefName.c_str(),
				targetIsCraftStation													? "crafting station"
				: world->hasComponent<StructureBlueprint>(targetEntity) ? "build site"
																		: "storage box",
				static_cast<unsigned long long>(task.haulTargetStorageId)
			);
			return;
		}

		// Standard haul is a two-phase task:
		// Phase 1: At source position → Pickup the item
		// Phase 2: At storage position → Deposit the item
		//
		// Phase is carry-state-first, position-second. A haul deposits only once it actually
		// carries the item; until then it must pick up. Position alone is ambiguous when the
		// source pile and the destination sit within arrival tolerance of each other (a loose
		// stack right beside the crafting station): the colonist is then atSource AND atTarget,
		// and a position-only check would Deposit an empty pack, fail, and re-issue the same
		// fetch every tick -- an infinite "deposit nothing -> refetch" loop that strands the
		// craft half-provisioned.
		const auto*	   haulItemDef = registry.getDefinition(task.haulItemDefName);
		const uint32_t cargoHeld = cargoToward(task, GoalTaskRegistry::Get().rootOf(task.haulGoalId), registry.getDefNameId(task.haulItemDefName),
											   ecs::availableQuantity(inventory, task.haulItemDefName), haulItemDef != nullptr && haulItemDef->isEdible());
		// A pile pickup that took a gathering over still lifts its own share beside the goods its claims
		// already carry; only goods beyond those mean the pickup is skipped.
		const auto sourceClaim = std::find_if(task.claims.begin(), task.claims.end(), [](const Claim& claim) {
			return claim.kind() == ClaimKind::SourceQuantity;
		});
		const bool pickupPending = sourceClaim != task.claims.end();
		const bool carryingHaulItem = cargoHeld > (pickupPending ? taskCargo(registry, task) : 0U);

		glm::vec2 diffToSource = position.value - task.haulSourcePosition;
		float	  distSqToSource = diffToSource.x * diffToSource.x + diffToSource.y * diffToSource.y;
		bool	  atSource = distSqToSource <= kPositionTolerance * kPositionTolerance;

		glm::vec2 diffToTarget = position.value - task.haulTargetPosition;
		float	  distSqToTarget = diffToTarget.x * diffToTarget.x + diffToTarget.y * diffToTarget.y;
		bool	  atTarget = distSqToTarget <= kPositionTolerance * kPositionTolerance;

		// Not carrying yet: this is the pickup leg, regardless of also being in range of the
		// destination. Pick up at the source.
		if (!carryingHaulItem && atSource) {
			// Storage-to-storage pull: the source is a BOX, not a loose ground pile. Emit Withdraw
			// (the box-withdraw variant of Pickup); its effect removes the item from the source box's
			// Inventory (clamped to the live quantity) and adds it to the colonist. No memory scan: the
			// source box and quantity are already resolved on the task.
			if (task.haulSourceStorageId != 0) {
				const auto sourceBox = static_cast<EntityID>(task.haulSourceStorageId);
				if (world->hasComponent<Packaged>(sourceBox)) {
					// Source box is being moved -- abandon and re-resolve rather than withdrawing mid-move.
					LOG_WARNING(
						Engine,
						"[Action] Haul pull aborted: source box %llu is packaged (being moved)",
						static_cast<unsigned long long>(task.haulSourceStorageId)
					);
					task.clear();
					action.clear();
					return;
				}
				action = Action::Withdraw(task.haulItemDefName, task.haulQuantity, task.haulSourceStorageId, task.haulSourcePosition);
				LOG_DEBUG(
					Engine,
					"[Action] Haul phase 1: Withdraw %u x %s from box %llu at (%.1f, %.1f)",
					task.haulQuantity,
					task.haulItemDefName.c_str(),
					static_cast<unsigned long long>(task.haulSourceStorageId),
					task.haulSourcePosition.x,
					task.haulSourcePosition.y
				);
				return;
			}

			// Phase 1: At source - do Pickup. The decision chose the remembered pile it set as the source,
			// so it is found by its Memory key (the source position and the item), not by a memory scan.
			std::optional<std::pair<glm::vec2, uint32_t>> staleAtSource;
			if (const KnownWorldEntity* entity =
					memory.getWorldEntity(hashWorldEntity(task.haulSourcePosition, registry.getDefNameId(task.haulItemDefName)))) {
				const auto& defName = task.haulItemDefName;
				const auto* def = registry.getDefinition(defName);
				if (def != nullptr && def->capabilities.carryable.has_value()) {
					// A pickup takes what its claim was sized to (the root's need net of in-flight, the
					// carry), never more than is there: a pile yields that and keeps the rest, a single
					// loose item is capped to its own carryable count (applyCollectionEffect). A harvest
					// instead takes all it can carry, so freshly cut goods aren't stranded at a far tree.
					const uint32_t pickupQty = pickupPending ? sourceClaim->quantity() : task.haulQuantity;
					action = Action::Pickup(defName, pickupQty, entity->position, defName);
					LOG_DEBUG(
						Engine,
						"[Action] Haul phase 1: Pickup %u x %s at (%.1f, %.1f)",
						pickupQty,
						defName.c_str(),
						entity->position.x,
						entity->position.y
					);
					return;
				}

				// Remembered but isn't actually carryable anymore - stale memory entry
				staleAtSource = std::make_pair(entity->position, entity->defNameId);
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
		} else if (carryingHaulItem && atTarget) {
			// Phase 2: carrying the item and at the destination - do Deposit.
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

			// Arriving with the goods in hand skips the pickup: the source claim is spent and the deposit
			// commits what is held. A crafting station (WorkQueue) takes the goods into its own store; a
			// build site or plain storage container takes them as before.
			claimsToDeliveryLeg(task, cargoHeld);
			const bool toCraftStation = world->hasComponent<WorkQueue>(storageEntity);
			action = Action::Deposit(
				task.haulItemDefName,
				task.haulQuantity,
				task.haulTargetStorageId,
				task.haulTargetPosition,
				/*deliverToCraftStation=*/toCraftStation
			);
			LOG_DEBUG(
				Engine,
				"[Action] Haul phase 2: Deposit %u x %s into storage %llu",
				task.haulQuantity,
				task.haulItemDefName.c_str(),
				static_cast<unsigned long long>(task.haulTargetStorageId)
			);
		} else if (carryingHaulItem) {
			// Carrying the haul item but standing somewhere other than the destination -- the
			// colonist arrived at the SOURCE pile already holding (often at carry cap) some of
			// that same item. The deposit clause above only fires when atTarget, and the pickup
			// clause needs empty hands, so without this the state falls to the lost-warning below,
			// clears to None, the AI re-routes back to the source (the storage-stocking option
			// targets the loose pile), and the colonist deadlocks: spamming "not at source or
			// target", never filling the box, action stuck at None.
			//
			// Redirect to the deposit leg, reading the colonist's ACTUAL carry state HERE at action
			// time -- not at option-build time, where an over-cap two-hand armful is momentarily
			// dropped and re-picked each tick (availableQuantity reads 0) and any "am I carrying?"
			// test on the option is unreliable. The colonist walks to the box and deposits.
			claimsToDeliveryLeg(task, cargoHeld);
			headToDeposit(entity, task);

			action.clear();
			LOG_DEBUG(
				Engine,
				"[Action] Haul: carrying %s at source, redirecting to deposit at (%.1f, %.1f)",
				task.haulItemDefName.c_str(),
				task.haulTargetPosition.x,
				task.haulTargetPosition.y
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
		bool canBackpack = !ecs::itemIsTwoHand(assets::AssetRegistry::Get(), itemName);

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
				// Backpack full - put away or drop what couldn't fit
				const uint32_t toDispose = quantity - added;
				ecs::storeOrDrop(*world, itemName, toDispose, dropPosition, m_onDropResource);
				LOG_DEBUG(Engine, "[Action] Got rid of %u x %s from %s hand (backpack full)", toDispose, itemName.c_str(), handName);
			}
		} else {
			// 2-handed item in hand - can't be packed, so put away or drop
			ecs::storeOrDrop(*world, itemName, quantity, dropPosition, m_onDropResource);
			LOG_DEBUG(Engine, "[Action] Got rid of %u x %s from %s hand (2-handed item)", quantity, itemName.c_str(), handName);
		}

		handSlot.reset();
	}

	void ActionSystem::stowHeldToolsForArmful(Inventory& inventory, glm::vec2 dropPosition) {
		// A held one-hand tool blocks a two-hand armful (both hands needed). Move it out of the way
		// (belt -> pack -> storage within reach -> drop). A tool is quantity 1, so a drop is a
		// single loose axe at the colonist's feet.
		ecs::stowHeldOneHandItemsToFreeHands(
			inventory,
			assets::AssetRegistry::Get(),
			[this, dropPosition](const std::string& defName, uint32_t count) {
				ecs::storeOrDrop(*world, defName, count, dropPosition, m_onDropResource);
			}
		);
	}

	void ActionSystem::clearHandsForTwoHandedPickup(Inventory& inventory, glm::vec2 dropPosition) {
		// A two-hand armful mirrors one stack across both hands; clearing each hand
		// independently would get rid of it twice. Handle the mirror once: put the whole
		// armful away (or drop it as one loose pile), free both hands.
		if (inventory.leftHand.has_value() && inventory.rightHand.has_value()
			&& inventory.leftHand->defName == inventory.rightHand->defName
			&& ecs::itemIsTwoHand(assets::AssetRegistry::Get(), inventory.leftHand->defName)) {
			const std::string defName  = inventory.leftHand->defName;
			const uint32_t	  quantity = inventory.leftHand->quantity;
			ecs::storeOrDrop(*world, defName, quantity, dropPosition, m_onDropResource);
			inventory.leftHand.reset();
			inventory.rightHand.reset();
			return;
		}
		clearHandItem(inventory.leftHand, inventory, dropPosition, "left");
		clearHandItem(inventory.rightHand, inventory, dropPosition, "right");
	}

} // namespace ecs
