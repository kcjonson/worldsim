#include "assets/MemoryConfig.h"
#include "assets/ConfigNumber.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <limits>

namespace assets {

	MemoryConfig& MemoryConfig::Get() {
		static MemoryConfig instance;
		return instance;
	}

	bool MemoryConfig::loadFromFile(const std::string& xmlPath) {
		clear();

		pugi::xml_document			 document;
		const pugi::xml_parse_result result = document.load_file(xmlPath.c_str());
		if (!result) {
			LOG_ERROR(Engine, "Failed to load memory config XML: %s - %s", xmlPath.c_str(), result.description());
			return false;
		}
		const pugi::xml_node root = document.child("Memory");
		if (!root) {
			LOG_ERROR(Engine, "No Memory root element in: %s", xmlPath.c_str());
			return false;
		}

		// Missing or not entirely a number reads as NaN, which validation reports, rather than as a silent
		// zero or the number it starts with.
		recentSightingMinutes = parseConfigNumber(root.child("RecentSighting").attribute("gameMinutes").as_string())
									.value_or(std::numeric_limits<float>::quiet_NaN());

		isLoaded = true;
		LOG_INFO(Engine, "Loaded memory config from %s", xmlPath.c_str());
		return true;
	}

	void MemoryConfig::clear() {
		isLoaded = false;
		recentSightingMinutes = 0.0F;
	}

} // namespace assets
