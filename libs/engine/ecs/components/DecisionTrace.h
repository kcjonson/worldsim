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
		uint64_t gatherTargetEntityId = 0;

		// Crafting-specific fields (for Craft tasks)
		std::string craftRecipeDefName;
		uint64_t stationEntityId = 0;

		// Human-readable explanation for UI
		std::string reason;

		/// Calculate priority score for sorting
		/// Higher score = higher priority
		[[nodiscard]] float calculatePriority() const {
			// Tier 3: Critical needs get highest priority (300-310)
			if (needValue < 10.0F && status != OptionStatus::Satisfied) {
				return 300.0F + (10.0F - needValue);
			}
			// Tier 5: Actionable needs (100-150ish based on urgency)
			if (needValue < threshold && status != OptionStatus::Satisfied) {
				return 100.0F + (threshold - needValue);
			}
			// Tier 6: Work tasks (Gather Food, Crafting, etc.) - when needValue=100 and threshold=0
			// This indicates a work task, not a real need - priority 50
			if (taskType == TaskType::FulfillNeed && needValue >= 100.0F && threshold == 0.0F && status == OptionStatus::Available) {
				return 50.0F;
			}
			// Tier 6.5: Crafting work - priority 40
			if (taskType == TaskType::Craft && status == OptionStatus::Available) {
				return 40.0F;
			}
			// Tier 6.6: Gathering materials for crafting - priority 35
			if (taskType == TaskType::Gather && status == OptionStatus::Available) {
				return 35.0F;
			}
			// Tier 7: Wander (lowest priority among active options)
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
