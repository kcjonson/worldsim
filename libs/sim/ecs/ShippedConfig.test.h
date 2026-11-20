#pragma once

// The game's shipped config for sim tests and benchmarks. A broken shipped config stops the run.

#include "ColonistRules.h"
#include "NeedLevels.h"
#include "components/Needs.h"

#include "components/Attributes.h"

#include <assets/TraitRegistry.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace ecs::testing {

	inline std::filesystem::path repoConfig() {
		// <root>/libs/sim/ecs/ShippedConfig.test.h -> <root>/assets/config. Normalized first: included as
		// "../ShippedConfig.test.h", __FILE__ can keep the "..".
		return std::filesystem::path(__FILE__).lexically_normal().parent_path().parent_path().parent_path().parent_path() / "assets" /
			   "config";
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

	/// Every attribute at `level` but the `overrides`, for tests that set only the ones they read. The
	/// component has no values of its own; a colonist's come from a roll.
	inline Attributes attributesAt(float level, std::initializer_list<std::pair<float Attributes::*, float>> overrides = {}) {
		Attributes attributes{};
		for (const AttributeField& field : kAttributeFields) {
			attributes.*(field.value) = level;
		}
		for (const auto& [member, value] : overrides) {
			attributes.*member = value;
		}
		return attributes;
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
