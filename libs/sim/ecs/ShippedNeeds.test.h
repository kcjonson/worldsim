#pragma once

#include "ColonistRules.h"
#include "NeedLevels.h"
#include "components/Needs.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace ecs {

	/// The game's need levels from assets/config/colonists/needs, loaded once, so a test's colonists seek
	/// at the thresholds the game's do. A broken shipped config stops the run.
	inline const NeedLevels& shippedNeedLevels() {
		static const NeedLevels levels = [] {
			// <root>/libs/sim/ecs/ShippedNeeds.test.h -> <root>/assets/config/colonists/needs
			const std::filesystem::path folder = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() /
												 "assets" / "config" / "colonists" / "needs";
			NeedLevels					   loaded;
			const std::vector<std::string> errors = loadNeedLevels(loaded, folder);
			for (const std::string& error : errors) {
				std::fprintf(stderr, "shipped need levels: %s\n", error.c_str());
			}
			if (!errors.empty()) {
				std::abort();
			}
			return loaded;
		}();
		return levels;
	}

	/// A colonist's needs as the game spawns them.
	inline NeedsComponent shippedNeeds() {
		return NeedsComponent(shippedNeedLevels());
	}

} // namespace ecs
