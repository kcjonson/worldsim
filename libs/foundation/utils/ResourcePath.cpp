// Resource Path Utilities implementation

#include "utils/ResourcePath.h"
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace Foundation {

	std::filesystem::path getExecutableDir() {
#ifdef __APPLE__
		uint32_t bufsize = 0;
		_NSGetExecutablePath(nullptr, &bufsize);
		std::vector<char> buf(bufsize);
		if (_NSGetExecutablePath(buf.data(), &bufsize) == 0) {
			return std::filesystem::path(buf.data()).parent_path();
		}
#endif
		// TODO: Add Linux/Windows implementations when needed
		// Linux: readlink("/proc/self/exe", ...)
		// Windows: GetModuleFileName(NULL, ...)
		return {};
	}

	std::optional<std::filesystem::path> findResource(const std::filesystem::path& relativePath) {
		std::vector<std::filesystem::path> searchPaths;

		// Most reliable: relative to executable directory (works regardless of cwd)
		auto exeDir = getExecutableDir();
		if (!exeDir.empty()) {
			searchPaths.push_back(exeDir);
		}

		// Try to get current path - may fail if cwd was deleted (common in IDE terminals)
		try {
			auto cwd = std::filesystem::current_path();
			searchPaths.push_back(cwd);
			searchPaths.push_back(cwd / "build/apps/ui-sandbox"); // From project root
		} catch (const std::filesystem::filesystem_error& /*e*/) {
			// Current directory doesn't exist - skip cwd-relative paths
		}

		// Search each path
		for (const auto& searchPath : searchPaths) {
			auto fullPath = searchPath / relativePath;
			if (std::filesystem::exists(fullPath)) {
				return fullPath;
			}
		}

		return std::nullopt;
	}

	std::string findResourceString(const std::filesystem::path& relativePath) {
		auto result = findResource(relativePath);
		return result ? result->string() : "";
	}

} // namespace Foundation
