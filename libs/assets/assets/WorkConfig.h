#pragma once

// Work configuration: action types and work types from the config folder's legacy/, the arbitration file
// (the ladder and priority tuning) from arbitration/, construction, and mood and memory from the human's
// folder, loaded into their registries and cross-validated.

#include "assets/CreatureConfigFiles.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// The config name of every task type the AI evaluates, in ecs::TaskType order after None. Each must
	/// have an arbitration tier in arbitration/priority-tuning.xml's <TaskTiers>.
	inline constexpr std::array<const char*, 8> kTaskTypeNames = {
		"FulfillNeed", "Harvest", "Craft", "Haul", "PlacePackaged", "Build", "Deconstruct", "Wander"
	};

	/// Clears the work-config registries and loads them from configRoot (the folder holding legacy/, arbitration/,
	/// and construction/) and the mood and memory config from `human`'s one <Mood> and one <Memory> file, in
	/// dependency order, then cross-validates them. `human` must have no errors and its one <Mood> and one
	/// <Memory> file (asserted): a caller with a broken human folder reports its errors and loads nothing.
	/// Returns every error found; an empty list means the configuration is loaded and valid.
	[[nodiscard]] std::vector<std::string> loadWorkConfig(const std::filesystem::path& configRoot, const CreatureConfigFiles& human);

} // namespace assets
