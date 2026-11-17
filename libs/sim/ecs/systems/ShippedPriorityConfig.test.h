#pragma once

#include <assets/PriorityConfig.h>

#include <filesystem>

namespace ecs {

	/// Loads the game's priority-tuning.xml into the PriorityConfig singleton, so a test sees the same
	/// arbitration tiers and score weights the game does.
	inline bool loadShippedPriorityConfig() {
		// <root>/libs/sim/ecs/systems/ShippedPriorityConfig.test.h -> <root>/assets/config/work/priority-tuning.xml
		const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path() /
										   "assets" / "config" / "work" / "priority-tuning.xml";
		return assets::PriorityConfig::Get().loadFromFile(path.parent_path(), path.filename());
	}

} // namespace ecs
