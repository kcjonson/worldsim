// Priority Configuration Implementation
// Handles XML parsing with pugixml for priority tuning values.

#include "assets/PriorityConfig.h"

#include "assets/ConfigNumber.h"
#include "assets/ConfigXml.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace assets {

	namespace {
		constexpr int kInt16Min = std::numeric_limits<int16_t>::min();
		constexpr int kInt16Max = std::numeric_limits<int16_t>::max();

		/// Each rung the decision ranks options on, by its ladder name.
		struct DecisionRungName {
			std::string_view name;
			uint8_t DecisionRungs::* position;
		};
		constexpr std::array<DecisionRungName, 5> kDecisionRungNames = {{
			{"critical_need", &DecisionRungs::criticalNeed},
			{"committed_work", &DecisionRungs::committedWork},
			{"ordinary_need", &DecisionRungs::ordinaryNeed},
			{"opportunistic_work", &DecisionRungs::opportunisticWork},
			{"idle", &DecisionRungs::idle},
		}};
	} // namespace

	PriorityConfig& PriorityConfig::Get() {
		static PriorityConfig instance;
		return instance;
	}

	bool PriorityConfig::loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file) {
		clear();

		const std::string	 source = file.generic_string();
		pugi::xml_document	 document;
		const pugi::xml_node root = readRootElement(document, configRoot / file, "arbitration", source, problemList);
		if (!root) {
			return false;
		}

		ConfigReader			 reader(source);
		std::vector<std::string> structure;
		onlyAttributes(root, {}, "<arbitration>", source, structure);
		for (const pugi::xml_node child : root.children()) {
			const std::string_view element = child.name();
			if (child.type() == pugi::node_element && element != "ladder" && element != "WorkCategoryOrder" && element != "Bonuses") {
				structure.push_back(source + ": <arbitration>: unknown element <" + std::string(element) + ">");
			}
		}
		parseLadder(root, source, structure);
		resolveDecisionRungs(source);

		const pugi::xml_node bonusesNode = root.child("Bonuses");
		parseBonuses(&bonusesNode, reader);

		if (auto orderNode = root.child("WorkCategoryOrder")) {
			parseCategoryOrder(&orderNode, reader);
		}

		std::vector<std::string> problems = std::move(structure);
		std::vector<std::string> numbers = reader.takeProblems();
		problems.insert(problems.end(), numbers.begin(), numbers.end());
		if (!problems.empty()) {
			clear();
			problemList = std::move(problems);
			LOG_ERROR(Engine, "Priority config %s has %zu bad value(s)", source.c_str(), problemList.size());
			return false;
		}

		LOG_INFO(Engine, "Loaded priority config from %s", source.c_str());
		return true;
	}

	void PriorityConfig::clear() {
		rungNames.clear();
		decisionRungPositions = DecisionRungs{};
		decisionRungProblemList.clear();

		distanceFactorConfig = DistanceFactorConfig{};
		inProgressConfig = InProgressBonusConfig{};
		storagePriorityConfig = StoragePriorityConfig{};
		taskAgeConfig = TaskAgeBonusConfig{};
		categoryOrder.clear();
		problemList.clear();
	}

	float PriorityConfig::calculateDistanceFactor(float distance) const {
		if (distance <= 0.0F) {
			return distanceFactorConfig.maxFactor;
		}
		if (distance >= distanceFactorConfig.maxDistance) {
			return 0.0F;
		}
		return distanceFactorConfig.maxFactor * (1.0F - distance / distanceFactorConfig.maxDistance);
	}

	int16_t PriorityConfig::getInProgressBonus() const {
		return inProgressConfig.bonus;
	}

	int16_t PriorityConfig::getStoragePriorityWeight() const {
		return storagePriorityConfig.weight;
	}

	int16_t PriorityConfig::calculateTaskAgeBonus(float taskAge) const {
		const float minutes = taskAge / 60.0F;
		const float bonus = minutes * static_cast<float>(taskAgeConfig.bonusPerMinute);
		return static_cast<int16_t>(std::clamp(bonus, static_cast<float>(kInt16Min), static_cast<float>(taskAgeConfig.maxBonus)));
	}

	void PriorityConfig::parseBonuses(const void* nodePtr, ConfigReader& reader) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		const pugi::xml_node distanceNode = node.child("DistanceFactor");
		distanceFactorConfig.maxDistance = reader.childNumber(distanceNode, "DistanceFactor", "maxDistance");
		distanceFactorConfig.maxFactor = reader.childNumber(distanceNode, "DistanceFactor", "maxFactor");

		inProgressConfig.bonus =
			static_cast<int16_t>(reader.childInteger(node.child("InProgress"), "InProgress", "bonus", kInt16Min, kInt16Max));

		storagePriorityConfig.weight =
			static_cast<int16_t>(reader.childInteger(node.child("StoragePriority"), "StoragePriority", "weight", kInt16Min, kInt16Max));

		const pugi::xml_node ageNode = node.child("TaskAge");
		taskAgeConfig.bonusPerMinute =
			static_cast<int16_t>(reader.childInteger(ageNode, "TaskAge", "bonusPerMinute", kInt16Min, kInt16Max));
		taskAgeConfig.maxBonus = static_cast<int16_t>(reader.childInteger(ageNode, "TaskAge", "maxBonus", kInt16Min, kInt16Max));
	}

	void PriorityConfig::parseLadder(const pugi::xml_node& root, const std::string& source, std::vector<std::string>& problems) {
		const auto ladders = root.children("ladder");
		const auto count = std::distance(ladders.begin(), ladders.end());
		if (count != 1) {
			problems.push_back(source + ": <arbitration> has " + std::to_string(count) + " <ladder> elements; the ladder is declared once");
			return;
		}
		const pugi::xml_node ladder = root.child("ladder");
		onlyAttributes(ladder, {}, "<ladder>", source, problems);
		const std::size_t problemsBefore = problems.size();
		std::size_t		  index = 0;
		for (const pugi::xml_node rung : ladder.children()) {
			if (rung.type() != pugi::node_element) {
				continue;
			}
			if (std::string_view(rung.name()) != "rung") {
				problems.push_back(source + ": <ladder>: unknown element <" + std::string(rung.name()) + ">");
				continue;
			}
			const std::string label = "<ladder> rung " + std::to_string(++index);
			onlyAttributes(rung, {"name"}, label, source, problems);
			const std::string name = rung.attribute("name").value();
			if (name.empty()) {
				problems.push_back(source + ": " + label + " name is missing");
			} else if (std::ranges::find(rungNames, name) != rungNames.end()) {
				problems.push_back(source + ": " + label + " name \"" + name + "\" is given twice");
			} else {
				rungNames.push_back(name);
			}
		}
		// A rung's position is stored in eight bits, with 255 kept for a task nothing has ranked; survival is the rung
		// the loaders keep for threats.
		constexpr std::size_t kMaxRungs = 255;
		if (index == 0) {
			problems.push_back(source + ": <ladder> has no <rung>");
		} else if (index > kMaxRungs) {
			problems.push_back(
				source + ": <ladder> has " + std::to_string(index) + " rungs; it holds at most " + std::to_string(kMaxRungs)
			);
		} else if (problems.size() == problemsBefore && std::ranges::find(rungNames, "survival") == rungNames.end()) {
			problems.push_back(source + ": <ladder> has no rung named \"survival\", which is reserved for threats");
		}
	}

	void PriorityConfig::resolveDecisionRungs(const std::string& source) {
		for (const DecisionRungName& rung : kDecisionRungNames) {
			const auto found = std::ranges::find(rungNames, rung.name);
			if (found == rungNames.end()) {
				decisionRungProblemList.push_back(
					source + ": <ladder> has no rung named \"" + std::string(rung.name) + "\", which the decision ranks options on"
				);
			} else {
				decisionRungPositions.*rung.position = static_cast<uint8_t>(found - rungNames.begin());
			}
		}
		if (!decisionRungProblemList.empty()) {
			return;
		}
		// The decision reads an unranked task, and a decision with nothing selected, as idle, so idle must be the lowest
		// of its rungs, and they keep the order the decision was written for. Other rungs may sit anywhere among them.
		for (std::size_t index = 1; index < kDecisionRungNames.size(); ++index) {
			const DecisionRungName& higher = kDecisionRungNames[index - 1];
			const DecisionRungName& lower = kDecisionRungNames[index];
			if (decisionRungPositions.*higher.position > decisionRungPositions.*lower.position) {
				decisionRungProblemList.push_back(
					source + ": <ladder> has \"" + std::string(lower.name) + "\" above \"" + std::string(higher.name) +
					"\"; the decision ranks options on critical_need, committed_work, ordinary_need, opportunistic_work, and idle, in "
					"that order"
				);
				return;
			}
		}
	}

	void PriorityConfig::parseCategoryOrder(const void* nodePtr, ConfigReader& reader) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		struct CategoryTier {
			std::string name;
			float		tier;
		};
		std::vector<CategoryTier> categories;

		for (pugi::xml_node catNode : node.children("Category")) {
			std::string name = catNode.attribute("name").as_string();
			const float tier = reader.attributeNumber(catNode, "Category", "tier");

			if (!name.empty() && std::isfinite(tier)) {
				categories.push_back({name, tier});
			}
		}

		// Sort by tier
		std::sort(categories.begin(), categories.end(), [](const CategoryTier& a, const CategoryTier& b) { return a.tier < b.tier; });

		categoryOrder.clear();
		for (const auto& cat : categories) {
			categoryOrder.push_back(cat.name);
		}
	}

} // namespace assets
