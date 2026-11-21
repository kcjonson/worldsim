#pragma once

// The asset definition names a folder of definition files declares, read without loading the definitions.

#include <filesystem>
#include <string>
#include <vector>

namespace assets {

	/// The `<defName>` of every `<AssetDef>` in every `.xml` file under `folder`, in the order found, each once. A file
	/// that is not readable XML adds nothing.
	[[nodiscard]] std::vector<std::string> definitionNamesIn(const std::filesystem::path& folder);

} // namespace assets
