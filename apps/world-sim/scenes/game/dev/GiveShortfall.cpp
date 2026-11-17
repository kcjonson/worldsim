#include "scenes/game/dev/GiveShortfall.h"

#include <assets/AssetDefinition.h>
#include <ecs/components/Inventory.h>

namespace world_sim {

	std::string giveShortfallReason(const ecs::Inventory& inventory, const std::string& material, const std::string& slot) {
		if (slot == "belt") {
			return "the belt has no free slot";
		}
		if (slot != "hands") {
			return "no room for more " + material;
		}
		std::string others;
		for (const auto* hand : {&inventory.leftHand, &inventory.rightHand}) {
			// A two-hand item is one stack mirrored in both hands: name it once.
			if (hand->has_value() && (*hand)->defName != material && others != (*hand)->defName) {
				others += (others.empty() ? "" : " and ") + (*hand)->defName;
			}
		}
		if (!others.empty()) {
			return "hands occupied by " + others;
		}
		// Empty hands take any one-hand item, so only an armful comes up short here, and only on weight.
		if (!inventory.leftHand.has_value() && !inventory.rightHand.has_value()) {
			return "no " + material + " fits under his carry weight";
		}
		return "the hands hold all the " + material + " they can";
	}

	std::string giveMaterialRefusal(const assets::AssetDefinition* definition, const std::string& material) {
		if (definition == nullptr) {
			return "unknown asset '" + material + "'";
		}
		if (!definition->itemProperties.has_value()) {
			return "'" + material + "' is not an item";
		}
		return {};
	}

} // namespace world_sim
