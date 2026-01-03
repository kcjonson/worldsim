// Priority Configuration Implementation
// Handles XML parsing with pugixml for priority tuning values.

#include "assets/PriorityConfig.h"

#include <utils/Log.h>

#include <pugixml.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine::assets {

	PriorityConfig& PriorityConfig::Get() {
		static PriorityConfig instance;
		return instance;
	}

	PriorityConfig::PriorityConfig() {
		// Initialize default bands
		bands["Critical"] = 30000;
		bands["PlayerDraft"] = 20000;
		bands["Needs"] = 10000;
		bands["WorkHigh"] = 5000;
		bands["WorkMedium"] = 3000;
		bands["WorkLow"] = 1000;
		bands["Idle"] = 0;
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

		// Parse sections
		if (auto bandsNode = root.child("Bands")) {
			parseBands(&bandsNode);
		}

		if (auto userPriorityNode = root.child("UserPriorityMapping")) {
			if (auto stepNode = userPriorityNode.child("stepSize")) {
				userPriorityStep = static_cast<int16_t>(stepNode.text().as_int(100));
			}
		}

		if (auto bonusesNode = root.child("Bonuses")) {
			parseBonuses(&bonusesNode);
		}

		if (auto thresholdsNode = root.child("Thresholds")) {
			parseThresholds(&thresholdsNode);
		}

		if (auto haulingNode = root.child("HaulingTuning")) {
			parseHaulingTuning(&haulingNode);
		}

		if (auto orderNode = root.child("WorkCategoryOrder")) {
			parseCategoryOrder(&orderNode);
		}

		LOG_INFO(Engine, "Loaded priority config from %s", xmlPath.c_str());
		return true;
	}

	void PriorityConfig::clear() {
		bands.clear();
		bands["Critical"] = 30000;
		bands["PlayerDraft"] = 20000;
		bands["Needs"] = 10000;
		bands["WorkHigh"] = 5000;
		bands["WorkMedium"] = 3000;
		bands["WorkLow"] = 1000;
		bands["Idle"] = 0;

		userPriorityStep = 100;
		distanceConfig = DistanceBonusConfig{};
		skillConfig = SkillBonusConfig{};
		chainConfig = ChainBonusConfig{};
		inProgressConfig = InProgressBonusConfig{};
		taskAgeConfig = TaskAgeBonusConfig{};
		haulingConfig = HaulingTuningConfig{};
		timingConfig = TimingConfig{};
		categoryOrder.clear();
		categoryTiers.clear();
	}

	int16_t PriorityConfig::getBandBase(const std::string& bandName) const {
		auto it = bands.find(bandName);
		if (it != bands.end()) {
			return it->second;
		}
		return 0;
	}

	int16_t PriorityConfig::userPriorityToBase(uint8_t userPriority) const {
		// User priority 1-3 → WorkHigh, 4-6 → WorkMedium, 7-9 → WorkLow
		// Within each band, higher priority = higher value
		if (userPriority < 1)
			userPriority = 1;
		if (userPriority > 9)
			userPriority = 9;

		int16_t bandBase;
		int16_t offset;

		if (userPriority <= 3) {
			bandBase = getBandBase("WorkHigh");
			offset = static_cast<int16_t>((4 - userPriority) * userPriorityStep);
		} else if (userPriority <= 6) {
			bandBase = getBandBase("WorkMedium");
			offset = static_cast<int16_t>((7 - userPriority) * userPriorityStep);
		} else {
			bandBase = getBandBase("WorkLow");
			offset = static_cast<int16_t>((10 - userPriority) * userPriorityStep);
		}

		return bandBase + offset;
	}

	int16_t PriorityConfig::calculateDistanceBonus(float distance) const {
		if (distance <= distanceConfig.optimalDistance) {
			return distanceConfig.maxBonus;
		}

		if (distance >= distanceConfig.maxPenaltyDistance) {
			return -distanceConfig.maxPenalty;
		}

		// Linear interpolation
		float range = distanceConfig.maxPenaltyDistance - distanceConfig.optimalDistance;
		float normalized = (distance - distanceConfig.optimalDistance) / range;
		float bonus =
			static_cast<float>(distanceConfig.maxBonus) - normalized * static_cast<float>(distanceConfig.maxBonus + distanceConfig.maxPenalty);

		return static_cast<int16_t>(std::round(bonus));
	}

	int16_t PriorityConfig::calculateSkillBonus(float skillLevel) const {
		int16_t bonus = static_cast<int16_t>(skillLevel * static_cast<float>(skillConfig.multiplier));
		return std::min(bonus, skillConfig.maxBonus);
	}

	int16_t PriorityConfig::getChainBonus() const {
		return chainConfig.bonus;
	}

	int16_t PriorityConfig::getInProgressBonus() const {
		return inProgressConfig.bonus;
	}

	int16_t PriorityConfig::calculateTaskAgeBonus(float taskAge) const {
		float	minutes = taskAge / 60.0F;
		int16_t bonus = static_cast<int16_t>(minutes * static_cast<float>(taskAgeConfig.bonusPerMinute));
		return std::min(bonus, taskAgeConfig.maxBonus);
	}

	float PriorityConfig::getCategoryTier(const std::string& categoryName) const {
		auto it = categoryTiers.find(categoryName);
		if (it != categoryTiers.end()) {
			return it->second;
		}
		return 999.0F; // Unknown categories go last
	}

	void PriorityConfig::parseBands(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		for (pugi::xml_node bandNode : node.children("Band")) {
			std::string name = bandNode.attribute("name").as_string();
			int16_t		base = static_cast<int16_t>(bandNode.attribute("base").as_int(0));

			if (!name.empty()) {
				bands[name] = base;
				LOG_DEBUG(Engine, "Priority band: %s = %d", name.c_str(), base);
			}
		}
	}

	void PriorityConfig::parseBonuses(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		// Distance
		if (auto distNode = node.child("Distance")) {
			if (auto n = distNode.child("optimalDistance")) {
				distanceConfig.optimalDistance = n.text().as_float(5.0F);
			}
			if (auto n = distNode.child("maxPenaltyDistance")) {
				distanceConfig.maxPenaltyDistance = n.text().as_float(50.0F);
			}
			if (auto n = distNode.child("maxBonus")) {
				distanceConfig.maxBonus = static_cast<int16_t>(n.text().as_int(50));
			}
			if (auto n = distNode.child("maxPenalty")) {
				distanceConfig.maxPenalty = static_cast<int16_t>(n.text().as_int(50));
			}
		}

		// Skill
		if (auto skillNode = node.child("Skill")) {
			if (auto n = skillNode.child("multiplier")) {
				skillConfig.multiplier = static_cast<int16_t>(n.text().as_int(10));
			}
			if (auto n = skillNode.child("maxBonus")) {
				skillConfig.maxBonus = static_cast<int16_t>(n.text().as_int(100));
			}
		}

		// Chain continuation
		if (auto chainNode = node.child("ChainContinuation")) {
			if (auto n = chainNode.child("bonus")) {
				chainConfig.bonus = static_cast<int16_t>(n.text().as_int(2000));
			}
		}

		// In-progress
		if (auto ipNode = node.child("InProgress")) {
			if (auto n = ipNode.child("bonus")) {
				inProgressConfig.bonus = static_cast<int16_t>(n.text().as_int(200));
			}
		}

		// Task age
		if (auto ageNode = node.child("TaskAge")) {
			if (auto n = ageNode.child("bonusPerMinute")) {
				taskAgeConfig.bonusPerMinute = static_cast<int16_t>(n.text().as_int(1));
			}
			if (auto n = ageNode.child("maxBonus")) {
				taskAgeConfig.maxBonus = static_cast<int16_t>(n.text().as_int(100));
			}
		}
	}

	void PriorityConfig::parseThresholds(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		if (auto n = node.child("taskSwitchThreshold")) {
			timingConfig.taskSwitchThreshold = static_cast<int16_t>(n.text().as_int(50));
		}
		if (auto n = node.child("reEvalInterval")) {
			timingConfig.reEvalInterval = n.text().as_float(0.5F);
		}
		if (auto n = node.child("reservationTimeout")) {
			timingConfig.reservationTimeout = n.text().as_float(10.0F);
		}
	}

	void PriorityConfig::parseHaulingTuning(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		if (auto n = node.child("storageCriticalThreshold")) {
			haulingConfig.storageCriticalThreshold = n.text().as_float(0.2F);
		}
		if (auto n = node.child("storageCriticalBonus")) {
			haulingConfig.storageCriticalBonus = static_cast<int16_t>(n.text().as_int(500));
		}
		if (auto n = node.child("blockingConstructionBonus")) {
			haulingConfig.blockingConstructionBonus = static_cast<int16_t>(n.text().as_int(1000));
		}
		if (auto n = node.child("perishableSpoilThreshold")) {
			haulingConfig.perishableSpoilThreshold = n.text().as_float(60.0F);
		}
		if (auto n = node.child("perishableBonus")) {
			haulingConfig.perishableBonus = static_cast<int16_t>(n.text().as_int(800));
		}
		if (auto n = node.child("batchRadius")) {
			haulingConfig.batchRadius = n.text().as_float(8.0F);
		}
		if (auto n = node.child("maxBatchSize")) {
			haulingConfig.maxBatchSize = static_cast<int16_t>(n.text().as_int(5));
		}
	}

	void PriorityConfig::parseCategoryOrder(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		struct CategoryTier {
			std::string name;
			float		tier;
		};
		std::vector<CategoryTier> categories;

		for (pugi::xml_node catNode : node.children("Category")) {
			std::string name = catNode.attribute("name").as_string();
			float		tier = catNode.attribute("tier").as_float(999.0F);

			if (!name.empty()) {
				categories.push_back({name, tier});
				categoryTiers[name] = tier;
			}
		}

		// Sort by tier
		std::sort(categories.begin(), categories.end(), [](const CategoryTier& a, const CategoryTier& b) { return a.tier < b.tier; });

		categoryOrder.clear();
		for (const auto& cat : categories) {
			categoryOrder.push_back(cat.name);
		}
	}

} // namespace engine::assets
