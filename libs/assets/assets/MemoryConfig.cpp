#include "assets/MemoryConfig.h"
#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <utility>

namespace assets {

	MemoryConfig& MemoryConfig::Get() {
		static MemoryConfig instance;
		return instance;
	}

	bool MemoryConfig::loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file) {
		clear();

		const std::string	 source = file.generic_string();
		pugi::xml_document	 document;
		const pugi::xml_node root = readRootElement(document, configRoot / file, "Memory", source, problemList);
		if (!root) {
			return false;
		}

		ConfigReader reader(source);
		recentSightingMinutes = reader.attributeNumber(root.child("RecentSighting"), "RecentSighting", "gameMinutes");

		std::vector<std::string> problems = reader.takeProblems();
		if (!problems.empty()) {
			clear();
			problemList = std::move(problems);
			LOG_ERROR(Engine, "Memory config %s has %zu bad value(s)", source.c_str(), problemList.size());
			return false;
		}
		isLoaded = true;
		LOG_INFO(Engine, "Loaded memory config from %s", source.c_str());
		return true;
	}

	void MemoryConfig::clear() {
		isLoaded = false;
		recentSightingMinutes = 0.0F;
		problemList.clear();
	}

} // namespace assets
