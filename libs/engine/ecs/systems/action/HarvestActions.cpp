#include "../ActionSystem.h"

#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../components/Action.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"

#include "assets/AssetRegistry.h"

#include <utils/Log.h>

#include <algorithm>
#include <random>
#include <utility>
#include <vector>

namespace ecs {

	void ActionSystem::applyCollectionEffect(
		const Action& action,
		Task&		  task,
		Inventory&	  inventory,
		Memory&		  memory
	) {
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
			// Two-hand bulk goods (e.g. felled wood) ride in the hands as a weight-limited
			// armful, never the backpack; everything else stacks into the pack.
			if (ecs::itemIsTwoHand(harvestRegistry, collEff.itemDefName)) {
				added = ecs::addArmful(inventory, harvestRegistry, collEff.itemDefName, collEff.quantity);
			} else {
				added = inventory.addItem(collEff.itemDefName, wanted);
			}
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
						harvestCap.durability,
						entity.position,
						defName,
						harvestCap.destructive,
						harvestCap.regrowthTime
					);
					LOG_DEBUG(
						Engine,
						"[Action] Starting Harvest action for %s from %s (durability %.0f)",
						harvestCap.yieldDefName.c_str(),
						defName.c_str(),
						harvestCap.durability
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
				harvestCap.durability,
				entity.position,
				defName,
				harvestCap.destructive,
				harvestCap.regrowthTime
			);
			LOG_DEBUG(
				Engine,
				"[Action] Starting goal-driven Harvest action for %s from %s (durability %.0f, goal %llu)",
				harvestCap.yieldDefName.c_str(),
				defName.c_str(),
				harvestCap.durability,
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

} // namespace ecs
