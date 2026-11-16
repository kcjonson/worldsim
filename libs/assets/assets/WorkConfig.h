#pragma once

// Work configuration: action types, task chains, work types, priority tuning, and construction,
// loaded from one config folder into their registries and cross-validated.

#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// Clears the work-config registries and loads them from configRoot (the folder holding
	/// actions/, work/, and construction/), in dependency order, then cross-validates them.
	/// Returns every error found; an empty list means the configuration is loaded and valid.
	[[nodiscard]] std::vector<std::string> loadWorkConfig(const std::filesystem::path& configRoot);

} // namespace assets
