#pragma once

// The game's config, loaded once per game by the loading scene: the work config, the human's config, and the
// colonist rules. See /docs/technical/config-loading.md.

#include <ecs/ColonistRules.h>

#include <filesystem>
#include <string>
#include <vector>

namespace world_sim {

	struct GameConfigLoad {
		ecs::ColonistRules		 rules;
		std::vector<std::string> errors;
	};

	/// Find the human's config files in `humanFolder`. When the folder has errors they are the load's errors
	/// and neither loader runs; otherwise load the work config and the colonist rules from `configRoot` and
	/// the human's files. An empty error list means everything is loaded and valid.
	[[nodiscard]] GameConfigLoad loadGameConfig(const std::filesystem::path& configRoot, const std::filesystem::path& humanFolder);

} // namespace world_sim
