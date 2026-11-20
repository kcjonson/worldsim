#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace assets {

	struct ConfigFiles {
		std::vector<std::filesystem::path> files;
		/// Set, with no files, when the folder is missing or cannot be read.
		std::optional<std::string> error;
	};

	/// Every .xml file under `folder` in the order a config that later files override loads them: the
	/// folder's own files first, in path order (the base set), then its subfolders' files, in path order.
	[[nodiscard]] ConfigFiles configFilesIn(const std::filesystem::path& folder);

	/// How a config error names `file`: its path relative to the folder the load was given, with forward slashes
	/// on every platform (`tools/AxePrimitive.xml` under the recipes folder).
	[[nodiscard]] std::string configFileName(const std::filesystem::path& file, const std::filesystem::path& folder);

} // namespace assets
