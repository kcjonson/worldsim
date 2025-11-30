#pragma once

// Resource Path Utilities
// Resolves relative resource paths (fonts, shaders, etc.) to absolute paths
// by searching multiple locations including the executable's directory.
// This handles cases where the current working directory is invalid or
// doesn't contain the resources (common when launching from IDEs).

#include <filesystem>
#include <optional>
#include <string>

namespace Foundation {

	// Get the directory containing the currently running executable
	// Returns empty path if it cannot be determined
	std::filesystem::path getExecutableDir();

	// Find a resource file by searching multiple locations:
	// 1. Relative to executable directory
	// 2. Relative to current working directory (if valid)
	// 3. Relative to cwd/build/apps/ui-sandbox (for running from project root)
	//
	// Returns the full absolute path if found, or std::nullopt if not found
	std::optional<std::filesystem::path> findResource(const std::filesystem::path& relativePath);

	// Convenience: find resource and return string path, or empty string if not found
	std::string findResourceString(const std::filesystem::path& relativePath);

} // namespace Foundation
