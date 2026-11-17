#include "assets/MoodConfig.h"

#include "assets/ConfigNumber.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>

namespace assets {

	MoodConfig& MoodConfig::Get() {
		static MoodConfig instance;
		return instance;
	}

	bool MoodConfig::loadFromFile(const std::string& xmlPath) {
		clear();

		pugi::xml_document		 document;
		const pugi::xml_parse_result result = document.load_file(xmlPath.c_str());
		if (!result) {
			LOG_ERROR(Engine, "Failed to load mood config XML: %s - %s", xmlPath.c_str(), result.description());
			return false;
		}
		const pugi::xml_node root = document.child("Mood");
		if (!root) {
			LOG_ERROR(Engine, "No Mood root element in: %s", xmlPath.c_str());
			return false;
		}

		ConfigReader reader(xmlPath);

		easingPointsPerGameHour = reader.attributeNumber(root.child("Easing"), "Easing", "pointsPerGameHour");

		const pugi::xml_node penaltyNode = root.child("NeedPenalty");
		penaltyCurve = {
			.comfortable = reader.attributeNumber(penaltyNode, "NeedPenalty", "comfortable"),
			.warning = reader.attributeNumber(penaltyNode, "NeedPenalty", "warning"),
			.penaltyAtWarning = reader.attributeNumber(penaltyNode, "NeedPenalty", "penaltyAtWarning"),
		};

		for (const pugi::xml_node needNode : root.child("NeedWeights").children("Need")) {
			weights.push_back({.need = needNode.attribute("name").as_string(), .weight = reader.attributeNumber(needNode, "Need", "weight")});
		}

		for (const pugi::xml_node bandNode : root.child("Bands").children("Band")) {
			bandList.push_back({.name = bandNode.attribute("name").as_string(), .from = reader.attributeNumber(bandNode, "Band", "from")});
		}
		std::stable_sort(bandList.begin(), bandList.end(), [](const MoodBand& left, const MoodBand& right) { return left.from > right.from; });

		problemList = reader.takeProblems();
		isLoaded = true;
		LOG_INFO(Engine, "Loaded mood config from %s", xmlPath.c_str());
		return true;
	}

	void MoodConfig::clear() {
		++loadGeneration;
		isLoaded = false;
		easingPointsPerGameHour = 0.0F;
		penaltyCurve = {};
		weights.clear();
		bandList.clear();
		problemList.clear();
	}

	const MoodBand& MoodConfig::bandFor(float mood) const {
		for (const MoodBand& band : bandList) {
			if (mood >= band.from) {
				return band;
			}
		}
		return bandList.back();
	}

} // namespace assets
