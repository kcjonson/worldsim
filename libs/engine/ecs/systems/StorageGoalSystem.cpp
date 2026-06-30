#include "StorageGoalSystem.h"

#include "GoalSystemHelpers.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/StorageConfiguration.h"
#include "../components/Transform.h"

#include <assets/AssetRegistry.h>

#include <algorithm>
#include <cstdint>

namespace ecs {

	namespace {
		// Generate a unique chain ID linking a stocking Harvest to the carry-in Haul it spawns
		// (createHaulForCompletedHarvest copies the chainId). The carry-in Haul is recognized as a
		// stocking delivery -- not an ordinary loose-pile haul -- by being StorageGoalSystem-owned
		// AND having a chainId, so the chainId must be non-zero/unique. Internal linkage: a static
		// local counter, distinct from other systems' generators.
		uint64_t generateChainId() {
			static uint64_t nextChainId = 1;
			return nextChainId++;
		}

	} // namespace

	void StorageGoalSystem::update(float /*deltaTime*/) {
		if (world == nullptr) {
			return;
		}

		// Throttle: only update every N frames
		frameCounter++;
		if (frameCounter < updateFrameInterval) {
			return;
		}
		frameCounter = 0;

		auto& registry = GoalTaskRegistry::Get();
		auto& assetRegistry = engine::assets::AssetRegistry::Get();

		// Track existing goals to detect removed storages
		// Using ownership: only track goals that StorageGoalSystem created
		std::unordered_set<EntityID> storagesWithGoals;
		for (const auto* goal : registry.getGoalsByOwner(GoalOwner::StorageGoalSystem)) {
			storagesWithGoals.insert(goal->destinationEntity);
		}

		activeGoalCount = 0;

		// One trip's carry budget for sizing stocking harvests (shared with the other goal systems).
		const float maxColonistCarryKg = colonistCarryCapacityKg(world);

		// Cascade-remove a storage's umbrella Haul goal AND its stocking children (Harvest goals
		// and the carry-in Hauls they spawn hang off the umbrella as children). A plain
		// removeGoalByDestination drops only the top-level Haul and would orphan the children.
		auto removeStorageGoalTree = [&registry](EntityID storage) {
			const auto* g = registry.getGoalByDestination(storage);
			if (g != nullptr) {
				registry.removeGoalWithChildren(g->id);
			}
		};

		// Query all entities with StorageConfiguration + Inventory + Position
		for (auto [entity, config, inventory, position] :
			 world->view<StorageConfiguration, Inventory, Position>()) {
			// Only process storages with rules configured
			if (!config.hasRules()) {
				// No rules = storage not configured yet
				// Remove any existing goal (and stocking children)
				removeStorageGoalTree(entity);
				storagesWithGoals.erase(entity);
				continue;
			}

			// Existence/full gate: a storage wants items as long as it has a free slot. Storage
			// capacity is a slot count; each stack occupies one slot. This is the authoritative
			// "is there any room" check -- it creates the goal while a slot is free and removes it
			// when the storage is slot-full. How MUCH a single haul carries is sized elsewhere,
			// per item, from the destination's addableCount (stack headroom + freeSlots *
			// stackSize); it is not the slot count. targetAmount below mirrors the free-slot count
			// purely as a "still wants items" signal (availableCapacity() > 0) for the task UI and
			// the decision layer; it is NOT a haul-size or a delivery quota.
			uint32_t usedSlots = inventory.getSlotCount();
			uint32_t availableSlots = 0;
			if (usedSlots < inventory.maxCapacity) {
				availableSlots = inventory.maxCapacity - usedSlots;
			}

			if (availableSlots == 0) {
				// Full - remove goal (and stocking children) if exists
				removeStorageGoalTree(entity);
				storagesWithGoals.erase(entity);
				continue;
			}

			// Build list of accepted defNameIds from storage rules
			std::vector<uint32_t> acceptedDefNameIds;
			engine::assets::ItemCategory primaryCategory = engine::assets::ItemCategory::None;

			for (const auto& rule : config.rules) {
				if (rule.isWildcard()) {
					// Wildcard rule - accept entire category
					// We'll track the category for filtering
					primaryCategory = rule.category;
				} else {
					// Specific item rule
					uint32_t defNameId = assetRegistry.getDefNameId(rule.defName);
					if (defNameId != 0) {
						acceptedDefNameIds.push_back(defNameId);
					}
				}
			}

			// Check if goal already exists
			uint64_t	umbrellaId = 0;
			const auto* existingGoal = registry.getGoalByDestination(entity);
			if (existingGoal != nullptr) {
				// Refresh the "wants items" signal from the live free-slot count (deposits shrank
				// it). deliveredAmount resets to 0 each refresh: it isn't a real quota for storage
				// (a haul's true clamp is the destination's per-item addableCount at deposit
				// time), just bookkeeping so availableCapacity() == targetAmount == free slots.
				umbrellaId = existingGoal->id;
				registry.updateGoal(existingGoal->id, [&](GoalTask& goal) {
					goal.targetAmount = availableSlots;
					goal.deliveredAmount = 0;
					goal.acceptedDefNameIds = acceptedDefNameIds;
					goal.acceptedCategory = primaryCategory;
				});
			} else {
				// Create new umbrella Haul goal
				GoalTask goal;
				goal.type = TaskType::Haul;
				goal.owner = GoalOwner::StorageGoalSystem;
				goal.destinationEntity = entity;
				goal.destinationPosition = position.value;
				goal.destinationDefNameId = 0;
				goal.acceptedDefNameIds = acceptedDefNameIds;
				goal.acceptedCategory = primaryCategory;
				goal.targetAmount = availableSlots;
				goal.deliveredAmount = 0;
				goal.createdAt = 0.0F; // TODO: use actual game time

				umbrellaId = registry.createGoal(std::move(goal));
			}

			// Stocking demand: a SPECIFIC-ITEM rule (not a category wildcard) with minAmount above
			// the count already in the box drives chopping to produce the wanted item. For each such
			// shortfall, if a colonist knows a harvestable that yields the item, emit a Harvest goal
			// (a child of the umbrella) so the colonist cuts -> the cut yield is carried -> a carry-in
			// Haul (spawned on harvest completion, inheriting the chainId) deposits it into the box,
			// until the box holds minAmount. Category wildcard rules never drive harvest: they only
			// accept hauled items, as before. Stocking work is tier 6 (opportunistic) in the
			// (tier, score) arbitration: above idle (tier 7) but strictly below active work orders
			// (tier 4), so a colonist always prefers construction/craft work over filling a box.
			if (umbrellaId != 0) {
				reconcileStockingHarvests(registry, assetRegistry, config, inventory, umbrellaId, maxColonistCarryKg);
			}

			storagesWithGoals.erase(entity);
			activeGoalCount++;
		}

		// Remove goals (and stocking children) for storages that no longer exist
		for (EntityID oldStorage : storagesWithGoals) {
			removeStorageGoalTree(oldStorage);
		}
	}

	void StorageGoalSystem::reconcileStockingHarvests(
		GoalTaskRegistry&					 registry,
		const engine::assets::AssetRegistry& assetRegistry,
		const StorageConfiguration&			 config,
		const Inventory&					 inventory,
		uint64_t							 umbrellaId,
		float								 maxColonistCarryKg
	) {
		// What stocking work is already in flight for this box, per item: a Harvest child being cut,
		// or a carry-in Haul child (chainId'd) carrying the cut yield in. Either means "this item is
		// being stocked right now" -- don't emit a duplicate Harvest for it.
		// A box stocks only a handful of distinct items; a small vector with a linear scan beats a
		// per-tick heap-allocating set.
		std::vector<uint32_t> itemsBeingStocked;
		for (const auto* child : registry.getChildGoals(umbrellaId)) {
			if (child == nullptr) {
				continue;
			}
			if (child->type == TaskType::Harvest && child->yieldDefNameId != 0) {
				itemsBeingStocked.push_back(child->yieldDefNameId);
			} else if (child->type == TaskType::Haul && child->chainId.has_value() && !child->acceptedDefNameIds.empty()) {
				itemsBeingStocked.push_back(child->acceptedDefNameIds.front());
			}
		}

		const auto* umbrella = registry.getGoal(umbrellaId);
		if (umbrella == nullptr) {
			return;
		}
		const EntityID	destinationEntity = umbrella->destinationEntity;
		const glm::vec2 destinationPosition = umbrella->destinationPosition;

		for (const auto& rule : config.rules) {
			if (rule.isWildcard() || rule.minAmount == 0) {
				continue; // wildcards never drive harvest; a 0 minimum has no pull
			}
			const uint32_t itemDefNameId = assetRegistry.getDefNameId(rule.defName);
			if (itemDefNameId == 0) {
				continue; // unknown item
			}
			if (std::find(itemsBeingStocked.begin(), itemsBeingStocked.end(), itemDefNameId) != itemsBeingStocked.end()) {
				continue; // already harvesting / carrying this item in
			}

			// Shortfall against the configured minimum, measured from what the box already holds.
			const uint32_t current = inventory.getQuantity(rule.defName);
			if (current >= rule.minAmount) {
				continue; // minimum already met
			}
			const uint32_t shortfall = rule.minAmount - current;

			// Physical headroom: don't chop what the box can't actually hold (e.g. its slot for this
			// item is at the stack cap with no free slot). addableCount is stack room + freeSlots *
			// stackSize, the same clamp the haul sizing uses.
			const uint32_t headroom = inventory.addableCount(rule.defName);
			if (headroom == 0) {
				continue;
			}

			// Only chop if a colonist actually knows a harvestable source for this item; otherwise
			// the box just waits for the item to be hauled in (or never filled). Mirrors the
			// crafting BOM-shortfall harvest gate.
			if (!colonyKnowsHarvestableSource(world, assetRegistry, itemDefNameId)) {
				continue;
			}

			// Size the chop to ONE trip's carry capacity, capped by the shortfall and the box's
			// headroom. A shortfall larger than a colonist can carry is filled over multiple trips:
			// the Harvest completes at one trip's worth, spawns the carry-in Haul, and the next tick
			// re-emits a fresh Harvest for the remaining shortfall (mirrors ConstructionSystem's
			// per-trip harvest demand). Without the cap the Harvest would never reach availableCapacity
			// 0 (the colonist fills up first), never spawn the carry-in, and strand the box.
			const uint32_t perTrip = ecs::cargoUnitsPerTrip(assetRegistry, rule.defName, maxColonistCarryKg);
			const uint32_t demand = std::min({shortfall, headroom, perTrip > 0 ? perTrip : shortfall});
			if (demand == 0) {
				continue;
			}

			GoalTask harvest;
			harvest.type = TaskType::Harvest;
			harvest.owner = GoalOwner::StorageGoalSystem;
			harvest.destinationEntity = destinationEntity;
			harvest.destinationPosition = destinationPosition;
			harvest.destinationDefNameId = 0;
			harvest.acceptedDefNameIds = {itemDefNameId};
			harvest.acceptedCategory = engine::assets::ItemCategory::None;
			harvest.targetAmount = demand;
			harvest.deliveredAmount = 0;
			harvest.createdAt = 0.0F;
			harvest.parentGoalId = umbrellaId;
			harvest.status = GoalStatus::Available;
			harvest.yieldDefNameId = itemDefNameId;
			// chainId tags the carry-in Haul (spawned on harvest completion) as a stocking delivery,
			// so the decision layer routes it from inventory into the box rather than treating it as
			// an ordinary loose-pile haul.
			harvest.chainId = generateChainId();

			registry.createGoal(std::move(harvest));
		}
	}

} // namespace ecs
