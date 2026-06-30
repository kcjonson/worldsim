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

	namespace {
		/// Default per-task-type BASE tiers for the (tier, score) arbitration (lower = higher
		/// priority). These mirror the spec's tier ladder; runtime classifiers in AIDecisionSystem
		/// promote specific options (critical need -> 2, serves-work-order -> 4, etc.). The XML
		/// <TaskTiers> block overrides these so designers can retune without a code change; these
		/// defaults are the same values, kept in sync as the validation/tuning source of truth.
		void installDefaultTaskTiers(std::unordered_map<std::string, int>& tiers) {
			tiers["FulfillNeed"] = 5; // base = actionable need; promoted to 2 (critical) or 7 (gather-food sentinel)
			tiers["Craft"] = 4;
			tiers["Build"] = 4;
			tiers["Deconstruct"] = 4;
			tiers["Haul"] = 6;			// base = opportunistic; promoted to 4 when serving a work order
			tiers["Harvest"] = 6;		// base = opportunistic; promoted to 4 when serving a work order
			tiers["PlacePackaged"] = 6; // base = opportunistic; promoted to 4 when serving/carrying
			tiers["Wander"] = 7;
		}
	} // namespace

	PriorityConfig::PriorityConfig() {
		// Initialize default bands (work-type display priority)
		bands["Critical"] = 30000;
		bands["PlayerDraft"] = 20000;
		bands["Needs"] = 10000;
		bands["WorkHigh"] = 5000;
		bands["WorkMedium"] = 3000;
		bands["WorkLow"] = 1000;
		bands["Idle"] = 0;

		installDefaultTaskTiers(taskTiers);
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

		// The loaded XML is the authoritative source for arbitration tiers: clear the
		// constructor-installed defaults BEFORE parsing <TaskTiers> so a type missing from the XML
		// (or a deleted <TaskTiers> block) actually fails validateTaskTiers rather than silently
		// falling back to a C++ default. A default-constructed PriorityConfig (unit tests that never
		// load XML) keeps the constructor's defaults; only this file-load path is authoritative.
		taskTiers.clear();

		// Parse sections
		if (auto bandsNode = root.child("Bands")) {
			parseBands(&bandsNode);
		}

		if (auto tiersNode = root.child("TaskTiers")) {
			parseTaskTiers(&tiersNode);
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

		taskTiers.clear();
		installDefaultTaskTiers(taskTiers);

		userPriorityStep = 100;
		distanceConfig = DistanceBonusConfig{};
		distanceFactorConfig = DistanceFactorConfig{};
		skillConfig = SkillBonusConfig{};
		chainConfig = ChainBonusConfig{};
		inProgressConfig = InProgressBonusConfig{};
		taskAgeConfig = TaskAgeBonusConfig{};
		haulingConfig = HaulingTuningConfig{};
		timingConfig = TimingConfig{};
		categoryOrder.clear();
		categoryTiers.clear();
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
			// Absent key OR a sentinel value (malformed/non-integer tier= parsed to kUnassignedTier)
			// both count as unassigned, so a <Task name="Craft"/> with no valid tier fails loud
			// rather than silently sorting to the bottom.
			if (it == taskTiers.end() || it->second == kUnassignedTier) {
				missingOut.push_back(name);
			}
		}
		return missingOut.empty();
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

	float PriorityConfig::calculateDistanceFactor(float distance) const {
		if (distance <= 0.0F) {
			return distanceFactorConfig.maxFactor;
		}
		if (distance >= distanceFactorConfig.maxDistance) {
			return 0.0F;
		}
		return distanceFactorConfig.maxFactor * (1.0F - distance / distanceFactorConfig.maxDistance);
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

	void PriorityConfig::parseTaskTiers(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		for (pugi::xml_node tierNode : node.children("Task")) {
			std::string name = tierNode.attribute("name").as_string();
			if (name.empty()) {
				continue;
			}
			// tier attribute is required per task; a missing/zero attribute is a config error caught
			// by validateTaskTiers only when the whole entry is absent, so default to the sentinel
			// here so a malformed entry surfaces as "unassigned" rather than silently tier 0.
			int tier = tierNode.attribute("tier").as_int(kUnassignedTier);
			taskTiers[name] = tier;
			LOG_DEBUG(Engine, "Task tier: %s = %d", name.c_str(), tier);
		}
	}

	void PriorityConfig::parseBonuses(const void* nodePtr) {
		const pugi::xml_node& node = *static_cast<const pugi::xml_node*>(nodePtr);

		// Within-tier distance factor (arbitration)
		if (auto dfNode = node.child("DistanceFactor")) {
			if (auto n = dfNode.child("maxDistance")) {
				distanceFactorConfig.maxDistance = n.text().as_float(60.0F);
			}
			if (auto n = dfNode.child("maxFactor")) {
				distanceFactorConfig.maxFactor = n.text().as_float(300.0F);
			}
		}

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
				// Fallback matches the struct default, the XML value, and taskSwitchThreshold (50):
				// the within-tier hysteresis margin. An empty node must not yield the old 200.
				inProgressConfig.bonus = static_cast<int16_t>(n.text().as_int(50));
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
