#pragma once

// The game's config, loaded once per game by the loading scene: the work config, the behavior library and the
// creatures, the human's config, and the colonist rules. See /docs/technical/config-loading.md.

#include <assets/Entries.h>
#include <ecs/ActionEntries.h>
#include <ecs/ColonistRules.h>

#include <filesystem>
#include <string>
#include <vector>

namespace world_sim {

	struct GameConfigLoad {
		ecs::ColonistRules		 rules;
		assets::EntryLibrary	 entries;
		/// The action entries the sim's action factories read, resolved from `entries`.
		ecs::ActionEntries		 actions;
		std::vector<std::string> errors;
	};

	/// Find every creature folder's config files under `creaturesRoot`. When any folder has structural errors, or
	/// the human (`creaturesRoot/Human`) lacks a file of any of the five kinds it reads (one error per kind), those
	/// are the load's errors and no loader runs; otherwise load the work config from `configRoot`, then, when it
	/// loaded clean, the behavior library and the creatures with the factories' action entries resolved from it, and
	/// the colonist rules from `configRoot` and the human's files, with the thought sources' actions resolved once
	/// both have loaded. An empty error list means everything is loaded and valid.
	[[nodiscard]] GameConfigLoad loadGameConfig(const std::filesystem::path& configRoot, const std::filesystem::path& creaturesRoot);

} // namespace world_sim
