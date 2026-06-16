// Resource Path Utilities implementation

#include "utils/ResourcePath.h"
#include <vector>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
		return {};
#elif defined(_WIN32)
		std::vector<wchar_t> buf(MAX_PATH);
		for (;;) {
			const DWORD len = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
			if (len == 0) {
				return {};
			}
			if (len < buf.size()) {
				return std::filesystem::path(std::wstring(buf.data(), len)).parent_path();
			}
			buf.resize(buf.size() * 2); // path was truncated; grow and retry
		}
#else
		// TODO: Linux implementation: readlink("/proc/self/exe", ...)
		return {};
#endif
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
