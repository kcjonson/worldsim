#pragma once

// The game's shipped config for sim tests.

#include <assets/TraitRegistry.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

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

} // namespace ecs::testing
