// Priority Configuration Implementation
// Handles XML parsing with pugixml for priority tuning values.

#include "assets/PriorityConfig.h"

#include "assets/ConfigNumber.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace assets {

	PriorityConfig& PriorityConfig::Get() {
		static PriorityConfig instance;
		return instance;
	}

	bool PriorityConfig::loadFromFile(const std::string& xmlPath) {
		pugi::xml_document	   doc;
		pugi::xml_parse_result result = doc.load_file(xmlPath.c_str());

		if (!result) {
			LOG_ERROR(Engine, "Failed to load priority config XML: %s - %s", xmlPath.c_str(), result.description());
			return false;
		}

		pugi::xml_node root = doc.child("PriorityTuning");
		if (!root) {
			LOG_ERROR(Engine, "No PriorityTuning root element in: %s", xmlPath.c_str());
			return false;
		}

		clear();

		ConfigReader reader(xmlPath);

		if (auto tiersNode = root.child("TaskTiers")) {
			parseTaskTiers(&tiersNode, reader);
		}

		const pugi::xml_node bonusesNode = root.child("Bonuses");
		parseBonuses(&bonusesNode, reader);

		if (auto orderNode = root.child("WorkCategoryOrder")) {
			parseCategoryOrder(&orderNode, reader);
		}

		problemList = reader.takeProblems();

		LOG_INFO(Engine, "Loaded priority config from %s", xmlPath.c_str());
		return true;
	}

	void PriorityConfig::clear() {
		++m_generation;
		taskTiers.clear();

		distanceFactorConfig = DistanceFactorConfig{};
		inProgressConfig = InProgressBonusConfig{};
		storagePriorityConfig = StoragePriorityConfig{};
		taskAgeConfig = TaskAgeBonusConfig{};
		categoryOrder.clear();
		problemList.clear();
	}

	int PriorityConfig::getTaskTier(const std::string& taskTypeName) const {
		auto it = taskTiers.find(taskTypeName);
		if (it != taskTiers.end()) {
			return it->second;
		}
		return kUnassignedTier;
	}

	bool PriorityConfig::validateTaskTiers(const std::vector<std::string>& requiredTypeNames, std::vector<std::string>& missingOut) const {
		missingOut.clear();
		for (const auto& name : requiredTypeNames) {
			auto it = taskTiers.find(name);
			if (it == taskTiers.end()) {
				missingOut.push_back(name);
			}
		}
		return missingOut.empty();
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
		float	minutes = taskAge / 60.0F;
		int16_t bonus = static_cast<int16_t>(minutes * static_cast<float>(taskAgeConfig.bonusPerMinute));
		return std::min(bonus, taskAgeConfig.maxBonus);
	}

	void PriorityConfig::parseTaskTiers(const void* nodePtr, ConfigReader& reader) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		for (pugi::xml_node tierNode : node.children("Task")) {
			std::string name = tierNode.attribute("name").as_string();
			if (name.empty()) {
				continue;
			}
			const int tier = reader.attributeInteger(tierNode, "Task", "tier");
			taskTiers[name] = tier;
			LOG_DEBUG(Engine, "Task tier: %s = %d", name.c_str(), tier);
		}
	}

	void PriorityConfig::parseBonuses(const void* nodePtr, ConfigReader& reader) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		const pugi::xml_node distanceNode = node.child("DistanceFactor");
		distanceFactorConfig.maxDistance = reader.childNumber(distanceNode, "DistanceFactor", "maxDistance");
		distanceFactorConfig.maxFactor = reader.childNumber(distanceNode, "DistanceFactor", "maxFactor");

		inProgressConfig.bonus = static_cast<int16_t>(reader.childInteger(node.child("InProgress"), "InProgress", "bonus"));

		storagePriorityConfig.weight = static_cast<int16_t>(reader.childInteger(node.child("StoragePriority"), "StoragePriority", "weight"));

		const pugi::xml_node ageNode = node.child("TaskAge");
		taskAgeConfig.bonusPerMinute = static_cast<int16_t>(reader.childInteger(ageNode, "TaskAge", "bonusPerMinute"));
		taskAgeConfig.maxBonus = static_cast<int16_t>(reader.childInteger(ageNode, "TaskAge", "maxBonus"));
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
