#pragma once

// Priority Configuration
// The arbitration file (assets/config/arbitration/, root <arbitration>): the ladder of named rungs, and the
// tunable weights for task priority calculations. Last in the work config's load order; depends on
// WorkTypeRegistry.
//
// The AI arbitrates by a lexicographic (tier, score) key: an option's tier is the position of its rung in
// the ladder, and the <Bonuses> weights make up the within-tier score.
//
// See /docs/design/game-systems/colonists/priority-config.md for design details.

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace pugi {
	class xml_node;
}

namespace assets {

	class ConfigReader;

	/// In-progress task config.
	/// Repurposed for the (tier, score) arbitration as the within-tier hysteresis margin: the
	/// stickiness added to the currently in-progress option's score so a same-tier challenger must
	/// beat it by more than this margin to win. It can never lift an option across a tier boundary
	/// (score only compares within a tier).
	struct InProgressBonusConfig {
		int16_t bonus = 0; // Within-tier hysteresis margin for the in-progress option
	};

	/// Within-tier distance factor config.
	/// Replaces the old +-50 distanceBonus for the (tier, score) arbitration. The distance factor is
	/// the dominant within-tier score term: it decreases monotonically with distance and is weighted
	/// to dominate skill (0-100) and task age (0-100) across the working range, so the nearest
	/// reachable source wins within a tier (fixes "chops a tree 70 m away over an adjacent one").
	/// Linear curve: factor(d) = maxFactor * max(0, 1 - d / maxDistance), clamped to [0, maxFactor].
	struct DistanceFactorConfig {
		float maxDistance = 0.0F; // Distance (m) at which the factor reaches 0
		float maxFactor = 0.0F;	  // Factor at distance 0 (dominates skill+age, exceeds hysteresis)
	};

	/// Task age bonus config
	struct TaskAgeBonusConfig {
		int16_t bonusPerMinute = 0; // Bonus per minute unclaimed
		int16_t maxBonus = 0;		// Cap for age bonus
	};

	/// Storage-priority weight config (AI arbitration, within the opportunistic_work rung only).
	/// A storage-stocking option's destination box has a StoragePriority for the item (Low..Critical,
	/// ranked 0..3). The within-tier score gains rank * weight, so a higher-priority destination box
	/// outranks a lower-priority one even when farther. Set to the distance factor's max (300) so
	/// storage priority DOMINATES distance within that rung (a Critical box farther away beats a Low box
	/// nearer). It can never cross a tier boundary: tier is compared first, lexicographically.
	struct StoragePriorityConfig {
		int16_t weight = 0; // Per-priority-rank score weight (rank 0..3 * weight)
	};

	/// The rungs the decision ranks its options on, each as its position in the ladder (0 the top), the
	/// handle a work entry's rung is too: a lower position outranks a higher one.
	struct DecisionRungs {
		uint8_t criticalNeed = 0;
		uint8_t committedWork = 0;
		uint8_t ordinaryNeed = 0;
		uint8_t opportunisticWork = 0;
		uint8_t idle = 0;
	};

	/// Priority tuning configuration
	/// Loaded from assets/config/arbitration/priority-tuning.xml
	class PriorityConfig {
	  public:
		/// Get the singleton config instance
		static PriorityConfig& Get();

		// --- Loading ---

		/// Replaces everything with `configRoot / file`: false, with problems() saying why, when the file can't be
		/// read or a value in it is missing or malformed. Errors name the file as `file` is written.
		bool loadFromFile(const std::filesystem::path& configRoot, const std::filesystem::path& file);

		/// What the file got wrong: each names the file, the element and the attribute.
		[[nodiscard]] const std::vector<std::string>& problems() const { return problemList; }

		/// Clear all loaded config
		void clear();

		/// The ladder's rung names, highest first; a rung's position is its tier.
		[[nodiscard]] const std::vector<std::string>& rungs() const { return rungNames; }

		/// The rungs the decision ranks options on, found by name in the ladder at load. Each is the rung's
		/// position only when missingDecisionRungs() is empty; loadWorkConfig fails the load otherwise.
		[[nodiscard]] const DecisionRungs& decisionRungs() const { return decisionRungPositions; }

		/// One problem for each rung the decision ranks options on that the ladder lacks, naming the file and the
		/// rung; empty when the ladder has them all.
		[[nodiscard]] const std::vector<std::string>& missingDecisionRungs() const { return missingDecisionRungList; }

		// --- Bonus Calculations ---

		/// Calculate the within-tier distance factor for the (tier, score) arbitration: a strong,
		/// monotonically-decreasing nearest-source term. factor(d) = maxFactor * max(0, 1 - d/maxDistance).
		/// @param distance Distance to task target (meters)
		/// @return Distance factor in [0, maxFactor]
		[[nodiscard]] float calculateDistanceFactor(float distance) const;

		/// Get in-progress task bonus
		/// @return In-progress bonus value
		[[nodiscard]] int16_t getInProgressBonus() const;

		/// Get the per-rank storage-priority score weight (AI arbitration, within the opportunistic_work rung).
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

		// --- Work Category Order ---

		/// Get all category names sorted by tier order
		[[nodiscard]] const std::vector<std::string>& getCategoryOrder() const { return categoryOrder; }

	  private:
		PriorityConfig() = default;

		/// Read the one <ladder> under `root` into rungNames, adding what is wrong with it to `problems`.
		void parseLadder(const pugi::xml_node& root, const std::string& source, std::vector<std::string>& problems);

		/// Find the decision's rungs in rungNames, noting each one missing against `source`.
		void resolveDecisionRungs(const std::string& source);

		/// Parse bonuses from XML node
		void parseBonuses(const void* node, ConfigReader& reader);

		/// Parse work category order from XML node
		void parseCategoryOrder(const void* node, ConfigReader& reader);

		// --- Storage ---

		/// The ladder's rung names, highest first
		std::vector<std::string> rungNames;

		DecisionRungs			 decisionRungPositions;
		std::vector<std::string> missingDecisionRungList;

		/// Bonus configs
		DistanceFactorConfig  distanceFactorConfig;
		InProgressBonusConfig inProgressConfig;
		StoragePriorityConfig storagePriorityConfig;
		TaskAgeBonusConfig	  taskAgeConfig;

		std::vector<std::string> problemList;

		/// Work category order (sorted by tier)
		std::vector<std::string> categoryOrder;
	};

} // namespace assets
