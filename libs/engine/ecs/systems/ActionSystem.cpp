#include "ActionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "TimeSystem.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/MemoryQueries.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Packaged.h"
#include "../components/Skills.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include "assets/AssetRegistry.h"
#include "assets/ItemProperties.h"
#include "assets/RecipeDef.h"
#include "assets/RecipeRegistry.h"

#include <utils/Log.h>

#include <algorithm>
#include <optional>
#include <random>
#include <utility>
#include <vector>

namespace ecs {

	namespace {

		/// Default ground quality for sleep (reduced recovery rate)
		constexpr float kGroundSleepQuality = 0.5F;

		/// Default water quality for drinking
		constexpr float kDefaultWaterQuality = 1.0F;

		/// Position tolerance for matching entities at target location (in tiles)
		constexpr float kPositionTolerance = 0.1F;

		/// Threshold for items requiring two hands (items with handsRequired >= this are two-handed)
		constexpr uint8_t kTwoHandedThreshold = 2;

	} // namespace

	void ActionSystem::update(float deltaTime) {
		// Actions advance on game time, so fast-forward (3x/10x) speeds up work and pause
		// freezes it. TimeSystem is always registered in-game; unit tools/tests that omit it
		// fall back to real time (scale 1.0), keeping action durations equal to seconds there.
		float timeScale = 1.0F;
		if (auto* timeSystem = world->tryGetSystem<TimeSystem>()) {
			timeScale = timeSystem->effectiveTimeScale();
		}
		const float scaledDt = deltaTime * timeScale;

		// Process all colonists with the required components.
		//
		// Action Interruption Policy: Once an action starts, it runs to completion.
		// If a colonist's task changes mid-action (e.g., AIDecisionSystem assigns a
		// higher-priority need), the action stops being processed but remains in an
		// incomplete state until the colonist returns and the task state becomes
		// Arrived again. This is intentional - colonists "commit" to actions rather
		// than abandoning half-eaten food or interrupted sleep. Future work may add
		// explicit action cancellation for emergencies (e.g., flee from danger).
		for (auto [entity, position, task, action, needs, memory, inventory] :
			 world->view<Position, Task, Action, NeedsComponent, Memory, Inventory>()) {

			// Only process entities that have arrived at their destination
			if (task.state != TaskState::Arrived) {
				continue;
			}

			// Only process actionable tasks (FulfillNeed, Gather, Craft, Haul, PlacePackaged,
			// Harvest, Build, Deconstruct)
			if (task.type != TaskType::FulfillNeed && task.type != TaskType::Gather && task.type != TaskType::Craft &&
				task.type != TaskType::Haul && task.type != TaskType::PlacePackaged && task.type != TaskType::Harvest &&
				task.type != TaskType::Build && task.type != TaskType::Deconstruct) {
				// For non-actionable tasks like Wander, just clear when arrived
				task.clear();
				task.timeSinceEvaluation = 0.0F;
				continue;
			}

			// Start action if not already active
			if (!action.isActive()) {
				if (task.type == TaskType::Build || task.type == TaskType::Deconstruct) {
					startBuildAction(entity, task, action);
				} else {
					startAction(task, action, position, memory, needs, inventory);
				}
				LOG_INFO(
					Engine,
					"[Action] Entity %llu: Started %s action (%.1fs duration)",
					static_cast<unsigned long long>(entity),
					actionTypeName(action.type),
					action.duration
				);
			}

			// Construction actions advance continuously by workDone, not by elapsed/duration.
			// A failed start (e.g. blueprint gone) clears the action, so guard on the effect.
			if (action.hasProgressEffect()) {
				advanceConstructionWork(scaledDt, action);
			} else {
				// Process the action
				processAction(scaledDt, action, needs, task);
			}

			// Update WorkQueue progress for craft actions (for UI progress bar)
			if (action.hasCraftingEffect() && action.isActive()) {
				const auto& craftEff = action.craftingEffect();
				auto*		workQueue = world->getComponent<WorkQueue>(static_cast<EntityID>(craftEff.stationEntityId));
				if (workQueue != nullptr) {
					workQueue->progress = action.progress();
				}
			}

			// Complete action if done
			if (action.isComplete()) {
				float restoreAmount = 0.0F;
				if (action.hasNeedEffect()) {
					restoreAmount = action.needEffect().restoreAmount;
				}
				LOG_INFO(
					Engine,
					"[Action] Entity %llu: Completed %s action (restored %.1f%%)",
					static_cast<unsigned long long>(entity),
					actionTypeName(action.type),
					restoreAmount
				);
				completeAction(entity, action, needs, task, inventory, memory);
			}
		}
	}

	void ActionSystem::startAction(
		Task&				  task,
		Action&				  action,
		const Position&		  position,
		Memory&				  memory,
		const NeedsComponent& needs,
		const Inventory&	  inventory
	) {
		auto& registry = engine::assets::AssetRegistry::Get();

		// Handle Craft tasks separately
		if (task.type == TaskType::Craft) {
			startCraftAction(task, action, inventory);
			return;
		}

		// Handle Gather tasks - pickup or harvest materials for crafting
		if (task.type == TaskType::Gather) {
			startGatherAction(task, action, position, memory);
			return;
		}

		// Handle Haul tasks - pickup from source, then deposit to storage
		if (task.type == TaskType::Haul) {
			startHaulAction(task, action, position, memory);
			return;
		}

		// Handle PlacePackaged tasks - pickup packaged item, then place at target
		if (task.type == TaskType::PlacePackaged) {
			startPlacePackagedAction(task, action, position, inventory);
			return;
		}

		// Handle Harvest tasks (goal-driven harvesting for crafting materials)
		if (task.type == TaskType::Harvest) {
			startHarvestAction(task, action, position, memory, inventory);
			return;
		}

		// Handle FulfillNeed tasks
		switch (task.needToFulfill) {
			case NeedType::Hunger: {
				// Priority 1: Check inventory for any edible food (data-driven)
				for (const auto& edibleItemName : engine::assets::getEdibleItemNames()) {
					if (inventory.hasItem(edibleItemName)) {
						// Get nutrition from item properties
						auto  edibleInfo = engine::assets::getEdibleItemInfo(edibleItemName);
						float nutrition = edibleInfo ? edibleInfo->nutrition : 0.3F;
						action = Action::Eat(edibleItemName, nutrition);
						LOG_DEBUG(
							Engine,
							"[Action] Creating Eat action for %s (nutrition %.2f, qty %u)",
							edibleItemName.c_str(),
							nutrition,
							inventory.getQuantity(edibleItemName)
						);
						break;
					}
				}

				// If we already created an eat action from inventory, we're done
				if (action.type == ActionType::Eat) {
					break;
				}

				// Priority 2: Check if we're at a harvestable food source
				// Look for harvestable entities at target position
				for (const auto& [key, entity] : memory.knownWorldEntities) {
					// Check if entity is at target position (with tolerance)
					glm::vec2 diff = entity.position - task.targetPosition;
					float	  distSq = diff.x * diff.x + diff.y * diff.y;
					if (distSq > kPositionTolerance * kPositionTolerance) {
						continue;
					}

					// Check if entity has harvestable capability via registry
					if (!registry.hasCapability(entity.defNameId, engine::assets::CapabilityType::Harvestable)) {
						continue;
					}

					// Found harvestable entity at target - get harvest details
					const auto& defName = registry.getDefName(entity.defNameId);
					const auto* def = registry.getDefinition(defName);
					if (def != nullptr && def->capabilities.harvestable.has_value()) {
						const auto& harvestCap = def->capabilities.harvestable.value();

						// Only consider harvestables that yield edible items
						if (!engine::assets::isItemEdible(harvestCap.yieldDefName)) {
							continue;
						}

						// TODO: Check if entity is on cooldown before creating harvest action.
						// Proper fix: VisionSystem should filter entities on cooldown so they
						// don't appear as harvestable in Memory. Until then, colonists may
						// arrive at recently-harvested bushes expecting to harvest again.

						// Calculate random yield within range using proper RNG
						uint32_t yield = harvestCap.amountMin;
						if (harvestCap.amountMax > harvestCap.amountMin) {
							std::uniform_int_distribution<uint32_t> yieldDist(harvestCap.amountMin, harvestCap.amountMax);
							yield = yieldDist(m_rng);
						}

						action = Action::Harvest(
							harvestCap.yieldDefName,
							yield,
							harvestCap.duration,
							entity.position,
							def->defName,
							harvestCap.destructive,
							harvestCap.regrowthTime
						);
						LOG_DEBUG(
							Engine,
							"[Action] Creating Harvest action for %s → %u x %s",
							def->defName.c_str(),
							yield,
							harvestCap.yieldDefName.c_str()
						);
						break;
					}
				}

				// If we created a harvest action, we're done
				if (action.type == ActionType::Harvest) {
					break;
				}

				// No food in inventory and no harvestable at target
				// This indicates a desync between AI decision and world state.
				// Possible causes: entity was harvested by another colonist, or memory is stale.
				LOG_WARNING(
					Engine,
					"[Action] Hunger action failed at (%.1f, %.1f) - no food in inventory and no "
					"harvestable food source at target. Colonist will re-evaluate next tick.",
					task.targetPosition.x,
					task.targetPosition.y
				);
				action.clear();
				break;
			}

			case NeedType::Thirst: {
				// Water tiles are inexhaustible - drinking fully restores thirst
				action = Action::Drink();
				break;
			}

			case NeedType::Energy: {
				// Check if at current position (ground fallback) or at a bed
				bool  isGroundFallback = (task.targetPosition == position.value);
				float quality = isGroundFallback ? kGroundSleepQuality : 1.0F;
				action = Action::Sleep(quality);
				break;
			}

			case NeedType::Bladder:
			case NeedType::Digestion: {
				// Smart Toilet: check both bladder and digestion needs
				// If both need attention, handle both efficiently
				bool needsPee = needs.bladder().needsAttention();
				bool needsPoop = needs.digestion().needsAttention();

				// Honor the committed task even if need value fluctuated above threshold.
				// The colonist traveled to this location for a reason; complete the action.
				// Also opportunistically handle the other need if it also needs attention.
				if (task.needToFulfill == NeedType::Bladder) {
					needsPee = true; // Always do what we came for
				}
				if (task.needToFulfill == NeedType::Digestion) {
					needsPoop = true; // Always do what we came for
				}

				action = Action::Toilet(position.value, needsPee, needsPoop);
				break;
			}

			case NeedType::Count:
				// Invalid need type - shouldn't happen
				LOG_ERROR(Engine, "[Action] Invalid need type in task: %d", static_cast<int>(task.needToFulfill));
				break;
		}
	}

	void ActionSystem::processAction(float deltaTime, Action& action, NeedsComponent& needs, Task& task) {
		(void)needs; // Not used during in-progress phase
		(void)task;	 // Not used during in-progress phase

		// Update elapsed time
		action.elapsed += deltaTime;

		// Transition from Starting to InProgress
		if (action.state == ActionState::Starting) {
			action.state = ActionState::InProgress;
		}

		// Check for completion
		if (action.elapsed >= action.duration) {
			action.state = ActionState::Complete;
		}
	}

	void ActionSystem::completeAction(EntityID entity, Action& action, NeedsComponent& needs, Task& task, Inventory& inventory, Memory& memory) {
		// Apply effects based on variant type
		if (action.hasNeedEffect()) {
			const auto& needEff = action.needEffect();

			// Apply primary need restoration
			if (needEff.need < NeedType::Count) {
				needs.get(needEff.need).restore(needEff.restoreAmount);
			}

			// Apply side effect (if any)
			if (needEff.sideEffectNeed < NeedType::Count) {
				// Side effect amount: positive = restore, negative = drain
				if (needEff.sideEffectAmount > 0.0F) {
					needs.get(needEff.sideEffectNeed).restore(needEff.sideEffectAmount);
				} else {
					// Negative amount means drain (e.g., drinking fills bladder)
					auto& need = needs.get(needEff.sideEffectNeed);
					need.value += needEff.sideEffectAmount; // sideEffectAmount is already negative
					if (need.value < 0.0F) {
						need.value = 0.0F;
					}
				}
			}
		}

		// Handle collection effects (Pickup, Harvest)
		if (action.hasCollectionEffect()) {
			const auto& collEff = action.collectionEffect();

			auto& harvestRegistry = engine::assets::AssetRegistry::Get();

			// Cap this withdrawal by what the colonist can actually take this action: the carry
			// weight (tools are equipment and don't count) AND the backpack's stack/slot headroom.
			// Clamping by both means the pool below is only debited by what truly lands in the
			// inventory, so a tree's wood is conserved. At the limit `wanted` is 0: take nothing
			// and leave the source for the next trip rather than dropping items on the floor.
			const uint32_t massFit = ecs::cargoUnitsThatFit(inventory, harvestRegistry, collEff.itemDefName);
			const uint32_t slotFit = inventory.addableCount(collEff.itemDefName);
			const uint32_t wanted = std::min({collEff.quantity, massFit, slotFit});

			// Does the source hold a finite resource pool (trees) or is it single-shot
			// (ground pickup, regrowth bush)?
			const auto* sourceDef = harvestRegistry.getDefinition(collEff.sourceDefName);
			bool hasResourcePool = false;
			if (sourceDef != nullptr && sourceDef->capabilities.harvestable.has_value()) {
				const auto& harv = sourceDef->capabilities.harvestable.value();
				hasResourcePool = harv.totalResourceMin > 0 && harv.totalResourceMax > 0;
			}

			uint32_t added = 0;

			if (hasResourcePool && m_onDecrementResource) {
				if (wanted > 0) {
					// Withdraw only what the colonist can carry; the pool clamps to its own
					// contents and reports depletion. Items added == removed (we sized `wanted`
					// to fit by both weight and slots), so the tree's wood is conserved.
					const ResourceDraw draw =
						m_onDecrementResource(collEff.sourceDefName, collEff.sourcePosition.x, collEff.sourcePosition.y, wanted);
					added = inventory.addItem(collEff.itemDefName, draw.removed);
					LOG_INFO(
						Engine,
						"[Action] Chopped %u x %s (now carrying %u)",
						added,
						collEff.itemDefName.c_str(),
						inventory.getQuantity(collEff.itemDefName)
					);
					if (draw.depleted) {
						if (m_onRemoveEntity) {
							m_onRemoveEntity(collEff.sourceDefName, collEff.sourcePosition.x, collEff.sourcePosition.y);
						}
						uint32_t defNameId = harvestRegistry.getDefNameId(collEff.sourceDefName);
						memory.forgetWorldEntity({collEff.sourcePosition.x, collEff.sourcePosition.y}, defNameId);
						LOG_DEBUG(
							Engine,
							"[Action] Resource depleted - removed %s at (%.1f, %.1f)",
							collEff.sourceDefName.c_str(),
							collEff.sourcePosition.x,
							collEff.sourcePosition.y
						);
					}
				} else {
					LOG_DEBUG(
						Engine,
						"[Action] Colonist at carry limit - leaving %s at (%.1f, %.1f) for next trip",
						collEff.sourceDefName.c_str(),
						collEff.sourcePosition.x,
						collEff.sourcePosition.y
					);
				}
			} else {
				// Single-shot source: take what fits, then remove it (destructive) or start its
				// regrowth cooldown. Only act on the source if the colonist actually collected.
				added = inventory.addItem(collEff.itemDefName, wanted);
				if (added < collEff.quantity) {
					// Carry/slot-limited below the full yield -- expected, not an error.
					LOG_INFO(
						Engine, "[Action] Collected %u of %u x %s (carry-limited)", added, collEff.quantity, collEff.itemDefName.c_str()
					);
				} else {
					LOG_INFO(Engine, "[Action] Collected %u x %s", added, collEff.itemDefName.c_str());
				}

				if (added > 0 && collEff.destroySource) {
					if (m_onRemoveEntity) {
						m_onRemoveEntity(collEff.sourceDefName, collEff.sourcePosition.x, collEff.sourcePosition.y);
					}
					uint32_t defNameId = harvestRegistry.getDefNameId(collEff.sourceDefName);
					memory.forgetWorldEntity({collEff.sourcePosition.x, collEff.sourcePosition.y}, defNameId);
					LOG_DEBUG(
						Engine,
						"[Action] Removed source entity %s at (%.1f, %.1f)",
						collEff.sourceDefName.c_str(),
						collEff.sourcePosition.x,
						collEff.sourcePosition.y
					);
				} else if (added > 0 && collEff.regrowthTime > 0.0F) {
					if (m_onSetCooldown) {
						m_onSetCooldown(collEff.sourceDefName, collEff.sourcePosition.x, collEff.sourcePosition.y, collEff.regrowthTime);
						LOG_DEBUG(
							Engine,
							"[Action] Set cooldown on %s at (%.1f, %.1f) for %.1fs",
							collEff.sourceDefName.c_str(),
							collEff.sourcePosition.x,
							collEff.sourcePosition.y,
							collEff.regrowthTime
						);
					} else {
						LOG_WARNING(
							Engine,
							"[Action] No cooldown callback set - source entity %s at (%.1f, %.1f) cooldown NOT applied",
							collEff.sourceDefName.c_str(),
							collEff.sourcePosition.x,
							collEff.sourcePosition.y
						);
					}
				}
			}

			// For goal-driven Harvest tasks, update the GoalTaskRegistry.
			// Record only what actually landed in inventory (overflow is lost, above), so the
			// harvest goal's progress reflects real items the colonist now carries.
			if (task.type == TaskType::Harvest && task.harvestGoalId != 0 && added > 0) {
				auto& goalRegistry = GoalTaskRegistry::Get();

				goalRegistry.recordDelivery(task.harvestGoalId, added);

				// Check if goal is now complete
				const auto* goal = goalRegistry.getGoal(task.harvestGoalId);
				if (goal != nullptr && goal->availableCapacity() == 0) {
					// Harvest goal complete - notify dependent goals (the Haul that carries
					// these items to the station unblocks) then remove the harvest goal.
					goalRegistry.notifyGoalCompleted(task.harvestGoalId);
					goalRegistry.removeGoal(task.harvestGoalId);
				}
			}
		}

		// Handle consumption effects (Eat action)
		if (action.hasConsumptionEffect()) {
			const auto& consumeEff = action.consumptionEffect();

			// Remove item from inventory
			uint32_t removed = inventory.removeItem(consumeEff.itemDefName, consumeEff.quantity);
			if (removed > 0) {
				// Restore the need
				if (consumeEff.need < NeedType::Count) {
					needs.get(consumeEff.need).restore(consumeEff.restoreAmount);
				}

				// Apply side effect (e.g., eating fills digestion)
				if (consumeEff.sideEffectNeed < NeedType::Count) {
					if (consumeEff.sideEffectAmount > 0.0F) {
						needs.get(consumeEff.sideEffectNeed).restore(consumeEff.sideEffectAmount);
					} else {
						// Negative amount means drain (e.g., eating fills gut)
						auto& need = needs.get(consumeEff.sideEffectNeed);
						need.value += consumeEff.sideEffectAmount; // sideEffectAmount is already negative
						if (need.value < 0.0F) {
							need.value = 0.0F;
						}
					}
				}

				LOG_INFO(
					Engine,
					"[Action] Consumed %u x %s from inventory, restored %.1f%% %s",
					removed,
					consumeEff.itemDefName.c_str(),
					consumeEff.restoreAmount,
					consumeEff.need == NeedType::Hunger ? "hunger" : "need"
				);
			} else {
				LOG_WARNING(Engine, "[Action] Failed to consume %s from inventory (not found)", consumeEff.itemDefName.c_str());
			}
		}

		// Handle spawn effects (pooping creates Bio Pile, peeing does not)
		if (action.spawnBioPile) {
			// Spawn Bio Pile entity at action.targetPosition
			auto bioPile = world->createEntity();
			world->addComponent<Position>(bioPile, Position{action.targetPosition});
			world->addComponent<Rotation>(bioPile, Rotation{0.0F});
			world->addComponent<Appearance>(bioPile, Appearance{"Misc_BioPile", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
			LOG_INFO(Engine, "[Action] Spawned Bio Pile at (%.1f, %.1f)", action.targetPosition.x, action.targetPosition.y);
		}

		// Handle crafting effects
		if (action.hasCraftingEffect()) {
			const auto& craftEff = action.craftingEffect();

			// Consume inputs from inventory
			for (const auto& [itemName, count] : craftEff.inputs) {
				uint32_t removed = inventory.removeItem(itemName, count);
				if (removed < count) {
					LOG_WARNING(Engine, "[Action] Craft failed to consume %u x %s (only had %u)", count, itemName.c_str(), removed);
				}
			}

			// Add outputs to inventory (or drop on ground if non-backpackable)
			auto& assetRegistry = engine::assets::AssetRegistry::Get();
			for (const auto& [itemName, count] : craftEff.outputs) {
				const auto* itemDef = assetRegistry.getDefinition(itemName);
				bool canBackpack = (itemDef == nullptr) || (itemDef->handsRequired < kTwoHandedThreshold);

				if (canBackpack) {
					uint32_t added = inventory.addItem(itemName, count);
					LOG_INFO(Engine, "[Action] Crafted %u x %s (added to inventory)", added, itemName.c_str());
				} else {
					// Non-backpackable item - drop on ground at crafting station
					if (m_onDropItem) {
						for (uint32_t i = 0; i < count; ++i) {
							m_onDropItem(itemName, action.targetPosition.x, action.targetPosition.y);
						}
						LOG_INFO(Engine, "[Action] Crafted %u x %s (dropped on ground)", count, itemName.c_str());
					} else {
						LOG_WARNING(Engine, "[Action] Crafted non-backpackable item %s but no drop callback set", itemName.c_str());
					}
				}
			}

			// Fire notification callback for crafted item
			if (m_onItemCrafted) {
				auto&		recipeRegistry = engine::assets::RecipeRegistry::Get();
				const auto* recipe = recipeRegistry.getRecipe(craftEff.recipeDefName);
				if (recipe != nullptr) {
					m_onItemCrafted(recipe->label);
				} else {
					LOG_WARNING(
						Engine,
						"[Action] Crafted item notification skipped: recipe '%s' not found in registry",
						craftEff.recipeDefName.c_str()
					);
				}
			}

			// Update WorkQueue on the station
			auto* workQueue = world->getComponent<WorkQueue>(static_cast<EntityID>(craftEff.stationEntityId));
			if (workQueue != nullptr) {
				auto* job = workQueue->getNextJob();
				if (job != nullptr && job->recipeDefName == craftEff.recipeDefName) {
					job->completed++;
					LOG_INFO(
						Engine, "[Action] Updated WorkQueue: %s %u/%u complete", job->recipeDefName.c_str(), job->completed, job->quantity
					);
					// Clean up completed jobs
					workQueue->cleanupCompleted();
				}
				// Reset progress for next item (or 0 if queue empty)
				workQueue->progress = 0.0F;
			}
		}

		// Handle deposit effects (put items into storage)
		if (action.hasDepositEffect()) {
			const auto& depEff = action.depositEffect();

			// Craft-material delivery: the target is a crafting station with no storage
			// container. The harvested materials stay in the colonist's inventory (the Craft
			// action consumes them from there); arriving here just marks them delivered so the
			// parent Craft goal advances out of Blocked.
			if (depEff.deliverToCraftStation) {
				uint32_t carried = inventory.getQuantity(depEff.itemDefName);
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

			// Remove item from colonist inventory
			uint32_t removed = inventory.removeItem(depEff.itemDefName, depEff.quantity);
			if (removed > 0) {
				// Add to storage container's inventory
				auto* storageInv = world->getComponent<Inventory>(static_cast<EntityID>(depEff.storageEntityId));
				if (storageInv != nullptr) {
					uint32_t added = storageInv->addItem(depEff.itemDefName, removed);
					if (added < removed) {
						// Storage full - put remaining back in colonist inventory
						uint32_t leftover = removed - added;
						inventory.addItem(depEff.itemDefName, leftover);
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

					// Record delivery only for items that actually landed in storage. If the
					// store was full (partial add) or destroyed (no storageInv, items bounced
					// back), nothing is credited - the goal stays open.
					if (added > 0 && task.type == TaskType::Haul && task.haulGoalId != 0) {
						auto& goalRegistry = GoalTaskRegistry::Get();
						goalRegistry.recordDelivery(task.haulGoalId, added);

						const auto* goal = goalRegistry.getGoal(task.haulGoalId);
						if (goal != nullptr && goal->availableCapacity() == 0) {
							// Haul goal complete - remove it
							goalRegistry.removeGoal(task.haulGoalId);
						}
					}
				} else {
					// Storage entity not found - put items back, credit nothing
					inventory.addItem(depEff.itemDefName, removed);
					LOG_WARNING(
						Engine,
						"[Action] Storage entity %llu not found, items returned to inventory",
						static_cast<unsigned long long>(depEff.storageEntityId)
					);
				}
			} else {
				LOG_WARNING(Engine, "[Action] Deposit failed: %s not in inventory", depEff.itemDefName.c_str());
			}
			} // end storage-deposit branch
		}

		// Handle construction progress effects (Build, Deconstruct).
		// advanceConstructionWork already moved workDone to the bound and set the phase; here we
		// signal the layer above ActionSystem (which doesn't know ConstructionWorld) via callback.
		if (action.hasProgressEffect()) {
			const auto& progressEff = action.progressEffect();
			auto		blueprintEntity = static_cast<EntityID>(progressEff.targetEntityId);

			if (progressEff.deconstruct) {
				LOG_INFO(
					Engine,
					"[Action] Deconstruct complete on blueprint %llu - signaling removal/refund",
					static_cast<unsigned long long>(progressEff.targetEntityId)
				);
				if (m_onStructureDeconstructed) {
					m_onStructureDeconstructed(blueprintEntity);
				} else {
					LOG_WARNING(Engine, "[Action] No structure-deconstructed callback set - removal/refund skipped");
				}
			} else {
				LOG_INFO(
					Engine,
					"[Action] Build complete on blueprint %llu - signaling structure built",
					static_cast<unsigned long long>(progressEff.targetEntityId)
				);
				if (m_onStructureCompleted) {
					m_onStructureCompleted(blueprintEntity);
				} else {
					LOG_WARNING(Engine, "[Action] No structure-completed callback set - structure state not flipped");
				}
			}
		}

		// Special handling for Haul tasks - may need to continue to deposit phase
		// NOTE: This intentionally returns early WITHOUT clearing the task. Haul is a
		// two-phase task (Pickup→Deposit), so after phase 1 we set up phase 2 and return.
		// The action is cleared but the task persists. This differs from single-phase
		// tasks that clear both action and task at the end of this function.
		if (task.type == TaskType::Haul && action.type == ActionType::Pickup) {
			// Phase 1 complete (Pickup) - move to phase 2 (Deposit)
			task.targetPosition = task.haulTargetPosition;
			task.state = TaskState::Pending;
			task.chainStep++; // Advance chain: step 0 (Pickup) → step 1 (Deposit)
			action.clear();
			LOG_DEBUG(
				Engine,
				"[Action] Haul phase 1 complete (chain step %u), moving to storage at (%.1f, %.1f)",
				task.chainStep,
				task.haulTargetPosition.x,
				task.haulTargetPosition.y
			);
			return;
		}

		// Special handling for PlacePackaged tasks - two-phase (PickupPackaged→PlacePackaged)
		if (task.type == TaskType::PlacePackaged && action.type == ActionType::PickupPackaged) {
			// Phase 1 complete (PickupPackaged) - clear hands, pick up entity, then move to phase 2
			// Hand clearing: stow items to backpack, drop if can't fit
			clearHandsForTwoHandedPickup(inventory, action.targetPosition);

			// Get the packaged entity from the effect
			if (action.hasPlacePackagedEffect()) {
				const auto& placeEff = action.placePackagedEffect();
				auto		packagedEntity = static_cast<EntityID>(placeEff.packagedEntityId);

				// Mark the entity as being carried (hides from world rendering)
				auto* packaged = world->getComponent<Packaged>(packagedEntity);
				if (packaged != nullptr) {
					packaged->beingCarried = true;
				}

				// Track the carried entity in inventory
				inventory.carryingPackagedEntity = placeEff.packagedEntityId;

				// Put the entity's defName in both hands (for UI display)
				auto* appearance = world->getComponent<Appearance>(packagedEntity);
				if (appearance != nullptr && !appearance->defName.empty()) {
					inventory.leftHand = ItemStack{appearance->defName, 1};
					inventory.rightHand = ItemStack{appearance->defName, 1};
					LOG_INFO(
						Engine,
						"[Action] Picked up %s (entity %llu) - now carrying in both hands",
						appearance->defName.c_str(),
						static_cast<unsigned long long>(placeEff.packagedEntityId)
					);
				} else {
					LOG_WARNING(
						Engine,
						"[Action] Could not set hands: appearance=%p, defName=%s",
						static_cast<void*>(appearance),
						appearance ? appearance->defName.c_str() : "(null)"
					);
				}
			}

			// Transition to phase 2 - walk to placement target
			task.targetPosition = task.placeTargetPosition;
			task.state = TaskState::Moving;  // Must be Moving for MovementSystem to set Arrived
			task.chainStep++; // Advance chain: step 0 (PickupPackaged) → step 1 (Place)

			// Update MovementTarget to the new destination
			// This is critical - without this, the movement system won't know where to go
			auto* movementTarget = world->getComponent<MovementTarget>(entity);
			if (movementTarget != nullptr) {
				movementTarget->target = task.placeTargetPosition;
				movementTarget->active = true;
			}

			action.clear();
			LOG_DEBUG(
				Engine,
				"[Action] PlacePackaged phase 1 complete (chain step %u), moving to target at (%.1f, %.1f)",
				task.chainStep,
				task.placeTargetPosition.x,
				task.placeTargetPosition.y
			);
			return;
		}

		// Handle PlacePackaged completion (phase 2) - move entity to target and remove Packaged component
		if (action.type == ActionType::PlacePackaged && action.hasPlacePackagedEffect()) {
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

		// Clear the action and task
		action.clear();
		task.clear();
		task.timeSinceEvaluation = 0.0F;
	}

	void ActionSystem::startCraftAction(Task& task, Action& action, const Inventory& inventory) {
		auto& recipeRegistry = engine::assets::RecipeRegistry::Get();

		// Get the recipe
		const auto* recipe = recipeRegistry.getRecipe(task.craftRecipeDefName);
		if (recipe == nullptr) {
			LOG_ERROR(Engine, "[Action] Unknown recipe: %s", task.craftRecipeDefName.c_str());
			action.clear();
			return;
		}

		// Verify colonist has all required inputs
		for (const auto& input : recipe->inputs) {
			if (!inventory.hasQuantity(input.defName, input.count)) {
				LOG_WARNING(
					Engine, "[Action] Cannot craft %s - missing %u x %s", recipe->label.c_str(), input.count, input.defName.c_str()
				);
				action.clear();
				return;
			}
		}

		// Build inputs and outputs vectors for the action
		std::vector<std::pair<std::string, uint32_t>> inputs;
		for (const auto& input : recipe->inputs) {
			inputs.emplace_back(input.defName, input.count);
		}

		std::vector<std::pair<std::string, uint32_t>> outputs;
		for (const auto& output : recipe->outputs) {
			outputs.emplace_back(output.defName, output.count);
		}

		// Create the craft action
		action = Action::Craft(task.craftRecipeDefName, task.targetStationId, task.targetPosition, recipe->workAmount, inputs, outputs);

		LOG_DEBUG(Engine, "[Action] Starting Craft action for %s (%.1fs duration)", recipe->label.c_str(), action.duration);
	}

	void ActionSystem::startBuildAction(EntityID entity, Task& task, Action& action) {
		auto blueprintEntity = static_cast<EntityID>(task.buildBlueprintEntityId);
		auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
		if (blueprint == nullptr) {
			LOG_WARNING(
				Engine,
				"[Action] %s task targets entity %llu with no StructureBlueprint - clearing",
				task.type == TaskType::Deconstruct ? "Deconstruct" : "Build",
				static_cast<unsigned long long>(task.buildBlueprintEntityId)
			);
			action.clear();
			return;
		}

		const bool deconstruct = (task.type == TaskType::Deconstruct);

		// Build requires the blueprint to be under construction (materials staged); Deconstruct
		// requires there to be work to undo. A no-op start clears the action so the colonist
		// re-evaluates rather than spinning on a finished or not-yet-ready blueprint.
		if (deconstruct) {
			if (blueprint->workDone <= 0.0F) {
				LOG_DEBUG(
					Engine,
					"[Action] Deconstruct skipped: blueprint %llu has no work to undo",
					static_cast<unsigned long long>(task.buildBlueprintEntityId)
				);
				action.clear();
				return;
			}
		} else {
			if (blueprint->phase != StructureBlueprint::BuildPhase::UnderConstruction) {
				LOG_DEBUG(
					Engine,
					"[Action] Build skipped: blueprint %llu not UnderConstruction (phase %d)",
					static_cast<unsigned long long>(task.buildBlueprintEntityId),
					static_cast<int>(blueprint->phase)
				);
				action.clear();
				return;
			}
		}

		// Capture the builder's Construction skill to scale the work rate. Skills is optional;
		// a colonist without it (or untrained) still builds at the base rate.
		float skillLevel = 0.0F;
		if (const auto* skills = world->getComponent<Skills>(entity)) {
			skillLevel = skills->getLevel("Construction");
		}

		action = deconstruct ? Action::Deconstruct(task.buildBlueprintEntityId, task.targetPosition, skillLevel)
							  : Action::Build(task.buildBlueprintEntityId, task.targetPosition, skillLevel);

		LOG_DEBUG(
			Engine,
			"[Action] Starting %s action on blueprint %llu (skill %.1f, rate %.1f/s, workDone %.1f/%.1f)",
			deconstruct ? "Deconstruct" : "Build",
			static_cast<unsigned long long>(task.buildBlueprintEntityId),
			skillLevel,
			constructionWorkRate(skillLevel),
			blueprint->workDone,
			blueprint->workTotal
		);
	}

	bool ActionSystem::advanceConstructionWork(float deltaTime, Action& action) {
		// Transition Starting -> InProgress on the first tick, mirroring processAction.
		if (action.state == ActionState::Starting) {
			action.state = ActionState::InProgress;
		}

		const auto& progressEff = action.progressEffect();
		auto		blueprintEntity = static_cast<EntityID>(progressEff.targetEntityId);
		auto* blueprint = world->getComponent<StructureBlueprint>(blueprintEntity);
		if (blueprint == nullptr) {
			// Blueprint vanished mid-work (e.g. cancelled). Abandon the action; the task will
			// re-evaluate next tick. Don't fire a completion callback for a target that's gone.
			LOG_WARNING(
				Engine,
				"[Action] Construction target %llu vanished mid-work - abandoning action",
				static_cast<unsigned long long>(progressEff.targetEntityId)
			);
			action.clear();
			return false;
		}

		// Redundant-builder guard: completion is gated on the actual phase/work-bound transition,
		// not just on the work bound being reached. With multiple concurrent builders, a second
		// builder still arriving after the structure already flipped Complete (or a deconstruct
		// already at 0) would otherwise re-set its action to Complete every tick and re-fire the
		// completion callback (duplicate toast + redundant world version bump). Treat such a
		// builder as redundant: clear its action and return without firing.
		if (progressEff.deconstruct) {
			if (blueprint->workDone <= 0.0F) {
				action.clear();
				return false;
			}
		} else {
			if (blueprint->phase == StructureBlueprint::BuildPhase::Complete) {
				action.clear();
				return false;
			}
		}

		const float delta = constructionWorkRate(progressEff.skillLevel) * deltaTime;

		if (progressEff.deconstruct) {
			blueprint->workDone -= delta;
			if (blueprint->workDone <= 0.0F) {
				blueprint->workDone = 0.0F;
				action.state = ActionState::Complete;
				return true;
			}
		} else {
			blueprint->workDone += delta;
			if (blueprint->workDone >= blueprint->workTotal) {
				blueprint->workDone = blueprint->workTotal;
				blueprint->phase = StructureBlueprint::BuildPhase::Complete;
				action.state = ActionState::Complete;
				return true;
			}
		}

		return false;
	}

	void ActionSystem::startGatherAction(Task& task, Action& action, const Position& position, const Memory& memory) {
		auto& registry = engine::assets::AssetRegistry::Get();

		// Find the entity at the target position that we want to gather from
		constexpr float kPositionTolerance = 0.5F;

		for (const auto& [key, entity] : memory.knownWorldEntities) {
			// Check if entity is at target position
			glm::vec2 diff = entity.position - task.targetPosition;
			float	  distSq = diff.x * diff.x + diff.y * diff.y;
			if (distSq > kPositionTolerance * kPositionTolerance) {
				continue;
			}

			// Check if entity has Carryable capability (direct pickup)
			if (registry.hasCapability(entity.defNameId, engine::assets::CapabilityType::Carryable)) {
				const auto& defName = registry.getDefName(entity.defNameId);
				const auto* def = registry.getDefinition(defName);
				if (def != nullptr && def->capabilities.carryable.has_value()) {
					const auto& carryableCap = def->capabilities.carryable.value();
					action = Action::Pickup(defName, carryableCap.quantity, entity.position, defName);
					LOG_DEBUG(Engine, "[Action] Starting Pickup action for %s (qty %u)", defName.c_str(), carryableCap.quantity);
					return;
				}
			}

			// Check if entity has Harvestable capability
			if (registry.hasCapability(entity.defNameId, engine::assets::CapabilityType::Harvestable)) {
				const auto& defName = registry.getDefName(entity.defNameId);
				const auto* def = registry.getDefinition(defName);
				if (def != nullptr && def->capabilities.harvestable.has_value()) {
					const auto& harvestCap = def->capabilities.harvestable.value();

					// Calculate yield amount (skip RNG if range is single value)
					uint32_t yieldAmount = harvestCap.amountMin;
					if (harvestCap.amountMax > harvestCap.amountMin) {
						std::uniform_int_distribution<uint32_t> yieldDist(harvestCap.amountMin, harvestCap.amountMax);
						yieldAmount = yieldDist(m_rng);
					}

					action = Action::Harvest(
						harvestCap.yieldDefName,
						yieldAmount,
						harvestCap.duration,
						entity.position,
						defName,
						harvestCap.destructive,
						harvestCap.regrowthTime
					);
					LOG_DEBUG(
						Engine,
						"[Action] Starting Harvest action for %s from %s (duration %.1fs)",
						harvestCap.yieldDefName.c_str(),
						defName.c_str(),
						harvestCap.duration
					);
					return;
				}
			}
		}

		// No valid entity found at target - clear action
		LOG_WARNING(
			Engine,
			"[Action] No gatherable entity found at (%.1f, %.1f) for item %s",
			task.targetPosition.x,
			task.targetPosition.y,
			task.gatherItemDefName.c_str()
		);
		action.clear();
	}

	void ActionSystem::startHarvestAction(Task& task, Action& action, const Position& position, Memory& memory, const Inventory& inventory) {
		auto& registry = engine::assets::AssetRegistry::Get();

		// Goal-driven harvest: Find a harvestable entity at the target position that yields
		// the item type specified by the Harvest goal (harvestYieldDefNameId)
		constexpr float kPositionTolerance = 0.5F;

		// Track stale memory entries sitting at the target so we can forget them if no valid
		// harvestable is actually there (phantom target). Otherwise AIDecision keeps
		// re-selecting the dead entity and floods warnings until LRU eviction.
		std::vector<std::pair<glm::vec2, uint32_t>> staleAtTarget;

		for (const auto& [key, entity] : memory.knownWorldEntities) {
			// Check if entity is at target position
			glm::vec2 diff = entity.position - task.targetPosition;
			float	  distSq = diff.x * diff.x + diff.y * diff.y;
			if (distSq > kPositionTolerance * kPositionTolerance) {
				continue;
			}

			staleAtTarget.emplace_back(entity.position, entity.defNameId);

			// Check if entity has Harvestable capability
			if (!registry.hasCapability(entity.defNameId, engine::assets::CapabilityType::Harvestable)) {
				continue;
			}

			const auto& defName = registry.getDefName(entity.defNameId);
			const auto* def = registry.getDefinition(defName);
			if (def == nullptr || !def->capabilities.harvestable.has_value()) {
				continue;
			}

			const auto& harvestCap = def->capabilities.harvestable.value();

			// Verify this harvestable yields the item we need
			uint32_t yieldDefNameId = registry.getDefNameId(harvestCap.yieldDefName);
			if (yieldDefNameId != task.harvestYieldDefNameId) {
				continue; // Wrong yield type
			}

			// Chopping a tree needs the right tool. The decision layer already filters these
			// out for tool-less colonists; this guards against a stale task producing a chop
			// the colonist can't actually perform.
			if (!ecs::inventoryHoldsToolType(inventory, registry, harvestCap.requiredToolType)) {
				LOG_DEBUG(
					Engine, "[Action] Cannot harvest %s without a %s tool", defName.c_str(), harvestCap.requiredToolType.c_str()
				);
				continue;
			}

			// Calculate yield amount
			uint32_t yieldAmount = harvestCap.amountMin;
			if (harvestCap.amountMax > harvestCap.amountMin) {
				std::uniform_int_distribution<uint32_t> yieldDist(harvestCap.amountMin, harvestCap.amountMax);
				yieldAmount = yieldDist(m_rng);
			}

			action = Action::Harvest(
				harvestCap.yieldDefName,
				yieldAmount,
				harvestCap.duration,
				entity.position,
				defName,
				harvestCap.destructive,
				harvestCap.regrowthTime
			);
			LOG_DEBUG(
				Engine,
				"[Action] Starting goal-driven Harvest action for %s from %s (duration %.1fs, goal %llu)",
				harvestCap.yieldDefName.c_str(),
				defName.c_str(),
				harvestCap.duration,
				static_cast<unsigned long long>(task.harvestGoalId)
			);
			return;
		}

		// No valid harvestable found at target - the memory entry (if any) is stale. Forget
		// it so AIDecision stops re-selecting the phantom target on the next tick.
		for (const auto& [pos, defNameId] : staleAtTarget) {
			memory.forgetWorldEntity(pos, defNameId);
		}

		LOG_WARNING(
			Engine,
			"[Action] No harvestable entity found at (%.1f, %.1f) for yield %u (goal %llu) - forgot %zu stale entr%s",
			task.targetPosition.x,
			task.targetPosition.y,
			task.harvestYieldDefNameId,
			static_cast<unsigned long long>(task.harvestGoalId),
			staleAtTarget.size(),
			staleAtTarget.size() == 1 ? "y" : "ies"
		);
		action.clear();
	}

	void ActionSystem::startHaulAction(Task& task, Action& action, const Position& position, Memory& memory) {
		auto& registry = engine::assets::AssetRegistry::Get();

		constexpr float kPositionTolerance = 0.5F;

		// Inventory-source haul: the colonist already carries the harvested items, so there is
		// no ground pickup. It walks to the destination and deposits. Single-phase. The deposit
		// mode depends on the destination: a build site (blueprint) has a delivery Inventory the
		// Wood lands in for real; a crafting station has none, so "delivery" just credits the
		// goal and the items stay in inventory for the Craft action to consume.
		if (task.haulFromInventory) {
			const bool targetHasInventory =
				world->hasComponent<Inventory>(static_cast<EntityID>(task.haulTargetStorageId));
			action = Action::Deposit(
				task.haulItemDefName,
				task.haulQuantity,
				task.haulTargetStorageId,
				task.haulTargetPosition,
				/*deliverToCraftStation=*/!targetHasInventory
			);
			LOG_DEBUG(
				Engine,
				"[Action] Haul-from-inventory: deliver %u x %s to %s %llu",
				task.haulQuantity,
				task.haulItemDefName.c_str(),
				targetHasInventory ? "build site" : "crafting station",
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

		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		const auto& itemName = handSlot->defName;
		uint32_t	quantity = handSlot->quantity;

		// Check if item can go in backpack (1-handed items only)
		const auto* itemDef = assetRegistry.getDefinition(itemName);
		bool		canBackpack = (itemDef == nullptr) || (itemDef->handsRequired < kTwoHandedThreshold);

		if (canBackpack) {
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
		clearHandItem(inventory.leftHand, inventory, dropPosition, "left");
		clearHandItem(inventory.rightHand, inventory, dropPosition, "right");
	}

} // namespace ecs
