#include "assets/ConfigFiles.h"

#include <algorithm>
#include <utility>

namespace assets {

	ConfigFiles configFilesIn(const std::filesystem::path& folder) {
		ConfigFiles		found;
		std::error_code error;
		if (!std::filesystem::is_directory(folder, error)) {
			found.error = folder.generic_string() + ": not a folder";
			return found;
		}
		// Files are found under `base`, so a file directly in the folder has `base` as its exact parent.
		std::filesystem::path base = folder.lexically_normal();
		if (!base.has_filename() && base.has_relative_path()) {
			base = base.parent_path();
		}
		for (const auto& entry : std::filesystem::recursive_directory_iterator(base, error)) {
			if (entry.is_regular_file() && entry.path().extension() == ".xml") {
				found.files.push_back(entry.path());
			}
		}
		if (error) {
			found.files.clear();
			found.error = folder.generic_string() + ": " + error.message();
			return found;
		}
		std::ranges::sort(found.files, {}, [&base](const std::filesystem::path& file) { return std::pair(file.parent_path() != base, file); });
		return found;
	}

} // namespace assets
