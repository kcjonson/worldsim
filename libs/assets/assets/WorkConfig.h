#pragma once

// Work configuration: action types, task chains, work types, priority tuning, construction, mood, and traits,
// loaded from one config folder into their registries and cross-validated.

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

	/// Clears the work-config registries and loads them from configRoot (the folder holding
	/// actions/, work/, construction/, and colonists/), in dependency order, then cross-validates them.
	/// Returns every error found; an empty list means the configuration is loaded and valid.
	[[nodiscard]] std::vector<std::string> loadWorkConfig(const std::filesystem::path& configRoot);

} // namespace assets
