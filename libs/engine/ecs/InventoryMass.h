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
		for (const auto& [defName, quantity] : inv.items) {
			addStack(defName, quantity);
		}
		return total;
	}

	/// How many more units of `defName` fit before hitting the cargo weight cap.
	[[nodiscard]] inline uint32_t cargoUnitsThatFit(const Inventory& inv, const engine::assets::AssetRegistry& registry, const std::string& defName) {
		const float remaining = inv.carryCapacityKg - carriedCargoMassKg(inv, registry);
		return massUnitsThatFit(remaining, registry.getItemMassKg(defName), inv.maxStackSize);
	}

	/// Does the inventory hold an item that provides `toolType` (e.g. "Axe")? Checks both
	/// hands and the backpack; a held-or-stowed tool counts. Empty toolType always passes
	/// (the work needs no tool).
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
		for (const auto& [defName, quantity] : inv.items) {
			if (quantity > 0 && matches(defName)) {
				return true;
			}
		}
		return false;
	}

	/// How many units of `defName` an empty colonist with `carryCapacityKg` carries in one trip.
	[[nodiscard]] inline uint32_t cargoUnitsPerTrip(const engine::assets::AssetRegistry& registry, const std::string& defName, float carryCapacityKg) {
		return massUnitsPerTrip(carryCapacityKg, registry.getItemMassKg(defName));
	}

} // namespace ecs
