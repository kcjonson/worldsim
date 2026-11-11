#include "../ActionSystem.h"

#include "../../GoalTaskRegistry.h"
#include "../../InventoryMass.h"
#include "../../World.h"
#include "../../components/Action.h"
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

		/// Find the loose ground pile of `defNameId` filed at `pos`'s world key (the key Memory
		/// remembers it under). Returns the entity id and its live ResourceStack, so a drained pile
		/// is removed by exact id.
		[[nodiscard]] LoosePile findLoosePile(World* world, const LoosePileIndex& piles, uint32_t defNameId, glm::vec2 pos) {
			if (world == nullptr) {
				return {};
			}
			const EntityID entity = piles.find(*world, defNameId, pos);
			if (entity == kInvalidEntity) {
				return {};
			}
			ResourceStack* stack = world->getComponent<ResourceStack>(entity);
			return stack != nullptr ? LoosePile{entity, stack} : LoosePile{};
		}

		/// Does `sourceDefName` harvest from a finite resource pool (trees) rather than being
		/// single-shot (ground pickup, regrowth bush)?
		[[nodiscard]] bool sourceHasResourcePool(const assets::AssetRegistry& registry, const std::string& sourceDefName) {
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
		glm::vec2	  colonistPosition,
		Inventory&	  inventory,
		Memory&		  memory
	) {
		const auto& collEff = action.collectionEffect();

		auto& harvestRegistry = assets::AssetRegistry::Get();

		// Cap this withdrawal by what the colonist can actually take this action: the carry weight
		// (tools are equipment and don't count), and the pack's stack/slot headroom or, for a two-hand
		// good, the armful's. Clamping by it means a pool is only debited by what truly lands in the
		// inventory, so a tree's wood is conserved. At the limit `wanted` is 0: take nothing and leave
		// the source for the next trip rather than dropping items on the floor.
		const uint32_t room =
			ecs::pickupUnitsThatFit(inventory, harvestRegistry, collEff.itemDefName, ecs::carriedCargoMassKg(inventory, harvestRegistry));
		const uint32_t wanted = std::min(collEff.quantity, room);

		uint32_t added = 0;

		// Take the source from the world and report what it was just before; a source that is gone,
		// whoever took it, is forgotten so the colonist doesn't walk back to it.
		const auto takeSource = [&](SourceTake take) {
			const SourceState state =
				m_onTakeSource
					? m_onTakeSource(collEff.sourceDefName, collEff.sourcePosition.x, collEff.sourcePosition.y, take, collEff.regrowthTime)
					: SourceState::Ready;
			if (take == SourceTake::Remove || state == SourceState::Gone) {
				memory.forgetWorldEntity(collEff.sourcePosition, harvestRegistry.getDefNameId(collEff.sourceDefName));
			}
			if (state != SourceState::Ready) {
				LOG_DEBUG(
					Engine,
					"[Action] Source %s at (%.1f, %.1f) was %s at completion: no yield",
					collEff.sourceDefName.c_str(),
					collEff.sourcePosition.x,
					collEff.sourcePosition.y,
					state == SourceState::Gone ? "gone" : "regrowing"
				);
			}
			return state;
		};

		// Yield the colonist couldn't carry off drops beside the source as one loose, haulable pile.
		const auto dropUncarried = [&](uint32_t quantity) {
			if (quantity == 0 || !m_onDropResource) {
				return;
			}
			m_onDropResource(collEff.itemDefName, collEff.sourcePosition.x, collEff.sourcePosition.y, quantity);
			LOG_INFO(
				Engine,
				"[Action] Dropped loose pile of %u x %s at (%.1f, %.1f)",
				quantity,
				collEff.itemDefName.c_str(),
				collEff.sourcePosition.x,
				collEff.sourcePosition.y
			);
		};

		// Storage-to-storage pull (ActionType::Withdraw): take the item OUT of the source box's
		// Inventory, not a ground entity. removeItem clamps to the box's live quantity, so a concurrent
		// second colonist that beat us to it just withdraws less and the AI re-evaluates next tick -- the
		// same self-correcting race handling the loose pickup relies on. Add exactly what we removed to
		// the colonist (sized by pickupUnitsThatFit via `wanted`). This intercepts before every ground
		// path; a Withdraw never touches a pile, pool, or single-shot source.
		if (collEff.sourceStorageId != 0) {
			const auto sourceBox = static_cast<EntityID>(collEff.sourceStorageId);
			auto*	   sourceInv = world != nullptr ? world->getComponent<Inventory>(sourceBox) : nullptr;
			if (sourceInv == nullptr) {
				LOG_WARNING(
					Engine,
					"[Action] Withdraw failed: source box %llu has no Inventory",
					static_cast<unsigned long long>(collEff.sourceStorageId)
				);
				return;
			}
			const uint32_t removed = sourceInv->removeItem(collEff.itemDefName, wanted);
			// A two-hand good (wood) can't ride in the pack: it must seat into the hands as a
			// weight-limited armful, mirroring the loose-pile and single-shot branches above/below.
			// Stow any held one-hand tool first so it doesn't block the lift. addItem would silently
			// violate the two-hand invariant, then the deposit leg's removeFromHands would find empty
			// hands and strand the good -- breaking the Wood box-to-box case.
			if (ecs::itemIsTwoHand(harvestRegistry, collEff.itemDefName)) {
				stowHeldToolsForArmful(inventory, colonistPosition);
				added = ecs::addArmful(inventory, harvestRegistry, collEff.itemDefName, removed);
			} else {
				added = inventory.addItem(collEff.itemDefName, removed);
			}
			if (added < removed) {
				// Colonist couldn't hold all we pulled (raced carry/slot state, or a two-hand stack/
				// weight cap): put the remainder back in the source box so nothing is lost.
				sourceInv->addItem(collEff.itemDefName, removed - added);
			}
			LOG_INFO(
				Engine,
				"[Action] Withdrew %u x %s from box %llu (now carrying %u)",
				added,
				collEff.itemDefName.c_str(),
				static_cast<unsigned long long>(collEff.sourceStorageId),
				inventory.getQuantity(collEff.itemDefName)
			);
			return; // a pull's harvest-goal credit is N/A; the deposit leg credits the umbrella
		}

		// A loose ground pile: one world entity whose Appearance defName IS the material and
		// which carries a per-entity ResourceStack (the haulable remainder a fell left behind).
		// Hauling it must lift a weight-limited armful and decrement the LIVE stack, removing
		// the entity only when it empties -- never the full-destroy a Pickup's CollectionEffect
		// requests (destroySource=true), which would delete a still-loaded pile. This branch
		// intercepts before the pool/single-shot logic so a pile never reaches that destroy.
		// Only a ground pickup (sourceDefName == itemDefName, e.g. "Wood" from "Wood") may take the
		// loose-pile path. A tree-fell yields "Wood" from "Flora_Tree..."; its source differs, so it
		// must NOT be diverted here -- otherwise a pile sitting on the fell spot would hijack the chop.
		// A pickup whose pile has left the index goes on to the single-shot branch, where the world never
		// answers for a pile: the source reads Gone, its memory entry is forgotten, and nothing is taken.
		const LoosePile pile = (collEff.sourceDefName == collEff.itemDefName)
								   ? findLoosePile(world, m_loosePiles, harvestRegistry.getDefNameId(collEff.itemDefName), collEff.sourcePosition)
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
				stowHeldToolsForArmful(inventory, colonistPosition);
				added = ecs::addArmful(inventory, harvestRegistry, collEff.itemDefName, requested);
			} else {
				added = inventory.addItem(collEff.itemDefName, std::min(requested, room));
			}

			pile.stack->quantity -= added; // mutate the live component so the pile tracks what is left

			if (pile.stack->quantity == 0) {
				// Remove the exact entity we drained, not the first pile matching this position:
				// two same-material piles in one cell would otherwise alias.
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
			// Withdraw only what the colonist can carry; the pool clamps to its own contents and
			// reports depletion, so a bush's sticks are conserved across trips. A clear takes the
			// whole pool in one pass and removes the bush: the pack takes all that fits (not one cut's
			// yield) and the rest drops. The source must still stand before its pool pays, as for a
			// single-shot source. Removing the source erases its pool, so the removal follows the draw.
			const uint32_t	  request = collEff.clearsSource ? UINT32_MAX : wanted;
			const uint32_t	  packRoom = collEff.clearsSource ? room : wanted;
			const SourceState state = takeSource(SourceTake::Keep);
			if (state != SourceState::Ready) {
				if (state == SourceState::OnCooldown && collEff.clearsSource) {
					takeSource(SourceTake::Remove); // a clear still removes a regrowing source, with no yield
				}
			} else if (request > 0) {
				const ResourceDraw draw =
					m_onDecrementResource(collEff.sourceDefName, collEff.sourcePosition.x, collEff.sourcePosition.y, request);
				added = inventory.addItem(collEff.itemDefName, std::min(draw.removed, packRoom));
				dropUncarried(draw.removed - added);
				LOG_INFO(
					Engine,
					"[Action] Chopped %u x %s (now carrying %u)",
					added,
					collEff.itemDefName.c_str(),
					inventory.getQuantity(collEff.itemDefName)
				);
				if (draw.depleted || collEff.clearsSource) {
					takeSource(SourceTake::Remove);
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
			// regrowth cooldown. The source is taken from the world first and pays only if it was
			// still standing and not regrowing, so a later finisher on the same plant gets nothing.
			//
			// Two-hand bulk goods (a felled tree's wood) ride in the hands as a weight-limited
			// armful, never the backpack. Felling is one destructive action: whatever the
			// colonist can't carry off drops as a loose, haulable ground pile, and the tree
			// always falls -- even if the colonist's hands were already full (added == 0), the
			// whole yield still drops and the source is removed, so no wood is lost and no stump
			// lingers. Everything else (one-hand) stacks into the pack and only touches the
			// source when something fits, except a footprint clear, which removes it regardless
			// and drops what doesn't fit, like a fell.
			const bool isTwoHand = ecs::itemIsTwoHand(harvestRegistry, collEff.itemDefName);

			if (isTwoHand) {
				// A fell completes regardless of how much lands in hands; bulk goods never regrow.
				const bool fells = collEff.destroySource || collEff.clearsSource;
				if (takeSource(fells ? SourceTake::Remove : SourceTake::Keep) == SourceState::Ready) {
					// Felling yields a two-hand armful (wood) that needs both hands. If the colonist
					// crafted/holds a one-hand axe, it sits in a hand and would block the lift; stow it
					// (belt -> pack -> drop) first so the wood goes into the hands rather than dropping.
					stowHeldToolsForArmful(inventory, colonistPosition);
					added = ecs::addArmful(inventory, harvestRegistry, collEff.itemDefName, collEff.quantity);
					dropUncarried(collEff.quantity - added);
				}
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
				SourceTake leaves = SourceTake::Keep;
				if (collEff.clearsSource || collEff.destroySource) {
					leaves = SourceTake::Remove;
				} else if (collEff.regrowthTime > 0.0F) {
					leaves = SourceTake::Regrow;
				}
				// A cut gives its whole yield: what the pack can't take drops beside the plant as a loose pile,
				// as a fell's overflow does, rather than being lost. A loose-item fetch takes only its own item.
				const bool cut = collEff.sourceDefName != collEff.itemDefName;
				if ((take > 0 || collEff.clearsSource) && takeSource(leaves) == SourceState::Ready) {
					added = inventory.addItem(collEff.itemDefName, take);
					if (cut || collEff.clearsSource) {
						dropUncarried(collEff.quantity - added);
					}
				}
			}

			// Below the full yield when carry- or slot-limited, or the source was gone or regrowing.
			LOG_INFO(Engine, "[Action] Collected %u of %u x %s", added, collEff.quantity, collEff.itemDefName.c_str());
		}
	}

	void ActionSystem::startHarvestAction(Task& task, Action& action, const Position& position, Memory& memory, const Inventory& inventory) {
		(void)position; // the harvest target is the remembered entity, not the colonist's stand point
		auto& registry = assets::AssetRegistry::Get();

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
			registry.hasCapability(entity.defNameId, assets::CapabilityType::Harvestable) && def != nullptr &&
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
		// A footprint clear removes its source whatever kind it is: regrowing, pooled, or single-shot.
		const auto* goal = GoalTaskRegistry::Get().getGoal(task.harvestGoalId);
		action.collectionEffect().clearsSource = goal != nullptr && goal->clearFootprint.has_value();
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
