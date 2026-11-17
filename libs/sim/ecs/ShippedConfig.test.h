#pragma once

// The game's shipped config for sim tests and benchmarks. A broken shipped config stops the run.

#include "ColonistRules.h"
#include "NeedLevels.h"
#include "components/Needs.h"

#include <assets/TraitRegistry.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace ecs::testing {

	inline std::filesystem::path repoConfig() {
		// <root>/libs/sim/ecs/ShippedConfig.test.h -> <root>/assets/config
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config";
	}

	inline void stopOnErrors(const char* what, const std::vector<std::string>& errors) {
		for (const std::string& error : errors) {
			std::fprintf(stderr, "shipped %s: %s\n", what, error.c_str());
		}
		if (!errors.empty()) {
			std::abort();
		}
	}

	/// The shipped traits, loaded fresh for the caller.
	inline assets::TraitRegistry shippedTraits() {
		assets::TraitRegistry traits;
		stopOnErrors("traits", traits.loadFromFolder(repoConfig() / "colonists" / "traits"));
		return traits;
	}

	/// The shipped need levels, loaded once, so a test's colonists seek at the thresholds the game's do.
	inline const NeedLevels& shippedNeedLevels() {
		static const NeedLevels levels = [] {
			NeedLevels loaded;
			stopOnErrors("need levels", loadNeedLevels(loaded, repoConfig() / "colonists" / "needs"));
			return loaded;
		}();
		return levels;
	}

	/// A colonist's needs as the game spawns them.
	inline NeedsComponent shippedNeeds() {
		return NeedsComponent(shippedNeedLevels());
	}

} // namespace ecs::testing
