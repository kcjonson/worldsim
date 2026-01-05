#pragma once

// Decision Trace Component for Task Queue Display
// Captures why a colonist chose their current task and what alternatives exist.
// See /docs/design/game-systems/colonists/decision-trace.md for design details.

#include "Needs.h"
#include "Task.h"

#include <glm/vec2.hpp>

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

		// Gathering-specific fields (for Gather tasks)
		std::string gatherItemDefName;
		uint64_t	gatherTargetEntityId = 0;

		// Crafting-specific fields (for Craft tasks)
		std::string craftRecipeDefName;
		uint64_t	stationEntityId = 0;

		// Hauling-specific fields (for Haul tasks)
		std::string				 haulItemDefName;		  // Item to haul
		uint32_t				 haulQuantity = 1;		  // Quantity to haul
		std::optional<glm::vec2> haulSourcePosition;	  // Where to pick up from
		uint64_t				 haulTargetStorageId = 0; // Storage container entity ID
		std::optional<glm::vec2> haulTargetPosition;	  // Where to deposit

		// PlacePackaged-specific fields (for PlacePackaged tasks)
		uint64_t				 placePackagedEntityId = 0; // Entity ID of packaged item
		std::optional<glm::vec2> placeSourcePosition;		// Where the packaged item is
		std::optional<glm::vec2> placeTargetPosition;		// Where to place it

		// Skill-related fields (for work tasks with skill requirements)
		float	skillLevel = 0.0F; // Colonist's skill level for this work
		int16_t skillBonus = 0;	   // Calculated skill bonus for priority

		// Priority bonuses (from PriorityConfig calculations)
		int16_t distanceBonus = 0;	 // Distance-based bonus/penalty (-50 to +50)
		int16_t chainBonus = 0;		 // Chain continuation bonus (+2000 if continuing chain)
		int16_t inProgressBonus = 0; // Bonus for current task (+200)
		int16_t taskAgeBonus = 0;	 // Bonus for old unclaimed tasks (0 to +100)

		// Human-readable explanation for UI
		std::string reason;

		/// Calculate priority score for sorting
		/// Higher score = higher priority
		/// Full priority formula includes:
		/// - Base priority (by tier/task type)
		/// - Distance bonus (-50 to +50)
		/// - Skill bonus (0 to +100)
		/// - Chain continuation bonus (+2000 for next step in chain)
		/// - In-progress bonus (+200 for current task)
		/// - Task age bonus (0 to +100 for old unclaimed tasks)
		[[nodiscard]] float calculatePriority() const {
			// Helper to compute total bonus for work tasks
			auto workBonus = [this]() {
				return static_cast<float>(distanceBonus + skillBonus + chainBonus + inProgressBonus + taskAgeBonus);
			};

			// Tier 3: Critical needs get highest priority (300-310)
			// Needs are exempt from most bonuses (distance matters, others don't)
			if (needValue < 10.0F && status != OptionStatus::Satisfied) {
				return 300.0F + (10.0F - needValue) + static_cast<float>(distanceBonus);
			}
			// Tier 5: Actionable needs (100-150ish based on urgency)
			if (needValue < threshold && status != OptionStatus::Satisfied) {
				return 100.0F + (threshold - needValue) + static_cast<float>(distanceBonus);
			}
			// Tier 6: Work tasks (Gather Food, Crafting, etc.) - when needValue=100 and threshold=0
			// This indicates a work task, not a real need - priority 50 + all bonuses
			if (taskType == TaskType::FulfillNeed && needValue >= 100.0F && threshold == 0.0F && status == OptionStatus::Available) {
				return 50.0F + workBonus();
			}
			// Placing packaged items at target locations (priority 38 + distance/in-progress/chain)
			// If colonist is already carrying (needValue > 100), use needValue directly
			// as priority (typically 150) to ensure delivery completes before other tasks
			if (taskType == TaskType::PlacePackaged && status == OptionStatus::Available) {
				if (needValue > 100.0F) {
					// In-progress delivery - use high priority plus bonuses
					return needValue + static_cast<float>(distanceBonus + chainBonus + inProgressBonus);
				}
				return 38.0F + static_cast<float>(distanceBonus + chainBonus + inProgressBonus + taskAgeBonus);
			}
			// Tier 6.4: Hauling loose items to storage - priority 37 + bonuses (no skill bonus)
			if (taskType == TaskType::Haul && status == OptionStatus::Available) {
				return 37.0F + static_cast<float>(distanceBonus + chainBonus + inProgressBonus + taskAgeBonus);
			}
			// Tier 6.5: Crafting work - priority 40 + all bonuses
			if (taskType == TaskType::Craft && status == OptionStatus::Available) {
				return 40.0F + workBonus();
			}
			// Tier 6.6: Gathering materials for crafting - priority 35 + all bonuses
			if (taskType == TaskType::Gather && status == OptionStatus::Available) {
				return 35.0F + workBonus();
			}
			// Tier 7: Wander (lowest priority among active options - no bonuses)
			if (taskType == TaskType::Wander) {
				return 10.0F;
			}
			// Satisfied needs have no priority
			return 0.0F;
		}

		/// Check if this option can be executed (has a valid target or fallback)
		[[nodiscard]] bool isActionable() const { return status == OptionStatus::Selected || status == OptionStatus::Available; }
	};

	/// Decision trace component - captures the full decision context
	struct DecisionTrace {
		/// All evaluated options, sorted by priority (highest first)
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
