#pragma once

// Item Properties Lookup
//
// Provides edible/consumable properties for inventory items.
// This is a transitional solution until full item definitions are implemented.
//
// Current items and their sources:
// - "Berry" → Harvested from BerryBush (edible, nutrition 0.3)
// - "Stick" → Harvested from WoodyBush (not edible, crafting material)
// - "Stone" → Picked up from SmallStone (not edible, crafting material)
//
// Future: Replace with proper item definitions parsed from XML (Items/Berry.xml, etc.)

#include "AssetDefinition.h"

#include <optional>
#include <string>
#include <unordered_map>

namespace engine::assets {

	/// Properties of an edible item
	struct EdibleItemInfo {
		float			  nutrition = 0.3F; // 0-1 scale, how much hunger is restored
		CapabilityQuality quality = CapabilityQuality::Normal;
	};

	/// Lookup edible properties for an item by defName.
	/// Returns std::nullopt if item is not edible.
	///
	/// @param itemDefName Item definition name (e.g., "Berry", "Stick")
	/// @return Edible properties if item is edible, nullopt otherwise
	[[nodiscard]] inline std::optional<EdibleItemInfo> getEdibleItemInfo(const std::string& itemDefName) {
		// Known edible items and their properties
		// TODO: Replace with parsed item definitions (Items/*.xml)
		static const std::unordered_map<std::string, EdibleItemInfo> kEdibleItems = {
			{"Berry", {0.3F, CapabilityQuality::Normal}},
			// Future edible items:
			// {"Apple", {0.4F, CapabilityQuality::Good}},
			// {"CookedMeat", {0.6F, CapabilityQuality::Good}},
			// {"RawMeat", {0.3F, CapabilityQuality::Poor}},
		};

		auto iter = kEdibleItems.find(itemDefName);
		if (iter != kEdibleItems.end()) {
			return iter->second;
		}
		return std::nullopt;
	}

	/// Check if an item is edible (convenience wrapper)
	[[nodiscard]] inline bool isItemEdible(const std::string& itemDefName) {
		return getEdibleItemInfo(itemDefName).has_value();
	}

	/// Get all known edible item names (for AI to check inventory)
	[[nodiscard]] inline std::vector<std::string> getEdibleItemNames() {
		// Return list of all edible items
		// TODO: Replace with dynamic lookup from parsed item definitions
		return {"Berry"};
	}

} // namespace engine::assets
