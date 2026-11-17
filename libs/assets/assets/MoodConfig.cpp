#include "assets/MoodConfig.h"

#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>

namespace assets {

	MoodConfig& MoodConfig::Get() {
		static MoodConfig instance;
		return instance;
	}

	bool MoodConfig::loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file) {
		clear();

		const std::string	 source = file.generic_string();
		pugi::xml_document	 document;
		const pugi::xml_node root = readRootElement(document, configRoot / file, "Mood", source, problemList);
		if (!root) {
			return false;
		}

		ConfigReader reader(source);

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

		std::vector<std::string> problems = reader.takeProblems();
		if (!problems.empty()) {
			clear();
			problemList = std::move(problems);
			LOG_ERROR(Engine, "Mood config %s has %zu bad value(s)", source.c_str(), problemList.size());
			return false;
		}
		isLoaded = true;
		LOG_INFO(Engine, "Loaded mood config from %s", source.c_str());
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
