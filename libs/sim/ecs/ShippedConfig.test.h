#pragma once

// The game's shipped config for sim tests.

#include "components/Attributes.h"

#include <assets/TraitRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <initializer_list>
#include <string>
#include <utility>

namespace ecs::testing {

	inline std::filesystem::path repoConfig() {
		return std::filesystem::path(SIM_TEST_REPO_DIR) / "assets" / "config";
	}

	/// The shipped traits, loaded fresh for the caller; a load error fails the test.
	inline assets::TraitRegistry shippedTraits() {
		assets::TraitRegistry traits;
		for (const std::string& error : traits.loadFromFolder(repoConfig() / "colonists" / "traits")) {
			ADD_FAILURE() << error;
		}
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

} // namespace ecs::testing
