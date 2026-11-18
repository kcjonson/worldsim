#include "assets/MoodConfig.h"

#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <set>
#include <string_view>
#include <utility>

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

		std::vector<std::string> elementProblems;
		for (const pugi::xml_node node : root.children()) {
			if (node.type() != pugi::node_element) {
				continue;
			}
			const std::string_view element = node.name();
			if (element != "Easing" && element != "NeedPenalty" && element != "NeedCosts" && element != "Bands") {
				elementProblems.push_back(source + ": unknown element <" + std::string(element) + ">");
			}
		}

		easingPointsPerGameHour = reader.attributeNumber(root.child("Easing"), "Easing", "pointsPerGameHour");

		const pugi::xml_node penaltyNode = root.child("NeedPenalty");
		penaltyCurve = {
			.comfortable = reader.attributeNumber(penaltyNode, "NeedPenalty", "comfortable"),
			.warning = reader.attributeNumber(penaltyNode, "NeedPenalty", "warning"),
			.penaltyAtWarning = reader.attributeNumber(penaltyNode, "NeedPenalty", "penaltyAtWarning"),
		};

		std::vector<std::string> needProblems;
		std::set<std::string>	 listed;
		for (const pugi::xml_node needNode : root.child("NeedCosts").children("Need")) {
			const std::string name = needNode.attribute("name").as_string();
			if (name.empty()) {
				needProblems.push_back(source + ": a <Need> in <NeedCosts> has no name");
				continue;
			}
			const std::string what = ConfigReader::describe(needNode, "Need");
			if (!listed.insert(name).second) {
				needProblems.push_back(source + ": " + what + " is given twice");
			}
			onlyAttributes(needNode, {"name", "cost"}, what, source, needProblems);
			const float cost = reader.attributeNumber(needNode, "Need", "cost");
			if (cost < 0.0F) {
				reader.refuseAttribute(what, needNode, "cost", "zero or more");
			}
			costs.push_back({.need = name, .cost = cost});
		}

		for (const pugi::xml_node bandNode : root.child("Bands").children("Band")) {
			bandList.push_back({.name = bandNode.attribute("name").as_string(), .from = reader.attributeNumber(bandNode, "Band", "from")});
		}
		std::stable_sort(bandList.begin(), bandList.end(), [](const MoodBand& left, const MoodBand& right) {
			return left.from > right.from;
		});

		std::vector<std::string> problems = std::move(elementProblems);
		const std::vector<std::string> numberProblems = reader.takeProblems();
		problems.insert(problems.end(), numberProblems.begin(), numberProblems.end());
		problems.insert(problems.end(), needProblems.begin(), needProblems.end());
		if (!problems.empty()) {
			clear();
			problemList = std::move(problems);
			LOG_ERROR(Engine, "Mood config %s has %zu problem(s)", source.c_str(), problemList.size());
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
		costs.clear();
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
