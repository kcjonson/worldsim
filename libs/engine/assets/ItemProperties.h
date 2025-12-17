#pragma once

// Item Properties Lookup
//
// Provides edible/consumable properties for inventory items by querying
// the AssetRegistry for ItemDefinition data loaded from XML files.
//
// Item XML files are located in: assets/world/items/{ItemName}/{ItemName}.xml
//
// Current items:
// - "Berry" → Harvested from BerryBush (edible, nutrition 0.3)
// - "Stick" → Harvested from WoodyBush (not edible, crafting material)
// - "Stone" → Picked up from SmallStone (not edible, crafting material)

#include "AssetDefinition.h"
#include "AssetRegistry.h"

#include <optional>
#include <string>
#include <vector>

namespace engine::assets {

	/// Properties of an edible item (extracted from ItemDefinition)
	struct EdibleItemInfo {
		float			  nutrition = 0.3F; // 0-1 scale, how much hunger is restored
		CapabilityQuality quality = CapabilityQuality::Normal;
	};

	/// Lookup edible properties for an item by defName.
	/// Queries the AssetRegistry for ItemDefinition data loaded from XML.
	/// Returns std::nullopt if item is not edible or not found.
	///
	/// @param itemDefName Item definition name (e.g., "Berry", "Stick")
	/// @return Edible properties if item is edible, nullopt otherwise
	[[nodiscard]] inline std::optional<EdibleItemInfo> getEdibleItemInfo(const std::string& itemDefName) {
		const auto* itemDef = AssetRegistry::Get().getItemDefinition(itemDefName);
		if (itemDef == nullptr || !itemDef->isEdible()) {
			return std::nullopt;
		}

		EdibleItemInfo info;
		info.nutrition = itemDef->getNutrition();
		info.quality = itemDef->getQuality();
		return info;
	}

	/// Check if an item is edible (convenience wrapper)
	/// Queries the AssetRegistry for ItemDefinition data.
	[[nodiscard]] inline bool isItemEdible(const std::string& itemDefName) {
		const auto* itemDef = AssetRegistry::Get().getItemDefinition(itemDefName);
		return itemDef != nullptr && itemDef->isEdible();
	}

	/// Get all known edible item names (for AI to check inventory)
	/// Returns all items with EdibleCapability from the AssetRegistry.
	[[nodiscard]] inline std::vector<std::string> getEdibleItemNames() {
		return AssetRegistry::Get().getEdibleItemNames();
	}

} // namespace engine::assets
