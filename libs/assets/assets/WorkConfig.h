#pragma once

// Work configuration: action types, task chains, work types, priority tuning, and construction from the
// config folder, and mood and memory from the human's folder, loaded into their registries and
// cross-validated.

#include "assets/CreatureConfigFiles.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// The config name of every task type the AI evaluates, in ecs::TaskType order after None. Each must
	/// have an arbitration tier in priority-tuning.xml's <TaskTiers>.
	inline constexpr std::array<const char*, 8> kTaskTypeNames = {
		"FulfillNeed", "Harvest", "Craft", "Haul", "PlacePackaged", "Build", "Deconstruct", "Wander"
	};

	/// Clears the work-config registries and loads them from configRoot (the folder holding actions/, work/,
	/// and construction/) and the mood and memory config from `human`'s one <Mood> and one <Memory> file, in
	/// dependency order, then cross-validates them. Returns every error found; an empty list means the
	/// configuration is loaded and valid, unless `human` has errors. Those stop the load before mood; they are
	/// returned when no other loader has reported them yet (takeErrors), so whichever of this and
	/// ecs::loadColonistRules runs first reports them and the other adds nothing.
	[[nodiscard]] std::vector<std::string> loadWorkConfig(const std::filesystem::path& configRoot, CreatureConfigFiles& human);

} // namespace assets
