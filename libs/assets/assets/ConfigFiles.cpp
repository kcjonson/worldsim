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
		for (const auto& entry : std::filesystem::recursive_directory_iterator(folder, error)) {
			if (entry.is_regular_file() && entry.path().extension() == ".xml") {
				found.files.push_back(entry.path());
			}
		}
		if (error) {
			found.files.clear();
			found.error = folder.generic_string() + ": " + error.message();
			return found;
		}
		std::ranges::sort(found.files, {}, [&folder](const std::filesystem::path& file) {
			return std::pair(file.parent_path() != folder, file);
		});
		return found;
	}

} // namespace assets
