#include "assets-testing/DefinitionNames.h"

#include <pugixml.hpp>

#include <algorithm>

namespace assets {

	std::vector<std::string> definitionNamesIn(const std::filesystem::path& folder) {
		std::vector<std::filesystem::path> files;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(folder)) {
			if (entry.is_regular_file() && entry.path().extension() == ".xml") {
				files.push_back(entry.path());
			}
		}
		std::ranges::sort(files);
		std::vector<std::string> names;
		for (const std::filesystem::path& file : files) {
			pugi::xml_document document;
			if (!document.load_file(file.string().c_str())) {
				continue;
			}
			for (const pugi::xml_node definition : document.child("AssetDefinitions").children("AssetDef")) {
				std::string name = definition.child_value("defName");
				if (std::ranges::find(names, name) == names.end()) {
					names.push_back(std::move(name));
				}
			}
		}
		return names;
	}

} // namespace assets
