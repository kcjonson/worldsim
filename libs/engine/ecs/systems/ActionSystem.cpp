#include "ActionSystem.h"

#include "../World.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/MemoryQueries.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include "assets/AssetRegistry.h"
#include "assets/ItemProperties.h"
#include "assets/RecipeDef.h"
#include "assets/RecipeRegistry.h"

#include <utils/Log.h>

#include <random>

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

			// Only process actionable tasks (FulfillNeed, Gather, Craft, and Haul)
			if (task.type != TaskType::FulfillNeed && task.type != TaskType::Gather && task.type != TaskType::Craft &&
				task.type != TaskType::Haul) {
				// For non-actionable tasks like Wander, just clear when arrived
				task.clear();
				task.timeSinceEvaluation = 0.0F;
				continue;
			}

			// Start action if not already active
			if (!action.isActive()) {
				startAction(task, action, position, memory, needs, inventory);
				LOG_INFO(
					Engine,
					"[Action] Entity %llu: Started %s action (%.1fs duration)",
					static_cast<unsigned long long>(entity),
					actionTypeName(action.type),
					action.duration
				);
			}

			// Process the action
			processAction(deltaTime, action, needs, task);

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
				completeAction(action, needs, task, inventory);
			}
		}
	}

	void ActionSystem::startAction(
		Task&				  task,
		Action&				  action,
		const Position&		  position,
		const Memory&		  memory,
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

	void ActionSystem::completeAction(Action& action, NeedsComponent& needs, Task& task, Inventory& inventory) {
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

			// Add items to inventory
			uint32_t added = inventory.addItem(collEff.itemDefName, collEff.quantity);

			// Warn if not all items could be stored (inventory full or stack limit)
			if (added < collEff.quantity) {
				uint32_t lost = collEff.quantity - added;
				LOG_WARNING(
					Engine,
					"[Action] Inventory full: collected %u x %s but only %u added, %u lost",
					collEff.quantity,
					collEff.itemDefName.c_str(),
					added,
					lost
				);
			} else {
				LOG_INFO(
					Engine, "[Action] Collected %u x %s (added %u to inventory)", collEff.quantity, collEff.itemDefName.c_str(), added
				);
			}

			// Entity removal/cooldown handled in Phase 5
			// TODO: Call PlacementExecutor to remove or set cooldown on source entity
			if (collEff.destroySource) {
				LOG_DEBUG(
					Engine,
					"[Action] Source entity %s at (%.1f, %.1f) should be removed",
					collEff.sourceDefName.c_str(),
					collEff.sourcePosition.x,
					collEff.sourcePosition.y
				);
			} else if (collEff.regrowthTime > 0.0F) {
				LOG_DEBUG(
					Engine,
					"[Action] Source entity %s at (%.1f, %.1f) should enter %.1fs cooldown",
					collEff.sourceDefName.c_str(),
					collEff.sourcePosition.x,
					collEff.sourcePosition.y,
					collEff.regrowthTime
				);
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
				} else {
					// Storage entity not found - put items back
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
			action.clear();
			LOG_DEBUG(
				Engine,
				"[Action] Haul phase 1 complete, moving to storage at (%.1f, %.1f)",
				task.haulTargetPosition.x,
				task.haulTargetPosition.y
			);
			return;
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

	void ActionSystem::startHaulAction(Task& task, Action& action, const Position& position, const Memory& memory) {
		auto& registry = engine::assets::AssetRegistry::Get();

		// Haul is a two-phase task:
		// Phase 1: At source position → Pickup the item
		// Phase 2: At storage position → Deposit the item
		// We determine which phase by checking which position we're closer to

		constexpr float kPositionTolerance = 0.5F;

		glm::vec2 diffToSource = position.value - task.haulSourcePosition;
		float	  distSqToSource = diffToSource.x * diffToSource.x + diffToSource.y * diffToSource.y;
		bool	  atSource = distSqToSource <= kPositionTolerance * kPositionTolerance;

		glm::vec2 diffToTarget = position.value - task.haulTargetPosition;
		float	  distSqToTarget = diffToTarget.x * diffToTarget.x + diffToTarget.y * diffToTarget.y;
		bool	  atTarget = distSqToTarget <= kPositionTolerance * kPositionTolerance;

		if (atSource && !atTarget) {
			// Phase 1: At source - do Pickup
			// Look for a carryable entity at the source position matching the item we want to haul
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
			}

			LOG_WARNING(
				Engine,
				"[Action] Haul failed: item %s not found at (%.1f, %.1f)",
				task.haulItemDefName.c_str(),
				task.haulSourcePosition.x,
				task.haulSourcePosition.y
			);
			action.clear();
		} else if (atTarget) {
			// Phase 2: At storage target - do Deposit (use same quantity as pickup)
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

} // namespace ecs
