#pragma once

// Carry-weight helpers: turn an Inventory + AssetRegistry into kilograms and
// per-trip unit counts. Lives outside Inventory itself because mass is an asset
// property the component can't resolve on its own.
//
// Tools (ItemCategory::Tool) are treated as equipment, not cargo: a colonist's axe
// does not eat into how much wood they can haul. This keeps the per-trip bound the
// construction system computes (from an empty colonist) consistent with the clamp
// the action system applies at harvest time, so a tool-carrying colonist never
// stalls one unit short of the goal's trip target.

#include <algorithm>
#include <cmath>
#include <string>

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "components/Inventory.h"

namespace ecs {

	// ── Pure mass arithmetic (no registry; unit-testable in isolation) ──────────

	/// How many whole units of mass `massKg` fit in `remainingKg` of free capacity.
	/// A massless item is never weight-limited (returns max). Negative headroom is 0.
	[[nodiscard]] inline uint32_t massUnitsThatFit(float remainingKg, float massKg, uint32_t masslessCap) {
		if (massKg <= 0.0F) {
			return masslessCap;
		}
		if (remainingKg <= 0.0F) {
			return 0;
		}
		return static_cast<uint32_t>(std::floor(remainingKg / massKg));
	}

	/// Whole units of mass `massKg` an empty carrier with `capacityKg` hauls per trip. At
	/// least 1 so a unit heavier than the cap still makes progress instead of stalling at 0.
	[[nodiscard]] inline uint32_t massUnitsPerTrip(float capacityKg, float massKg) {
		if (massKg <= 0.0F || capacityKg <= 0.0F) {
			return 1;
		}
		return std::max(1U, static_cast<uint32_t>(std::floor(capacityKg / massKg)));
	}

	/// Mass of everything the inventory carries that counts as cargo, kilograms.
	/// Excludes tools (equipment). Hand slots and backpack are both summed.
	[[nodiscard]] inline float carriedCargoMassKg(const Inventory& inv, const engine::assets::AssetRegistry& registry) {
		float total = 0.0F;

		auto addStack = [&](const std::string& defName, uint32_t quantity) {
			if (quantity == 0 || defName.empty()) {
				return;
			}
			const auto* def = registry.getDefinition(defName);
			if (def != nullptr && def->category == engine::assets::ItemCategory::Tool) {
				return; // equipment, not cargo
			}
			total += registry.getItemMassKg(defName) * static_cast<float>(quantity);
		};

		if (inv.leftHand.has_value()) {
			addStack(inv.leftHand->defName, inv.leftHand->quantity);
		}
		// A two-handed item shows in both hands as the same stack; counting both double-
		// counts it. Skip the right hand when it mirrors the left.
		if (inv.rightHand.has_value() && !(inv.leftHand.has_value() && inv.leftHand->defName == inv.rightHand->defName)) {
			addStack(inv.rightHand->defName, inv.rightHand->quantity);
		}
		for (const auto& stack : inv.items) {
			addStack(stack.defName, stack.quantity);
		}
		return total;
	}

	/// How many more units of `defName` fit before hitting the cargo weight cap. Weight is the
	/// only bound here; per-stack count limits live in Inventory (addableCount), and a hand
	/// armful is weight-limited by design, so a massless item is unbounded.
	[[nodiscard]] inline uint32_t cargoUnitsThatFit(const Inventory& inv, const engine::assets::AssetRegistry& registry, const std::string& defName) {
		const float remaining = inv.carryCapacityKg - carriedCargoMassKg(inv, registry);
		return massUnitsThatFit(remaining, registry.getItemMassKg(defName), UINT32_MAX);
	}

	/// Does the inventory hold an item that provides `toolType` (e.g. "Axe")? Checks the
	/// hands, belt, and backpack; a held, belted, or stowed tool counts. Empty toolType
	/// always passes (the work needs no tool).
	[[nodiscard]] inline bool inventoryHoldsToolType(const Inventory& inv, const engine::assets::AssetRegistry& registry, const std::string& toolType) {
		if (toolType.empty()) {
			return true;
		}
		auto matches = [&](const std::string& defName) {
			return registry.getToolType(registry.getDefNameId(defName)) == toolType;
		};
		if (inv.leftHand.has_value() && matches(inv.leftHand->defName)) {
			return true;
		}
		if (inv.rightHand.has_value() && matches(inv.rightHand->defName)) {
			return true;
		}
		for (const auto& slot : inv.belt) {
			if (slot.has_value() && matches(slot->defName)) {
				return true;
			}
		}
		for (const auto& stack : inv.items) {
			if (stack.quantity > 0 && matches(stack.defName)) {
				return true;
			}
		}
		return false;
	}

	/// How many units of `defName` an empty colonist with `carryCapacityKg` carries in one trip.
	[[nodiscard]] inline uint32_t cargoUnitsPerTrip(const engine::assets::AssetRegistry& registry, const std::string& defName, float carryCapacityKg) {
		return massUnitsPerTrip(carryCapacityKg, registry.getItemMassKg(defName));
	}

	// ── Hand-carried bulk materials ─────────────────────────────────────────────
	// Two-hand items (handsRequired >= 2, e.g. Wood) live in the hands as a weight-limited
	// armful and never enter the backpack, so anything that reads or consumes materials must
	// look at the hands, not just `items`. A two-hand armful occupies BOTH hands as the same
	// logical stack (equal quantity, same defName); these helpers preserve that mirror.

	/// True if `defName` must be carried in both hands (so it lives in the hands, not the pack).
	[[nodiscard]] inline bool itemIsTwoHand(const engine::assets::AssetRegistry& registry, const std::string& defName) {
		const auto* def = registry.getDefinition(defName);
		return def != nullptr && def->handsRequired >= 2;
	}

	/// Quantity of `defName` held in the hands. A two-hand item mirrors across both hands; it is
	/// counted once (right skipped when it mirrors the left), matching carriedCargoMassKg.
	[[nodiscard]] inline uint32_t handHeldQuantity(const Inventory& inv, const std::string& defName) {
		uint32_t qty = 0;
		if (inv.leftHand.has_value() && inv.leftHand->defName == defName) {
			qty += inv.leftHand->quantity;
		}
		if (inv.rightHand.has_value() && inv.rightHand->defName == defName &&
			!(inv.leftHand.has_value() && inv.leftHand->defName == inv.rightHand->defName)) {
			qty += inv.rightHand->quantity;
		}
		return qty;
	}

	/// Total `defName` a colonist can draw on: backpack plus hands. Callers that read materials
	/// (craft input, haul, deposit) must use this so hand-carried two-hand goods are seen.
	[[nodiscard]] inline uint32_t availableQuantity(const Inventory& inv, const std::string& defName) {
		return inv.getQuantity(defName) + handHeldQuantity(inv, defName);
	}

	/// Remove up to `quantity` of `defName` from the hands, keeping the two-hand mirror in sync
	/// (both hands decrement together and clear together). Returns the amount removed.
	inline uint32_t removeFromHands(Inventory& inv, const std::string& defName, uint32_t quantity) {
		const bool inLeft = inv.leftHand.has_value() && inv.leftHand->defName == defName;
		const bool inRight = inv.rightHand.has_value() && inv.rightHand->defName == defName;
		if (!inLeft && !inRight) {
			return 0;
		}
		const bool mirrored = inLeft && inRight; // a two-hand item occupies both hands
		ItemStack& primary = inLeft ? inv.leftHand.value() : inv.rightHand.value();
		const uint32_t toRemove = std::min(quantity, primary.quantity);
		primary.quantity -= toRemove;
		if (mirrored) {
			(inLeft ? inv.rightHand : inv.leftHand)->quantity = primary.quantity; // stay mirrored
		}
		if (primary.quantity == 0) {
			if (inLeft) {
				inv.leftHand.reset();
			}
			if (inRight) {
				inv.rightHand.reset();
			}
		}
		return toRemove;
	}

	/// Add a weight-limited armful of a two-hand material to the hands: grow an armful of the
	/// same material already held, or seat a new one (which needs both hands free). Clamps to
	/// the carry-weight cap and keeps the two-hand mirror in sync. Returns the amount lifted.
	inline uint32_t addArmful(Inventory& inv, const engine::assets::AssetRegistry& registry, const std::string& defName, uint32_t quantity) {
		// An armful is one stack: bounded by carry weight AND the item's stackSize (the carry rule
		// is weight-or-count, whichever binds first). For wood weight binds well below 40, so the
		// stack cap only bites for lighter materials. Unknown/unbounded items fall back to weight.
		const auto*	   def		 = registry.getDefinition(defName);
		const uint32_t stackCap	 = (def != nullptr && def->itemProperties.has_value()) ? def->itemProperties->stackSize : UINT32_MAX;
		const uint32_t held		 = handHeldQuantity(inv, defName);
		const uint32_t stackRoom = (held >= stackCap) ? 0U : stackCap - held;
		const uint32_t lifted	 = std::min({quantity, cargoUnitsThatFit(inv, registry, defName), stackRoom});
		if (lifted == 0) {
			return 0;
		}
		if (inv.leftHand.has_value() && inv.leftHand->defName == defName) {
			inv.leftHand->quantity += lifted;
			if (inv.rightHand.has_value() && inv.rightHand->defName == defName) {
				inv.rightHand->quantity += lifted;
			}
			return lifted;
		}
		if (!inv.hasHandsFree(2)) {
			return 0;
		}
		inv.leftHand = ItemStack{defName, lifted};
		inv.rightHand = ItemStack{defName, lifted};
		return lifted;
	}

	/// Stow any ONE-hand items held in the hands out of the way so both hands are free for a
	/// two-hand armful. Stow order per slot: belt (if a slot is free) -> pack (by weight) -> drop.
	/// A two-hand item already in the hands (an existing armful mirrored across both hands) is left
	/// alone -- it can't be stowed anywhere, and a fresh two-hand pickup grows it rather than needing
	/// free hands. Tools (an axe) stay valid for the harvest tool-check once belted/packed, so the
	/// colonist keeps working after stowing. `onDrop(defName, count)` performs the loose-ground drop
	/// for whatever neither the belt nor the pack can take.
	template <typename DropFn>
	inline void stowHeldToolsToFreeHands(Inventory& inv, const engine::assets::AssetRegistry& registry, DropFn&& onDrop) {
		auto stowOne = [&](std::optional<ItemStack>& hand) {
			if (!hand.has_value()) {
				return;
			}
			const std::string defName  = hand->defName;
			const uint32_t	  quantity = hand->quantity;
			// A two-hand item occupies both hands as one mirrored stack; it can't go to belt/pack and
			// must not be dropped here (it's not in the way of itself). Leave it for addArmful to grow.
			if (ecs::itemIsTwoHand(registry, defName)) {
				return;
			}
			// 1. Belt: one one-hand item per slot, quantity 1 (the common held-tool case).
			if (quantity == 1 && inv.stowToBelt(defName)) {
				hand.reset();
				return;
			}
			// 2. Pack: weight-respecting, only as many units as fit under the cargo cap.
			const uint32_t fits	 = ecs::cargoUnitsThatFit(inv, registry, defName);
			const uint32_t toPack = std::min(quantity, fits);
			const uint32_t packed = toPack > 0 ? inv.addItem(defName, toPack) : 0U;
			// 3. Ground: whatever the belt and pack couldn't take drops at the colonist's feet.
			const uint32_t dropped = quantity - packed;
			if (dropped > 0) {
				onDrop(defName, dropped);
			}
			hand.reset();
		};
		stowOne(inv.leftHand);
		stowOne(inv.rightHand);
	}

	/// Canonical "give an item to a colonist" cascade, weight-respecting at every step.
	/// A fresh output (a crafted tool, a gift) lands somewhere sensible instead of being
	/// force-stuffed into one slot. Seating order for a ONE-hand item:
	///   1. an empty hand           (so a just-made axe is actually held)
	///   2. a free belt slot        (quick-draw stow when hands are full)
	///   3. the backpack            (stack/slot limited)
	///   4. drop on the ground      (nothing fits) -- via onDrop, at the colonist's position
	/// A TWO-hand item can only ride in the hands (addArmful) or drop; it never enters belt/backpack.
	/// Weight binds throughout: cargoUnitsThatFit gates how many units may be carried before the
	/// carry-weight cap, so an over-weight colonist's output drops rather than exceeding the cap.
	/// `onDrop(defName, count)` performs the loose-ground drop (the harvest overflow mechanism).
	/// Returns the number of units that ended up carried (hands + belt + backpack); the rest dropped.
	template <typename DropFn>
	inline uint32_t giveItemToColonist(
		Inventory& inv, const engine::assets::AssetRegistry& registry, const std::string& defName, uint32_t quantity, DropFn&& onDrop
	) {
		if (quantity == 0) {
			return 0;
		}

		// Two-hand bulk goods live in the hands only; whatever weight/hands can't take drops.
		if (ecs::itemIsTwoHand(registry, defName)) {
			const uint32_t carried	 = ecs::addArmful(inv, registry, defName, quantity);
			const uint32_t remainder = quantity - carried;
			if (remainder > 0) {
				onDrop(defName, remainder);
			}
			return carried;
		}

		// One-hand item: weight is the first gate. Anything over the carry-weight cap drops.
		const uint32_t fits = ecs::cargoUnitsThatFit(inv, registry, defName);
		uint32_t	   toCarry = std::min(quantity, fits);
		uint32_t	   carried = 0;

		// 1. Empty hand(s): one unit per free hand (a hand stack is quantity 1).
		while (carried < toCarry && inv.freeHandCount() > 0) {
			if (!inv.rightHand.has_value()) {
				inv.rightHand = ItemStack{defName, 1};
			} else {
				inv.leftHand = ItemStack{defName, 1};
			}
			++carried;
		}

		// 2. Free belt slots: one one-hand item each.
		while (carried < toCarry && inv.beltHasFreeSlot()) {
			if (!inv.stowToBelt(defName)) {
				break;
			}
			++carried;
		}

		// 3. Backpack: stack/slot limited; addItem reports what it actually seated.
		if (carried < toCarry) {
			carried += inv.addItem(defName, toCarry - carried);
		}

		// 4. Anything that didn't fit (weight cap or no slot anywhere) drops on the ground.
		const uint32_t dropped = quantity - carried;
		if (dropped > 0) {
			onDrop(defName, dropped);
		}
		return carried;
	}

} // namespace ecs
