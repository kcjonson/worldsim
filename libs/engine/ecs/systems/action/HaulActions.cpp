#include "../ActionSystem.h"

#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Appearance.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Movement.h"
#include "../../components/NavPath.h"
#include "../../components/Packaged.h"
#include "../../components/StructureBlueprint.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"
#include "../../components/WorkQueue.h"

#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"

#include <utils/Log.h>

#include <algorithm>
#include <optional>
#include <utility>

namespace ecs {

	namespace {
		// Remaining units of `itemDefName` a crafting station still needs for its current recipe:
		// the recipe's required count minus what its store already holds (0 if already satisfied or
		// the item isn't a recipe input). This is the crafting-station analogue of
		// StructureBlueprint::remaining() -- both express "how much more does this destination need",
		// the single concept the metered deposit below caps every delivery to so neither a build site
		// nor a station ever banks surplus beyond its bill of materials.
		uint32_t craftStationRemaining(World* world, EntityID station, const Inventory& store, const std::string& itemDefName) {
			const auto* workQueue = world->getComponent<WorkQueue>(station);
			if (workQueue == nullptr) {
				return 0;
			}
			const CraftingJob* job = workQueue->getNextJob();
			if (job == nullptr) {
				return 0;
			}
			const auto* recipe = engine::assets::RecipeRegistry::Get().getRecipe(job->recipeDefName);
			if (recipe == nullptr) {
				return 0;
			}
			for (const auto& input : recipe->inputs) {
				if (input.defName == itemDefName) {
					const uint32_t inStore = store.getQuantity(itemDefName);
					return input.count > inStore ? input.count - inStore : 0U;
				}
			}
			return 0;
		}
	} // namespace

	void ActionSystem::applyDepositEffect(const Action& action, Task& task, Inventory& inventory) {
		const auto& depEff = action.depositEffect();

		auto&	   registry = engine::assets::AssetRegistry::Get();
		const bool twoHand	= ecs::itemIsTwoHand(registry, depEff.itemDefName);

		// Craft-material delivery, mirroring the build-site path: the material is MOVED from the
		// colonist's pack/hands into the crafting station's own Inventory store. The colonist's
		// pack empties; the station holds the staged materials, and the Craft action later
		// consumes them from the station (not from the colonist). Crediting the Haul goal by what
		// physically landed in the station lifts the parent Craft goal out of Blocked once every
		// input is present, exactly as construction gates on materialsComplete().
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
			const uint32_t toDeposit = std::min(carried, craftStationRemaining(world, stationEntity, *stationInv, depEff.itemDefName));
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

			if (added > 0 && task.type == TaskType::Haul && task.haulGoalId != 0) {
				auto& goalRegistry = GoalTaskRegistry::Get();
				goalRegistry.recordDelivery(task.haulGoalId, added);

				const auto* goal = goalRegistry.getGoal(task.haulGoalId);
				if (goal != nullptr && goal->availableCapacity() == 0) {
					goalRegistry.removeGoal(task.haulGoalId);
				}
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
					// Retire signal depends on the goal kind:
					//  - A stocking carry-in (StorageGoalSystem haul WITH a chainId) is a one-shot
					//    delivery of a single chopped armful: done once it actually deposits (added > 0),
					//    like a craft/construction inventory haul. If the box bounced the whole load
					//    (added == 0, a race against another deposit filling it), the goal stays live and
					//    re-drives to the box rather than stranding the wood in hand. StorageGoalSystem
					//    re-emits a fresh Harvest next tick if the box still wants more.
					//  - A normal storage haul's targetAmount is a slot count and its deliveredAmount
					//    sums across item types, so availableCapacity() is the wrong "done" signal -- it
					//    would retire after a single stack. Retire it on the storage's LIVE state
					//    instead: done once no free slot remains.
					//  - Other (craft/construction) goals that deposit into a storage Inventory keep
					//    using their counter.
					bool done = false;
					if (goal != nullptr) {
						if (goal->owner == GoalOwner::StorageGoalSystem) {
							done = goal->chainId.has_value() ? (added > 0) : !storageInv->hasSpace();
						} else {
							done = goal->availableCapacity() == 0;
						}
					}
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

		// Clear the colonist's carrying state and hands. Empty hands are only a problem when this
		// colonist was actually carrying THIS box: a crafted-furniture box spawns AT its place
		// target (source == target), so startPlacePackagedAction skips the degenerate pickup phase
		// and goes straight to phase 2 -- the box installs correctly with hands legitimately empty.
		// carryingPackagedEntity (set only by a real PickupPackaged) is the authoritative carry
		// state; warn only when it says we should be holding the box yet the hands are empty.
		const bool shouldBeCarrying = inventory.carryingPackagedEntity.has_value()
									  && inventory.carryingPackagedEntity.value() == placeEff.packagedEntityId;
		if (shouldBeCarrying && !inventory.leftHand.has_value() && !inventory.rightHand.has_value()) {
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

	void ActionSystem::startHaulAction(EntityID entity, Task& task, Action& action, const Position& position, Memory& memory, const Inventory& inventory) {
		auto& registry = engine::assets::AssetRegistry::Get();

		constexpr float kPositionTolerance = 0.5F;

		// Inventory-source haul: the colonist already carries the harvested items, so there is
		// no ground pickup. It walks to the destination and deposits. Single-phase. The deposit
		// mode depends on the destination: a build site (blueprint) records the material onto its
		// manifest; a crafting station (WorkQueue) moves it into the station's own store and
		// credits the parent Craft goal. The station is identified by its WorkQueue, since it now
		// also carries an Inventory store just like a plain storage container does.
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
				targetIsCraftStation ? "crafting station" : "build site",
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
		const bool carryingHaulItem = ecs::availableQuantity(inventory, task.haulItemDefName) > 0;

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
					// A pickup from a ground source takes only what's needed, never more than is
					// there. For a craft-material fetch the request is the EXACT remaining recipe need
					// (target minus delivered minus already carried): a multi-unit ResourceStack pile
					// then yields exactly that and leaves the surplus in the pile (clamped in
					// applyCollectionEffect) -- a 7-unit pile for a recipe needing 2 leaves 5 behind;
					// a single loose item is capped to its own carryable count there, so a pickup
					// never mints more than is present. A non-craft haul keeps the asset default. This
					// contrasts with a HARVEST, which takes all it can carry so freshly cut resources
					// aren't stranded at a far harvest spot.
					uint32_t	pickupQty = carryableCap.quantity;
					const auto* goal = task.haulGoalId != 0 ? GoalTaskRegistry::Get().getGoal(task.haulGoalId) : nullptr;
					if (goal != nullptr && goal->owner == GoalOwner::CraftingGoalSystem && goal->targetAmount > 0) {
						// targetAmount is an item count for a craft haul, so the remaining need is exact.
						const uint32_t carried = ecs::availableQuantity(inventory, task.haulItemDefName);
						const uint32_t accountedFor = goal->deliveredAmount + carried;
						pickupQty = goal->targetAmount > accountedFor ? goal->targetAmount - accountedFor : 0U;
						if (pickupQty == 0) {
							// Already delivered/carrying enough for the goal: nothing more to pick up.
							// Clear so the AI re-evaluates (on to the deposit).
							action.clear();
							return;
						}
					}
					action = Action::Pickup(defName, pickupQty, entity.position, defName);
					LOG_DEBUG(
						Engine,
						"[Action] Haul phase 1: Pickup %u x %s at (%.1f, %.1f)",
						pickupQty,
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

			// A crafting station (WorkQueue) takes the goods into its own store and credits the
			// parent Craft goal; a build site or plain storage container takes them as before.
			// The station carries an Inventory too, so the WorkQueue is what distinguishes it.
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
			// test on the option is unreliable. Mirror completeAction's Pickup->Deposit handoff:
			// re-point the task at the destination, set it Moving, advance the chain step, re-point
			// MovementTarget at the goal, and drop the stale pickup-leg route so the AI's per-tick
			// chain-leg repath plans a fresh A* path. The colonist walks to the box and deposits.
			task.targetPosition = task.haulTargetPosition;
			task.state = TaskState::Moving; // Must be Moving for MovementSystem to re-arrive at the destination
			++task.chainStep;

			if (auto* movementTarget = world->getComponent<MovementTarget>(entity)) {
				movementTarget->target = task.haulTargetPosition;
				movementTarget->active = true;
			}
			if (auto* navPath = world->getComponent<NavPath>(entity)) {
				navPath->valid = false;
			}

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

	void ActionSystem::stowHeldToolsForArmful(Inventory& inventory, glm::vec2 dropPosition) {
		// A held one-hand tool blocks a two-hand armful (both hands needed). Move it out of the way
		// (belt -> pack -> drop). The drop is per-unit loose items, matching clearHandItem's one-hand
		// drop; a tool is quantity 1, so this is a single dropped axe at the colonist's feet.
		ecs::stowHeldOneHandItemsToFreeHands(
			inventory,
			engine::assets::AssetRegistry::Get(),
			[this, dropPosition](const std::string& defName, uint32_t count) {
				if (m_onDropItem) {
					for (uint32_t i = 0; i < count; ++i) {
						m_onDropItem(defName, dropPosition.x, dropPosition.y);
					}
				} else {
					LOG_WARNING(
						Engine,
						"[Action] Cannot drop %u x %s freeing hands for armful - drop callback not configured (lost)",
						count,
						defName.c_str()
					);
				}
			}
		);
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
