#pragma once

// Item Properties Lookup
//
// Provides edible/consumable properties for inventory items by querying
// the AssetRegistry for AssetDefinition.itemProperties data.
//
// Unified model: entities can exist "in world" (visible) or "in inventory" (stored).
// Item properties come from the <item> section in entity XML definitions.
//
// Entity XMLs with item properties:
// - "Berry" → assets/world/misc/Berry/Berry.xml (edible, nutrition 0.3)
// - "Stick" → assets/world/misc/Stick/Stick.xml (not edible, crafting material)
// - "SmallStone" → assets/world/misc/SmallStone/SmallStone.xml (not edible, crafting material)

#include "AssetDefinition.h"
#include "AssetRegistry.h"

#include <optional>
#include <string>
#include <vector>

namespace engine::assets {

	/// Properties of an edible item (extracted from AssetDefinition.itemProperties)
	struct EdibleItemInfo {
		float			  nutrition = 0.3F; // 0-1 scale, how much hunger is restored
		CapabilityQuality quality = CapabilityQuality::Normal;
	};

	/// Lookup edible properties for an item by defName.
	/// Queries the AssetRegistry for AssetDefinition.itemProperties data.
	/// Returns std::nullopt if item is not edible or not found.
	///
	/// @param itemDefName Entity definition name (e.g., "Berry", "Stick")
	/// @return Edible properties if item is edible, nullopt otherwise
	[[nodiscard]] inline std::optional<EdibleItemInfo> getEdibleItemInfo(const std::string& itemDefName) {
		const auto* def = AssetRegistry::Get().getDefinition(itemDefName);
		if (def == nullptr || !def->isEdible()) {
			return std::nullopt;
		}

		EdibleItemInfo info;
		info.nutrition = def->itemProperties->getNutrition();
		info.quality = def->itemProperties->getQuality();
		return info;
	}

	/// Check if an item is edible (convenience wrapper)
	/// Queries the AssetRegistry for AssetDefinition.itemProperties data.
	[[nodiscard]] inline bool isItemEdible(const std::string& itemDefName) {
		const auto* def = AssetRegistry::Get().getDefinition(itemDefName);
		return def != nullptr && def->isEdible();
	}

	/// Get all known edible item names (for AI to check inventory)
	/// Returns all entities with edible itemProperties from the AssetRegistry.
	[[nodiscard]] inline std::vector<std::string> getEdibleItemNames() {
		std::vector<std::string> names;
		for (const auto& defName : AssetRegistry::Get().getDefinitionNames()) {
			const auto* def = AssetRegistry::Get().getDefinition(defName);
			if (def != nullptr && def->isEdible()) {
				names.push_back(defName);
			}
		}
		return names;
	}

} // namespace engine::assets
