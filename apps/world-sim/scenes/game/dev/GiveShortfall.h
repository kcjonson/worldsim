#pragma once

// Why the give verb gives nothing of a material, or put fewer units into an inventory than it was asked for.

#include <string>

namespace assets {
	struct AssetDefinition;
}
namespace ecs {
	struct Inventory;
}

namespace world_sim {

	/// What kept `inventory` from taking more of `material` through `slot` (hands, belt, or anything
	/// else for the pack or a box), read after the give: the other items in the hands by name, an
	/// armful his carry weight leaves no room for, a full belt, or no room.
	std::string giveShortfallReason(const ecs::Inventory& inventory, const std::string& material, const std::string& slot);

	/// Why `material` is given nowhere: empty for an item, else that its registry `definition` is
	/// missing (an unknown name, which an inventory would take as it takes any string) or is not an
	/// item (furniture, a station).
	std::string giveMaterialRefusal(const assets::AssetDefinition* definition, const std::string& material);

} // namespace world_sim
