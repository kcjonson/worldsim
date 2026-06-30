#pragma once

// Priority Configuration
// Tunable weights for task priority calculations loaded from XML.
// Last in the config load order - depends on WorkTypeRegistry.
//
// TWO PRIORITY CONCEPTS (don't confuse them):
//
// 1. Colonist Work Type Preference (1-9):
//    Each colonist has a 1-9 preference per work category.
//    "Bob prefers Farming (2) over Hauling (7)"
//    -> Use userPriorityToBase() to convert to internal priority
//
// 2. Goal Priority (set on buildings/entities):
//    Storage containers, crafting stations can be marked urgent.
//    "This storage needs filling NOW"
//    -> Adds bonus to all tasks targeting that goal
//
// Players do NOT set priority on individual tasks.
// Tasks inherit priority from goals + colonist preferences + situational bonuses.
//
// See /docs/design/game-systems/colonists/priority-config.md for design details.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace engine::assets {

	/// Distance bonus calculation config
	struct DistanceBonusConfig {
		float	optimalDistance = 5.0F;		// Distance at which bonus is maximum
		float	maxPenaltyDistance = 50.0F; // Distance at which penalty is maximum
		int16_t maxBonus = 50;				// Bonus for optimal distance
		int16_t maxPenalty = 50;			// Penalty for max distance
	};

	/// Skill bonus calculation config
	struct SkillBonusConfig {
		int16_t multiplier = 10; // Skill level * multiplier
		int16_t maxBonus = 100;	 // Cap for skill bonus
	};

	/// Chain continuation config
	struct ChainBonusConfig {
		int16_t bonus = 2000; // Bonus for continuing a chain
	};

	/// In-progress task config.
	/// Repurposed for the (tier, score) arbitration as the within-tier hysteresis margin: the
	/// stickiness added to the currently in-progress option's score so a same-tier challenger must
	/// beat it by more than this margin to win. It can never lift an option across a tier boundary
	/// (score only compares within a tier). Defaults to taskSwitchThreshold (50) below.
	struct InProgressBonusConfig {
		int16_t bonus = 50; // Within-tier hysteresis margin for the in-progress option
	};

	/// Within-tier distance factor config.
	/// Replaces the old +-50 distanceBonus for the (tier, score) arbitration. The distance factor is
	/// the dominant within-tier score term: it decreases monotonically with distance and is weighted
	/// to dominate skill (0-100) and task age (0-100) across the working range, so the nearest
	/// reachable source wins within a tier (fixes "chops a tree 70 m away over an adjacent one").
	/// Linear curve: factor(d) = maxFactor * max(0, 1 - d / maxDistance), clamped to [0, maxFactor].
	struct DistanceFactorConfig {
		float maxDistance = 60.0F; // Distance (m) at which the factor reaches 0
		float maxFactor = 300.0F;  // Factor at distance 0 (dominates skill+age, exceeds hysteresis)
	};

	/// Task age bonus config
	struct TaskAgeBonusConfig {
		int16_t bonusPerMinute = 1; // Bonus per minute unclaimed
		int16_t maxBonus = 100;		// Cap for age bonus
	};

	/// Hauling-specific tuning
	struct HaulingTuningConfig {
		float	storageCriticalThreshold = 0.2F;  // Below this, storage is critical
		int16_t storageCriticalBonus = 500;		  // Bonus when storage critical
		int16_t blockingConstructionBonus = 1000; // Bonus for items blocking builds
		float	perishableSpoilThreshold = 60.0F; // Seconds until spoil = perishable
		int16_t perishableBonus = 800;			  // Bonus for perishable items
		float	batchRadius = 8.0F;				  // Group items within this radius
		int16_t maxBatchSize = 5;				  // Max items per batch
	};

	/// Timing thresholds
	struct TimingConfig {
		int16_t taskSwitchThreshold = 50;	// Min priority gap to switch tasks
		float	reEvalInterval = 0.5F;		// Seconds between task re-evaluation
		float	reservationTimeout = 10.0F; // Seconds before reservation expires
	};

	/// Priority tuning configuration
	/// Loaded from assets/config/work/priority-tuning.xml
	class PriorityConfig {
	  public:
		/// Get the singleton config instance
		static PriorityConfig& Get();

		// --- Loading ---

		/// Load priority config from an XML file
		/// @param xmlPath Path to the XML file (e.g., priority-tuning.xml)
		/// @return true if loading succeeded
		bool loadFromFile(const std::string& xmlPath);

		/// Clear all loaded config (reset to defaults)
		void clear();

		// --- Task Tier Queries (arbitration) ---

		/// Get the base tier for a task type by name (lower number = higher priority).
		/// This is the BASE tier from config; runtime classifiers in AIDecisionSystem may promote a
		/// specific option to a higher tier (e.g. a critical need, or a haul serving a work order).
		/// @param taskTypeName Task type name (e.g. "Craft", "Haul", "Wander")
		/// @return Base tier, or kUnassignedTier if the type has no explicit tier configured
		[[nodiscard]] int getTaskTier(const std::string& taskTypeName) const;

		/// Sentinel tier for a task type that has no explicit entry in the config. Used by
		/// validateTaskTiers to fail loud at load if any required type is missing a tier.
		static constexpr int kUnassignedTier = 9999;

		/// Validate that every required task type has an explicit base tier configured. Returns true
		/// and leaves missingOut empty when all are present; otherwise returns false and fills
		/// missingOut with the names of the unassigned types. Called at config load so a task type
		/// added without a tier fails loud rather than defaulting silently.
		[[nodiscard]] bool validateTaskTiers(const std::vector<std::string>& requiredTypeNames, std::vector<std::string>& missingOut) const;

		// --- Priority Band Queries (work-type DISPLAY priority; not the AI arbitration) ---

		/// Get base priority for a band by name
		/// @param bandName Band name (e.g., "Critical", "WorkHigh")
		/// @return Base priority value, or 0 if not found
		[[nodiscard]] int16_t getBandBase(const std::string& bandName) const;

		/// Convert colonist's work type preference (1-9) to base priority.
		/// This is the per-colonist preference for a work category, NOT a per-task priority.
		/// Example: Bob has Farming=2, Hauling=7. When evaluating tasks, his Farming
		/// tasks get higher base priority than Hauling tasks.
		/// @param userPriority Colonist's preference for this work type (1=prefer, 9=avoid)
		/// @return Base priority in Work bands (WorkHigh/Medium/Low)
		[[nodiscard]] int16_t userPriorityToBase(uint8_t userPriority) const;

		// --- Bonus Calculations ---

		/// Calculate distance bonus/penalty (legacy +-50 range; retained for the work-type display
		/// priority path, NOT used by the (tier, score) arbitration).
		/// @param distance Distance to task target
		/// @return Bonus (positive) or penalty (negative)
		[[nodiscard]] int16_t calculateDistanceBonus(float distance) const;

		/// Calculate the within-tier distance factor for the (tier, score) arbitration: a strong,
		/// monotonically-decreasing nearest-source term. factor(d) = maxFactor * max(0, 1 - d/maxDistance).
		/// @param distance Distance to task target (meters)
		/// @return Distance factor in [0, maxFactor]
		[[nodiscard]] float calculateDistanceFactor(float distance) const;

		/// Calculate skill bonus
		/// @param skillLevel Colonist's skill level (0-20)
		/// @return Skill bonus
		[[nodiscard]] int16_t calculateSkillBonus(float skillLevel) const;

		/// Get chain continuation bonus
		/// @return Chain bonus value
		[[nodiscard]] int16_t getChainBonus() const;

		/// Get in-progress task bonus
		/// @return In-progress bonus value
		[[nodiscard]] int16_t getInProgressBonus() const;

		/// Calculate task age bonus
		/// @param taskAge Age of task in seconds
		/// @return Age bonus
		[[nodiscard]] int16_t calculateTaskAgeBonus(float taskAge) const;

		// --- Config Getters ---

		[[nodiscard]] const DistanceBonusConfig&   getDistanceConfig() const { return distanceConfig; }
		[[nodiscard]] const DistanceFactorConfig&  getDistanceFactorConfig() const { return distanceFactorConfig; }
		[[nodiscard]] const SkillBonusConfig&	   getSkillConfig() const { return skillConfig; }
		[[nodiscard]] const ChainBonusConfig&	   getChainConfig() const { return chainConfig; }
		[[nodiscard]] const InProgressBonusConfig& getInProgressConfig() const { return inProgressConfig; }
		[[nodiscard]] const TaskAgeBonusConfig&	   getTaskAgeConfig() const { return taskAgeConfig; }
		[[nodiscard]] const HaulingTuningConfig&   getHaulingConfig() const { return haulingConfig; }
		[[nodiscard]] const TimingConfig&		   getTimingConfig() const { return timingConfig; }

		// --- Thresholds ---

		[[nodiscard]] int16_t getTaskSwitchThreshold() const { return timingConfig.taskSwitchThreshold; }
		[[nodiscard]] float	  getReEvalInterval() const { return timingConfig.reEvalInterval; }
		[[nodiscard]] float	  getReservationTimeout() const { return timingConfig.reservationTimeout; }

		// --- Work Category Order ---

		/// Get all category names sorted by tier order
		[[nodiscard]] const std::vector<std::string>& getCategoryOrder() const { return categoryOrder; }

		/// Get tier for a category (or 999 if not found)
		[[nodiscard]] float getCategoryTier(const std::string& categoryName) const;

	  private:
		PriorityConfig();

		/// Parse bands from XML node
		void parseBands(const void* node);

		/// Parse per-task-type tier table from XML node
		void parseTaskTiers(const void* node);

		/// Parse bonuses from XML node
		void parseBonuses(const void* node);

		/// Parse timing thresholds from XML node
		void parseThresholds(const void* node);

		/// Parse hauling tuning from XML node
		void parseHaulingTuning(const void* node);

		/// Parse work category order from XML node
		void parseCategoryOrder(const void* node);

		// --- Storage ---

		/// Priority bands by name (work-type display priority)
		std::unordered_map<std::string, int16_t> bands;

		/// Per-task-type base tier for the (tier, score) arbitration (lower number = higher priority)
		std::unordered_map<std::string, int> taskTiers;

		/// User priority step size
		int16_t userPriorityStep = 100;

		/// Bonus configs
		DistanceBonusConfig	  distanceConfig;
		DistanceFactorConfig  distanceFactorConfig;
		SkillBonusConfig	  skillConfig;
		ChainBonusConfig	  chainConfig;
		InProgressBonusConfig inProgressConfig;
		TaskAgeBonusConfig	  taskAgeConfig;
		HaulingTuningConfig	  haulingConfig;
		TimingConfig		  timingConfig;

		/// Work category order (sorted by tier)
		std::vector<std::string>			   categoryOrder;
		std::unordered_map<std::string, float> categoryTiers;
	};

} // namespace engine::assets
