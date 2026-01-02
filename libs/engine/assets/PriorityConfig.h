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

	/// In-progress task config
	struct InProgressBonusConfig {
		int16_t bonus = 200; // Bonus for current task (resist switching)
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

		// --- Priority Band Queries ---

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

		/// Calculate distance bonus/penalty
		/// @param distance Distance to task target
		/// @return Bonus (positive) or penalty (negative)
		[[nodiscard]] int16_t calculateDistanceBonus(float distance) const;

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

		[[nodiscard]] const DistanceBonusConfig&   getDistanceConfig() const { return m_distance; }
		[[nodiscard]] const SkillBonusConfig&	   getSkillConfig() const { return m_skill; }
		[[nodiscard]] const ChainBonusConfig&	   getChainConfig() const { return m_chain; }
		[[nodiscard]] const InProgressBonusConfig& getInProgressConfig() const { return m_inProgress; }
		[[nodiscard]] const TaskAgeBonusConfig&	   getTaskAgeConfig() const { return m_taskAge; }
		[[nodiscard]] const HaulingTuningConfig&   getHaulingConfig() const { return m_hauling; }
		[[nodiscard]] const TimingConfig&		   getTimingConfig() const { return m_timing; }

		// --- Thresholds ---

		[[nodiscard]] int16_t getTaskSwitchThreshold() const { return m_timing.taskSwitchThreshold; }
		[[nodiscard]] float	  getReEvalInterval() const { return m_timing.reEvalInterval; }
		[[nodiscard]] float	  getReservationTimeout() const { return m_timing.reservationTimeout; }

		// --- Work Category Order ---

		/// Get all category names sorted by tier order
		[[nodiscard]] const std::vector<std::string>& getCategoryOrder() const { return m_categoryOrder; }

		/// Get tier for a category (or 999 if not found)
		[[nodiscard]] float getCategoryTier(const std::string& categoryName) const;

	  private:
		PriorityConfig();

		/// Parse bands from XML node
		void parseBands(const void* node);

		/// Parse bonuses from XML node
		void parseBonuses(const void* node);

		/// Parse timing thresholds from XML node
		void parseThresholds(const void* node);

		/// Parse hauling tuning from XML node
		void parseHaulingTuning(const void* node);

		/// Parse work category order from XML node
		void parseCategoryOrder(const void* node);

		// --- Storage ---

		/// Priority bands by name
		std::unordered_map<std::string, int16_t> m_bands;

		/// User priority step size
		int16_t m_userPriorityStep = 100;

		/// Bonus configs
		DistanceBonusConfig	  m_distance;
		SkillBonusConfig	  m_skill;
		ChainBonusConfig	  m_chain;
		InProgressBonusConfig m_inProgress;
		TaskAgeBonusConfig	  m_taskAge;
		HaulingTuningConfig	  m_hauling;
		TimingConfig		  m_timing;

		/// Work category order (sorted by tier)
		std::vector<std::string>			   m_categoryOrder;
		std::unordered_map<std::string, float> m_categoryTiers;
	};

} // namespace engine::assets
