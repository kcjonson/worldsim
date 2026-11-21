#pragma once

#include <assets/PriorityConfig.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string_view>

namespace ecs {

	/// Loads the game's arbitration/priority-tuning.xml into the PriorityConfig singleton, so a test sees the same
	/// ladder and score weights the game does.
	inline bool loadShippedPriorityConfig() {
		// <root>/libs/sim/ecs/systems/ShippedPriorityConfig.test.h -> <root>/assets/config/arbitration/priority-tuning.xml
		const std::filesystem::path path =
			std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path() / "assets" / "config" /
			"arbitration" / "priority-tuning.xml";
		return assets::PriorityConfig::Get().loadFromFile(path.parent_path(), path.filename());
	}

	/// The tier of the loaded ladder's rung `name`: its position, the handle a task, an option, and a claim holder
	/// store. Stops the run naming the rung when the ladder lacks it, so nothing ranks by a rung that isn't there.
	inline uint8_t rungNamed(std::string_view name) {
		const auto& rungs = assets::PriorityConfig::Get().rungs();
		const auto	found = std::ranges::find(rungs, name);
		if (found == rungs.end()) {
			std::fprintf(stderr, "the loaded ladder has no rung named \"%.*s\"\n", static_cast<int>(name.size()), name.data());
			std::abort();
		}
		return static_cast<uint8_t>(found - rungs.begin());
	}

} // namespace ecs
