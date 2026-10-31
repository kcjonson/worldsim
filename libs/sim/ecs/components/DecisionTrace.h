#pragma once

// Decision Trace Component for Task Queue Display
// Captures why a colonist chose their current task and what alternatives exist.
// See /docs/design/game-systems/colonists/decision-trace.md for design details.

#include "Needs.h"
#include "Task.h"

#include <glm/vec2.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ecs {

	/// Maximum number of options to display in the UI (configurable for future expansion)
	constexpr size_t kMaxDisplayedOptions = 10;

	/// Status of an evaluated task option
	enum class OptionStatus : uint8_t {
		Selected,  // This is the current task
		Available, // Could do this, but lower priority
		NoSource,  // Need exists but no known entity in memory
		Satisfied  // Need above threshold, no action needed
	};

	/// A single evaluated task option in the decision trace
	struct EvaluatedOption {
		TaskType taskType = TaskType::None;
		NeedType needType = NeedType::Count; // For FulfillNeed tasks

		// Need state at evaluation time
		float needValue = 100.0F; // Current value (0-100%)
		float threshold = 50.0F;  // Seek threshold for this need

		// Fulfillment status
		OptionStatus status = OptionStatus::Satisfied;

		// Target information (if Available or Selected)
		std::optional<glm::vec2> targetPosition;
		std::optional<uint32_t>	 targetDefNameId; // For display name lookup
		float					 distanceToTarget = 0.0F;


		// Crafting-specific fields (for Craft tasks)
		std::string craftRecipeDefName;
		uint64_t	stationEntityId = 0;

		// Hauling-specific fields (for Haul tasks)
		std::string				 haulItemDefName;		  // Item to haul
		uint32_t				 haulQuantity = 1;		  // Quantity to haul
		std::optional<glm::vec2> haulSourcePosition;	  // Where to pick up from
		uint64_t				 haulSourceStorageId = 0; // Source box entity ID for a storage->storage pull (0 = loose/inventory source)
		uint64_t				 haulTargetStorageId = 0; // Storage container entity ID
		std::optional<glm::vec2> haulTargetPosition;	  // Where to deposit
		uint64_t				 haulGoalId = 0;		  // Goal being fulfilled (for reservation)
		bool					 haulFromInventory = false; // Source is colonist inventory (craft-material haul)

		// PlacePackaged-specific fields (for PlacePackaged tasks)
		uint64_t				 placePackagedEntityId = 0; // Entity ID of packaged item
		std::optional<glm::vec2> placeSourcePosition;		// Where the packaged item is
		std::optional<glm::vec2> placeTargetPosition;		// Where to place it

		// Harvest-specific fields (for Harvest tasks)
		uint64_t harvestTargetEntityId = 0;	 // Entity ID of harvestable (tree, bush)
		uint64_t harvestGoalId = 0;			 // Goal being fulfilled (for reservation)
		uint32_t harvestYieldDefNameId = 0;	 // What item will be yielded

		// Build-specific fields (for Build tasks)
		uint64_t buildBlueprintEntityId = 0; // Blueprint entity whose workDone is advanced

		// True when this Haul/Harvest/Build provisions or performs an active work order
		// (a child of a Craft goal with a queued job, a construction material delivery, or the
		// Build action on a ready blueprint). This is committed work, not a loose chore:
		// classifyTier() promotes it to tier 4 (active work orders), above opportunistic work and
		// idle, so a far material source or build site can't make the colonist abandon started work.
		bool servesActiveWorkOrder = false;

		// True when this Haul/Harvest exists only to top up a storage container toward its
		// configured minimum (a Task B "stocking" goal owned by StorageGoalSystem). Stocking is real
		// work but ranks below work orders: it stays at the opportunistic tier (6), so a colonist
		// always prefers building/crafting (and provisioning those) over filling a box.
		// Mutually exclusive with servesActiveWorkOrder: a stocking goal is not a work order.
		bool servesStorageStocking = false;

		// --- (tier, score) arbitration key ---
		// tier: categorical priority, compared FIRST. Lower number = higher priority (1 best).
		// Set by AIDecisionSystem's classifyTier() from the config base + runtime promotions.
		// score: orders options WITHIN one tier only; never crosses a tier boundary.
		int	  tier = 7;	   // Default to the idle tier until classified
		float score = 0.0F; // = distanceFactor + skillBonus + taskAgeBonus + hysteresisBonus

		// Skill-related fields (for work tasks with skill requirements)
		float	skillLevel = 0.0F; // Colonist's skill level for this work
		int16_t skillBonus = 0;	   // Calculated skill bonus (a within-tier score term)

		// Within-tier score breakdown (kept as separate fields for the UI bonus display).
		// distanceFactor is stored as a float because it dominates the within-tier score (0..~300).
		float	distanceFactor = 0.0F; // Strong nearest-reachable term (decreases with distance)
		int16_t taskAgeBonus = 0;	   // Old unclaimed tasks rise within their tier (0 to +100)
		int16_t hysteresisBonus = 0;   // Stickiness margin, applied only to the in-progress option

		// Storage-stocking only (servesStorageStocking): destination box priority rank (0..3) * weight.
		// Orders stocking hauls WITHIN tier 6 so a higher-priority destination box wins even when
		// farther. Weighted to dominate distanceFactor; 0 for every non-stocking option. Never crosses
		// a tier (tier is compared first).
		int16_t storagePriorityBias = 0;

		// Stable tiebreak key for deterministic option ordering. When two options have the same
		// (tier, score) (e.g. two equidistant, equal-skill build sites), the sort must not fall back
		// to container iteration order: the goal registry is backed by unordered containers, so that
		// order is hash-bucket-dependent and would route colonists differently across machines,
		// desyncing a fixed-step multiplayer simulation. Each evaluator fills this with the most
		// stable id it has (goal id, station/entity id, etc.); higherPriority() breaks ties on it.
		uint64_t tiebreakId = 0;

		// Human-readable explanation for UI
		std::string reason;

		/// Compose the within-tier score from its breakdown fields. Score orders options WITHIN one
		/// tier only (tier is compared first), so it never needs to encode categorical priority.
		/// distanceFactor dominates; skill and age refine; hysteresis sticks the in-progress option.
		[[nodiscard]] float computeScore() const {
			return distanceFactor + static_cast<float>(skillBonus) + static_cast<float>(taskAgeBonus) + static_cast<float>(hysteresisBonus) +
				   static_cast<float>(storagePriorityBias);
		}

		/// Lexicographic (tier, score) ordering: returns true if `a` is HIGHER priority than `b`.
		/// Lower tier wins; within a tier, higher score wins; ties break on tiebreakId ascending
		/// (a multiplayer-determinism guard, since the goal registry iterates hash-bucket order).
		/// This is THE arbitration: tier is inviolable, score can never cross a tier boundary.
		[[nodiscard]] static bool higherPriority(const EvaluatedOption& a, const EvaluatedOption& b) {
			if (a.tier != b.tier) {
				return a.tier < b.tier;
			}
			if (a.score != b.score) {
				return a.score > b.score;
			}
			return a.tiebreakId < b.tiebreakId;
		}

		/// Check if this option can be executed (has a valid target or fallback)
		[[nodiscard]] bool isActionable() const { return status == OptionStatus::Selected || status == OptionStatus::Available; }
	};

	/// Decision trace component - captures the full decision context
	struct DecisionTrace {
		/// All evaluated options, sorted by the (tier, score) key (highest priority first)
		std::vector<EvaluatedOption> options;

		/// Timestamp of last evaluation (game time in seconds)
		float lastEvaluationTime = 0.0F;

		/// Summary of why the current task was selected
		std::string selectionSummary;

		/// Clear the trace for re-evaluation
		void clear() {
			options.clear();
			selectionSummary.clear();
			// Note: lastEvaluationTime is set by the system after building
		}

		/// Get the currently selected option (first with Selected status)
		[[nodiscard]] const EvaluatedOption* getSelected() const {
			for (const auto& option : options) {
				if (option.status == OptionStatus::Selected) {
					return &option;
				}
			}
			return nullptr;
		}

		/// Get number of options to display (capped by kMaxDisplayedOptions)
		[[nodiscard]] size_t displayCount() const { return std::min(options.size(), kMaxDisplayedOptions); }
	};

} // namespace ecs
