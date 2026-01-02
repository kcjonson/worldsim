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
		m_bands["Critical"] = 30000;
		m_bands["PlayerDraft"] = 20000;
		m_bands["Needs"] = 10000;
		m_bands["WorkHigh"] = 5000;
		m_bands["WorkMedium"] = 3000;
		m_bands["WorkLow"] = 1000;
		m_bands["Idle"] = 0;
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
		m_bands.clear();
		m_bands["Critical"] = 30000;
		m_bands["PlayerDraft"] = 20000;
		m_bands["Needs"] = 10000;
		m_bands["WorkHigh"] = 5000;
		m_bands["WorkMedium"] = 3000;
		m_bands["WorkLow"] = 1000;
		m_bands["Idle"] = 0;

		m_distance = DistanceBonusConfig{};
		m_skill = SkillBonusConfig{};
		m_chain = ChainBonusConfig{};
		m_inProgress = InProgressBonusConfig{};
		m_taskAge = TaskAgeBonusConfig{};
		m_hauling = HaulingTuningConfig{};
		m_timing = TimingConfig{};
		m_categoryOrder.clear();
		m_categoryTiers.clear();
	}

	int16_t PriorityConfig::getBandBase(const std::string& bandName) const {
		auto it = m_bands.find(bandName);
		if (it != m_bands.end()) {
			return it->second;
		}
		return 0;
	}

	int16_t PriorityConfig::calculateDistanceBonus(float distance) const {
		if (distance <= m_distance.optimalDistance) {
			return m_distance.maxBonus;
		}

		if (distance >= m_distance.maxPenaltyDistance) {
			return -m_distance.maxPenalty;
		}

		// Linear interpolation
		float range = m_distance.maxPenaltyDistance - m_distance.optimalDistance;
		float normalized = (distance - m_distance.optimalDistance) / range;
		float bonus =
			static_cast<float>(m_distance.maxBonus) - normalized * static_cast<float>(m_distance.maxBonus + m_distance.maxPenalty);

		return static_cast<int16_t>(std::round(bonus));
	}

	int16_t PriorityConfig::calculateSkillBonus(float skillLevel) const {
		int16_t bonus = static_cast<int16_t>(skillLevel * static_cast<float>(m_skill.multiplier));
		return std::min(bonus, m_skill.maxBonus);
	}

	int16_t PriorityConfig::getChainBonus() const {
		return m_chain.bonus;
	}

	int16_t PriorityConfig::getInProgressBonus() const {
		return m_inProgress.bonus;
	}

	int16_t PriorityConfig::calculateTaskAgeBonus(float taskAge) const {
		float	minutes = taskAge / 60.0F;
		int16_t bonus = static_cast<int16_t>(minutes * static_cast<float>(m_taskAge.bonusPerMinute));
		return std::min(bonus, m_taskAge.maxBonus);
	}

	float PriorityConfig::getCategoryTier(const std::string& categoryName) const {
		auto it = m_categoryTiers.find(categoryName);
		if (it != m_categoryTiers.end()) {
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
				m_bands[name] = base;
				LOG_DEBUG(Engine, "Priority band: %s = %d", name.c_str(), base);
			}
		}
	}

	void PriorityConfig::parseBonuses(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		// Distance
		if (auto distNode = node.child("Distance")) {
			if (auto n = distNode.child("optimalDistance")) {
				m_distance.optimalDistance = n.text().as_float(5.0F);
			}
			if (auto n = distNode.child("maxPenaltyDistance")) {
				m_distance.maxPenaltyDistance = n.text().as_float(50.0F);
			}
			if (auto n = distNode.child("maxBonus")) {
				m_distance.maxBonus = static_cast<int16_t>(n.text().as_int(50));
			}
			if (auto n = distNode.child("maxPenalty")) {
				m_distance.maxPenalty = static_cast<int16_t>(n.text().as_int(50));
			}
		}

		// Skill
		if (auto skillNode = node.child("Skill")) {
			if (auto n = skillNode.child("multiplier")) {
				m_skill.multiplier = static_cast<int16_t>(n.text().as_int(10));
			}
			if (auto n = skillNode.child("maxBonus")) {
				m_skill.maxBonus = static_cast<int16_t>(n.text().as_int(100));
			}
		}

		// Chain continuation
		if (auto chainNode = node.child("ChainContinuation")) {
			if (auto n = chainNode.child("bonus")) {
				m_chain.bonus = static_cast<int16_t>(n.text().as_int(2000));
			}
		}

		// In-progress
		if (auto ipNode = node.child("InProgress")) {
			if (auto n = ipNode.child("bonus")) {
				m_inProgress.bonus = static_cast<int16_t>(n.text().as_int(200));
			}
		}

		// Task age
		if (auto ageNode = node.child("TaskAge")) {
			if (auto n = ageNode.child("bonusPerMinute")) {
				m_taskAge.bonusPerMinute = static_cast<int16_t>(n.text().as_int(1));
			}
			if (auto n = ageNode.child("maxBonus")) {
				m_taskAge.maxBonus = static_cast<int16_t>(n.text().as_int(100));
			}
		}
	}

	void PriorityConfig::parseThresholds(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		if (auto n = node.child("taskSwitchThreshold")) {
			m_timing.taskSwitchThreshold = static_cast<int16_t>(n.text().as_int(50));
		}
		if (auto n = node.child("reEvalInterval")) {
			m_timing.reEvalInterval = n.text().as_float(0.5F);
		}
		if (auto n = node.child("reservationTimeout")) {
			m_timing.reservationTimeout = n.text().as_float(10.0F);
		}
	}

	void PriorityConfig::parseHaulingTuning(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		if (auto n = node.child("storageCriticalThreshold")) {
			m_hauling.storageCriticalThreshold = n.text().as_float(0.2F);
		}
		if (auto n = node.child("storageCriticalBonus")) {
			m_hauling.storageCriticalBonus = static_cast<int16_t>(n.text().as_int(500));
		}
		if (auto n = node.child("blockingConstructionBonus")) {
			m_hauling.blockingConstructionBonus = static_cast<int16_t>(n.text().as_int(1000));
		}
		if (auto n = node.child("perishableSpoilThreshold")) {
			m_hauling.perishableSpoilThreshold = n.text().as_float(60.0F);
		}
		if (auto n = node.child("perishableBonus")) {
			m_hauling.perishableBonus = static_cast<int16_t>(n.text().as_int(800));
		}
		if (auto n = node.child("batchRadius")) {
			m_hauling.batchRadius = n.text().as_float(8.0F);
		}
		if (auto n = node.child("maxBatchSize")) {
			m_hauling.maxBatchSize = static_cast<int16_t>(n.text().as_int(5));
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
				m_categoryTiers[name] = tier;
			}
		}

		// Sort by tier
		std::sort(categories.begin(), categories.end(), [](const CategoryTier& a, const CategoryTier& b) { return a.tier < b.tier; });

		m_categoryOrder.clear();
		for (const auto& cat : categories) {
			m_categoryOrder.push_back(cat.name);
		}
	}

} // namespace engine::assets
