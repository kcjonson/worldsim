#pragma once

// Why the give verb put fewer units into an inventory than it was asked for.

#include <string>

namespace ecs {
	struct Inventory;
}

namespace world_sim {

	/// What kept `inventory` from taking more of `material` through `slot` (hands, belt, or anything
	/// else for the pack or a box), read after the give: the other items in the hands by name, a full
	/// belt, or no room.
	std::string giveShortfallReason(const ecs::Inventory& inventory, const std::string& material, const std::string& slot);

} // namespace world_sim
