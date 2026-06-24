#include "../ActionSystem.h"

#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Appearance.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Needs.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"

#include "assets/AssetRegistry.h"
#include "assets/ItemProperties.h"

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

	} // namespace

	void ActionSystem::startNeedAction(
		Task&				  task,
		Action&				  action,
		const Position&		  position,
		Memory&				  memory,
		const NeedsComponent& needs,
		const Inventory&	  inventory
	) {
		auto& registry = engine::assets::AssetRegistry::Get();

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
							harvestCap.durability,
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

	void ActionSystem::applyNeedEffect(const Action& action, NeedsComponent& needs) {
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

	void ActionSystem::applyConsumptionEffect(const Action& action, NeedsComponent& needs, Inventory& inventory) {
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

	void ActionSystem::applySpawnEffect(const Action& action) {
		// Spawn Bio Pile entity at action.targetPosition
		auto bioPile = world->createEntity();
		world->addComponent<Position>(bioPile, Position{action.targetPosition});
		world->addComponent<Rotation>(bioPile, Rotation{0.0F});
		world->addComponent<Appearance>(bioPile, Appearance{"Misc_BioPile", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		LOG_INFO(Engine, "[Action] Spawned Bio Pile at (%.1f, %.1f)", action.targetPosition.x, action.targetPosition.y);
	}

} // namespace ecs
