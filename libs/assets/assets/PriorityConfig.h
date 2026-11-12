#pragma once

// Priority Configuration
// Tunable weights for task priority calculations loaded from XML.
// Last in the config load order - depends on WorkTypeRegistry.
//
// The AI arbitrates by a lexicographic (tier, score) key: <TaskTiers> gives each task type its base
// tier, and the <Bonuses> weights make up the within-tier score.
//
// See /docs/design/game-systems/colonists/priority-config.md for design details.

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace assets {

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

	/// Storage-priority weight config (AI arbitration, within tier 6 only).
	/// A storage-stocking option's destination box has a StoragePriority for the item (Low..Critical,
	/// ranked 0..3). The within-tier score gains rank * weight, so a higher-priority destination box
	/// outranks a lower-priority one even when farther. Set to the distance factor's max (300) so
	/// storage priority DOMINATES distance within tier 6 (a Critical box farther away beats a Low box
	/// nearer). It can never cross a tier boundary: tier is compared first, lexicographically.
	struct StoragePriorityConfig {
		int16_t weight = 300; // Per-priority-rank score weight (rank 0..3 * weight)
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

		/// Moves on every load and clear, so a reader that caches values can tell they may have changed.
		[[nodiscard]] uint64_t generation() const { return m_generation; }

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

		// --- Bonus Calculations ---

		/// Calculate the within-tier distance factor for the (tier, score) arbitration: a strong,
		/// monotonically-decreasing nearest-source term. factor(d) = maxFactor * max(0, 1 - d/maxDistance).
		/// @param distance Distance to task target (meters)
		/// @return Distance factor in [0, maxFactor]
		[[nodiscard]] float calculateDistanceFactor(float distance) const;

		/// Get in-progress task bonus
		/// @return In-progress bonus value
		[[nodiscard]] int16_t getInProgressBonus() const;

		/// Get the per-rank storage-priority score weight (AI arbitration, within tier 6).
		/// @return Storage-priority weight value
		[[nodiscard]] int16_t getStoragePriorityWeight() const;

		/// Calculate task age bonus
		/// @param taskAge Age of task in seconds
		/// @return Age bonus
		[[nodiscard]] int16_t calculateTaskAgeBonus(float taskAge) const;

		// --- Config Getters ---

		[[nodiscard]] const DistanceFactorConfig&  getDistanceFactorConfig() const { return distanceFactorConfig; }
		[[nodiscard]] const InProgressBonusConfig& getInProgressConfig() const { return inProgressConfig; }
		[[nodiscard]] const StoragePriorityConfig& getStoragePriorityConfig() const { return storagePriorityConfig; }
		[[nodiscard]] const TaskAgeBonusConfig&	   getTaskAgeConfig() const { return taskAgeConfig; }
		[[nodiscard]] const HaulingTuningConfig&   getHaulingConfig() const { return haulingConfig; }
		[[nodiscard]] const TimingConfig&		   getTimingConfig() const { return timingConfig; }

		// --- Thresholds ---

		[[nodiscard]] int16_t getTaskSwitchThreshold() const { return timingConfig.taskSwitchThreshold; }
		[[nodiscard]] float	  getReEvalInterval() const { return timingConfig.reEvalInterval; }

		// --- Work Category Order ---

		/// Get all category names sorted by tier order
		[[nodiscard]] const std::vector<std::string>& getCategoryOrder() const { return categoryOrder; }

	  private:
		PriorityConfig();

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

		/// Per-task-type base tier for the (tier, score) arbitration (lower number = higher priority)
		std::unordered_map<std::string, int> taskTiers;

		/// Bonus configs
		DistanceFactorConfig  distanceFactorConfig;
		InProgressBonusConfig inProgressConfig;
		StoragePriorityConfig storagePriorityConfig;
		TaskAgeBonusConfig	  taskAgeConfig;
		HaulingTuningConfig	  haulingConfig;
		TimingConfig		  timingConfig;

		uint64_t m_generation = 1;

		/// Work category order (sorted by tier)
		std::vector<std::string> categoryOrder;
	};

} // namespace assets
