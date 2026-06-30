#include "../ActionSystem.h"

#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../World.h"
#include "../../components/Action.h"
#include "../../components/Appearance.h"
#include "../../components/Inventory.h"
#include "../../components/Memory.h"
#include "../../components/ResourceStack.h"
#include "../../components/Task.h"
#include "../../components/Transform.h"

#include "assets/AssetRegistry.h"

#include <utils/Log.h>

#include <algorithm>
#include <random>

namespace ecs {

	namespace {

		/// The loose pile found at a position: the matched entity plus the live ResourceStack
		/// to mutate. `stack` is nullptr (and `entity` kInvalidEntity) when no pile is there.
		struct LoosePile {
			EntityID	   entity = kInvalidEntity;
			ResourceStack* stack = nullptr;
		};

		/// Find a loose ground pile of `defName` at `pos`: a world entity whose Appearance
		/// defName matches and which carries a ResourceStack. Matches by position within the
		/// same epsilon GameScene's remove-callback uses (~0.25 m). Returns the matched entity
		/// id and its live ResourceStack, so a drained pile is removed by exact id rather than
		/// re-scanned by position (two same-material piles inside the epsilon would alias).
		[[nodiscard]] LoosePile findLoosePile(World* world, const std::string& defName, glm::vec2 pos) {
			if (world == nullptr) {
				return {};
			}
			constexpr float kMatchEps = 0.25F;
			for (auto [entity, entPos, appearance, stack] : world->view<Position, Appearance, ResourceStack>()) {
				if (appearance.defName != defName) {
					continue;
				}
				const float dx = entPos.value.x - pos.x;
				const float dy = entPos.value.y - pos.y;
				if (dx * dx + dy * dy <= kMatchEps * kMatchEps) {
					return {entity, &stack};
				}
			}
			return {};
		}

		/// Does `sourceDefName` harvest from a finite resource pool (trees) rather than being
		/// single-shot (ground pickup, regrowth bush)?
		[[nodiscard]] bool sourceHasResourcePool(const engine::assets::AssetRegistry& registry, const std::string& sourceDefName) {
			const auto* sourceDef = registry.getDefinition(sourceDefName);
			if (sourceDef == nullptr || !sourceDef->capabilities.harvestable.has_value()) {
				return false;
			}
			const auto& harv = sourceDef->capabilities.harvestable.value();
			return harv.totalResourceMin > 0 && harv.totalResourceMax > 0;
		}

	} // namespace

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

		uint32_t added = 0;

		// A loose ground pile: one world entity whose Appearance defName IS the material and
		// which carries a per-entity ResourceStack (the haulable remainder a fell left behind).
		// Hauling it must lift a weight-limited armful and decrement the LIVE stack, removing
		// the entity only when it empties -- never the full-destroy a Pickup's CollectionEffect
		// requests (destroySource=true), which would delete a still-loaded pile. This branch
		// intercepts before the pool/single-shot logic so a pile never reaches that destroy.
		// Only a ground pickup (sourceDefName == itemDefName, e.g. "Wood" from "Wood") may take the
		// loose-pile path. A tree-fell yields "Wood" from "Flora_Tree..."; its source differs, so it
		// must NOT be diverted here -- otherwise a pile sitting on the fell spot would hijack the chop.
		const LoosePile pile = (collEff.sourceDefName == collEff.itemDefName)
								   ? findLoosePile(world, collEff.itemDefName, collEff.sourcePosition)
								   : LoosePile{};
		if (pile.stack) {
			// A pickup from a ground pile takes only what was REQUESTED (collEff.quantity, the
			// remaining craft need), not the whole pile -- the pile is at a known reachable spot,
			// so leaving the surplus there costs nothing (pile of 7, need 2 -> take 2, leave 5).
			// Carry capacity still clamps. A harvest (the pool/single-shot branches below) instead
			// takes all it can carry, because a far harvest spot shouldn't strand cut resources.
			const uint32_t requested = std::min(collEff.quantity, pile.stack->quantity);
			if (ecs::itemIsTwoHand(harvestRegistry, collEff.itemDefName)) {
				// A two-hand armful needs both hands; stow any held one-hand tool (belt -> pack -> drop)
				// so an axe in hand doesn't block the lift.
				stowHeldToolsForArmful(inventory, collEff.sourcePosition);
				added = ecs::addArmful(inventory, harvestRegistry, collEff.itemDefName, requested);
			} else {
				const uint32_t fit = ecs::cargoUnitsThatFit(inventory, harvestRegistry, collEff.itemDefName);
				added = inventory.addItem(collEff.itemDefName, std::min(requested, fit));
			}

			pile.stack->quantity -= added; // mutate the live component so the pile tracks what is left

			if (pile.stack->quantity == 0) {
				// Remove the exact entity we drained, not the first pile matching this position:
				// two same-material piles within the match epsilon would otherwise alias.
				if (m_onRemoveEntityById) {
					m_onRemoveEntityById(pile.entity);
				}
				uint32_t pileDefNameId = harvestRegistry.getDefNameId(collEff.itemDefName);
				memory.forgetWorldEntity({collEff.sourcePosition.x, collEff.sourcePosition.y}, pileDefNameId);
				LOG_INFO(
					Engine,
					"[Action] Hauled last %u x %s from loose pile at (%.1f, %.1f) - pile removed",
					added,
					collEff.itemDefName.c_str(),
					collEff.sourcePosition.x,
					collEff.sourcePosition.y
				);
			} else {
				LOG_INFO(
					Engine,
					"[Action] Hauled %u x %s from loose pile at (%.1f, %.1f) - %u left",
					added,
					collEff.itemDefName.c_str(),
					collEff.sourcePosition.x,
					collEff.sourcePosition.y,
					pile.stack->quantity
				);
			}
			// Fall through to the shared harvest-goal credit at the end; skip pool/single-shot.
		} else if (sourceHasResourcePool(harvestRegistry, collEff.sourceDefName) && m_onDecrementResource) {
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
			// regrowth cooldown.
			//
			// Two-hand bulk goods (a felled tree's wood) ride in the hands as a weight-limited
			// armful, never the backpack. Felling is one destructive action: whatever the
			// colonist can't carry off drops as a loose, haulable ground pile, and the tree
			// always falls -- even if the colonist's hands were already full (added == 0), the
			// whole yield still drops and the source is removed, so no wood is lost and no stump
			// lingers. Everything else (one-hand) stacks into the pack and only touches the
			// source when something was actually collected.
			const bool isTwoHand = ecs::itemIsTwoHand(harvestRegistry, collEff.itemDefName);

			bool destroySource = false;
			bool regrowSource = false;

			if (isTwoHand) {
				// Felling yields a two-hand armful (wood) that needs both hands. If the colonist
				// crafted/holds a one-hand axe, it sits in a hand and would block the lift; stow it
				// (belt -> pack -> drop) first so the wood goes into the hands rather than dropping.
				stowHeldToolsForArmful(inventory, collEff.sourcePosition);
				added = ecs::addArmful(inventory, harvestRegistry, collEff.itemDefName, collEff.quantity);

				const uint32_t remainder = collEff.quantity - added;
				if (remainder > 0 && m_onDropResource) {
					m_onDropResource(collEff.itemDefName, collEff.sourcePosition.x, collEff.sourcePosition.y, remainder);
					LOG_INFO(
						Engine,
						"[Action] Dropped loose pile of %u x %s at (%.1f, %.1f)",
						remainder,
						collEff.itemDefName.c_str(),
						collEff.sourcePosition.x,
						collEff.sourcePosition.y
					);
				}
				// A fell completes regardless of how much landed in hands; bulk goods never regrow.
				destroySource = collEff.destroySource;
			} else {
				// A loose-item fetch (source IS the material, e.g. picking up a SmallStone) yields
				// only that one entity's carryable count -- never more than is physically there, even
				// if the craft requested a larger remaining need (the shortfall is met by fetching
				// further items). A harvest cut (source differs from the yield, e.g. a bush yielding
				// Sticks) is NOT capped this way: its yield is whatever the harvestable gives.
				uint32_t take = wanted;
				if (collEff.sourceDefName == collEff.itemDefName) {
					const auto* srcDef = harvestRegistry.getDefinition(collEff.itemDefName);
					if (srcDef != nullptr && srcDef->capabilities.carryable.has_value()) {
						take = std::min(take, srcDef->capabilities.carryable->quantity);
					}
				}
				added = inventory.addItem(collEff.itemDefName, take);
				destroySource = added > 0 && collEff.destroySource;
				regrowSource = added > 0 && !collEff.destroySource && collEff.regrowthTime > 0.0F;
			}

			if (added < collEff.quantity) {
				// Carry/slot-limited below the full yield -- expected, not an error.
				LOG_INFO(
					Engine, "[Action] Collected %u of %u x %s (carry-limited)", added, collEff.quantity, collEff.itemDefName.c_str()
				);
			} else {
				LOG_INFO(Engine, "[Action] Collected %u x %s", added, collEff.itemDefName.c_str());
			}

			if (destroySource) {
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
			} else if (regrowSource) {
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
				// Harvest done: the material is in the colonist's inventory now, so the Haul that
				// carries it to the station can be created here (not up front), then the goal is retired.
				goalRegistry.createHaulForCompletedHarvest(task.harvestGoalId);
				goalRegistry.removeGoal(task.harvestGoalId);
			}
		}
	}

	void ActionSystem::startHarvestAction(Task& task, Action& action, const Position& position, Memory& memory, const Inventory& inventory) {
		(void)position; // the harvest target is the remembered entity, not the colonist's stand point
		auto& registry = engine::assets::AssetRegistry::Get();

		// Goal-driven harvest: locate the harvestable DIRECTLY by the memory key the evaluator chose
		// (harvestTargetEntityId == the knownWorldEntities key from evaluateHarvestOptions), NOT by a
		// proximity search around task.targetPosition. The stand point is snapped to an adjacent
		// reachable nav face (tree centers are navmesh holes), so it sits >0.5 m from the tree; a
		// proximity scan finds nothing, forgets the (live) entry, and the AI re-selects -> the
		// phantom-harvest loop. Keying on the id harvests the exact tree the evaluator picked,
		// regardless of how far the stand point is from it.
		auto it = memory.knownWorldEntities.find(task.harvestTargetEntityId);
		if (it == memory.knownWorldEntities.end()) {
			// The chosen entity is gone from memory (consumed, forgotten, evicted). Clear the action
			// so the AI re-resolves to another source or retires -- never fall back to a proximity
			// scan that could grab a different nearby entity.
			LOG_DEBUG(
				Engine,
				"[Action] Harvest target %llu no longer in memory (goal %llu) - clearing for re-resolve",
				static_cast<unsigned long long>(task.harvestTargetEntityId),
				static_cast<unsigned long long>(task.harvestGoalId)
			);
			action.clear();
			return;
		}

		const KnownWorldEntity& entity = it->second;
		const auto&				defName = registry.getDefName(entity.defNameId);
		const auto*				def = registry.getDefinition(defName);

		// Validate the remembered entity is still a harvestable yielding what the goal wants. If not,
		// the memory entry is stale for this purpose: forget THIS specific entry (by its real
		// position/defName) and clear, so the AI re-resolves rather than chopping the wrong thing.
		const bool stillHarvestable =
			registry.hasCapability(entity.defNameId, engine::assets::CapabilityType::Harvestable) && def != nullptr &&
			def->capabilities.harvestable.has_value();
		if (!stillHarvestable) {
			memory.forgetWorldEntity(entity.position, entity.defNameId);
			LOG_WARNING(
				Engine,
				"[Action] Harvest target %s at (%.1f, %.1f) is no longer harvestable (goal %llu) - forgot, clearing",
				defName.c_str(),
				entity.position.x,
				entity.position.y,
				static_cast<unsigned long long>(task.harvestGoalId)
			);
			action.clear();
			return;
		}

		const auto& harvestCap = def->capabilities.harvestable.value();

		// Verify this harvestable yields the item the goal needs. A mismatch means the chosen target
		// no longer serves this goal: forget it and clear so a correct source is re-resolved.
		uint32_t yieldDefNameId = registry.getDefNameId(harvestCap.yieldDefName);
		if (yieldDefNameId != task.harvestYieldDefNameId) {
			memory.forgetWorldEntity(entity.position, entity.defNameId);
			LOG_WARNING(
				Engine,
				"[Action] Harvest target %s yields %u, goal wants %u (goal %llu) - forgot, clearing",
				defName.c_str(),
				yieldDefNameId,
				task.harvestYieldDefNameId,
				static_cast<unsigned long long>(task.harvestGoalId)
			);
			action.clear();
			return;
		}

		// Chopping a tree needs the right tool. The decision layer already filters these out for
		// tool-less colonists; this guards against a stale task producing a chop the colonist can't
		// perform. The entity itself is still valid (a real tree), so do NOT forget it -- just clear
		// the action and let the AI re-resolve; another colonist with an axe can still cut it.
		if (!ecs::inventoryHoldsToolType(inventory, registry, harvestCap.requiredToolType)) {
			LOG_DEBUG(
				Engine, "[Action] Cannot harvest %s without a %s tool - clearing", defName.c_str(), harvestCap.requiredToolType.c_str()
			);
			action.clear();
			return;
		}

		// Calculate yield amount
		uint32_t yieldAmount = harvestCap.amountMin;
		if (harvestCap.amountMax > harvestCap.amountMin) {
			std::uniform_int_distribution<uint32_t> yieldDist(harvestCap.amountMin, harvestCap.amountMax);
			yieldAmount = yieldDist(m_rng);
		}

		// Build the Harvest at the ENTITY's position (the actual tree), not task.targetPosition (the
		// snapped stand point). applyCollectionEffect resolves the source pool/pile by this position.
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
			"[Action] Starting goal-driven Harvest action for %s from %s at (%.1f, %.1f) (durability %.0f, goal %llu)",
			harvestCap.yieldDefName.c_str(),
			defName.c_str(),
			entity.position.x,
			entity.position.y,
			harvestCap.durability,
			static_cast<unsigned long long>(task.harvestGoalId)
		);
	}

} // namespace ecs
