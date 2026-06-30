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
		// Build action on a ready blueprint). This is committed work, not a loose chore: its
		// priority is floored above idle Wander (see calculatePriority) so a far material source
		// or build site can't make the colonist abandon started work and wander off.
		bool servesActiveWorkOrder = false;

		// True when this Haul/Harvest exists only to top up a storage container toward its
		// configured minimum (a Task B "stocking" goal owned by StorageGoalSystem). Stocking is
		// real work, so it is floored above idle Wander -- but with a LOWER floor than
		// servesActiveWorkOrder, so a colonist always prefers building/crafting (and provisioning
		// those) over filling a box, and only stocks when no construction/craft work exists.
		// Mutually exclusive with servesActiveWorkOrder: a stocking goal is not a work order.
		bool servesStorageStocking = false;

		// Skill-related fields (for work tasks with skill requirements)
		float	skillLevel = 0.0F; // Colonist's skill level for this work
		int16_t skillBonus = 0;	   // Calculated skill bonus for priority

		// Priority bonuses (from PriorityConfig calculations)
		int16_t distanceBonus = 0;	 // Distance-based bonus/penalty (-50 to +50)
		int16_t chainBonus = 0;		 // Chain continuation bonus (+2000 if continuing chain)
		int16_t inProgressBonus = 0; // Bonus for current task (+200)
		int16_t taskAgeBonus = 0;	 // Bonus for old unclaimed tasks (0 to +100)

		// Stable tiebreak key for deterministic option ordering. When two options compute the
		// same calculatePriority() (e.g. two equidistant, equal-skill build sites), the sort
		// must not fall back to container iteration order: the goal registry is backed by
		// unordered containers, so that order is hash-bucket-dependent and would route colonists
		// differently across machines, desyncing a fixed-step multiplayer simulation. Each
		// evaluator fills this with the most stable id it has (goal id, station/entity id, etc.);
		// the comparator breaks priority ties on it for a deterministic total order.
		uint64_t tiebreakId = 0;

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
		/// Idle Wander's flat priority. Every actionable work/need option must beat this to keep a
		/// colonist from wandering when there is real work to do.
		static constexpr float kWanderPriority = 10.0F;

		/// Proactive Gather Food's flat priority (Tier 6 in ai-behavior.md). Sits ABOVE Wander and
		/// BELOW every real work type (Build/Craft/Haul/Harvest/PlacePackaged) and storage stocking:
		/// stockpiling food is idle filler done only when no real work exists, never a reason to skip
		/// or abandon a build/craft job. Flat, with no bonuses, so it cannot be inflated past real work.
		static constexpr float kGatherFoodPriority = 12.0F;

		/// Floor for work that provisions or performs an active work order (a Craft/Build child
		/// Haul/Harvest, or the Build action itself). The distance penalty (down to -50) would
		/// otherwise push a far material source or build site below kWanderPriority, so the colonist
		/// would abandon started work and wander. Flooring here keeps that work above idle without
		/// lifting it over needs or other work.
		static constexpr float kWorkOrderProvisionFloor = 20.0F;

		/// Floor for storage-stocking work (a Haul/Harvest driven by a storage container's unfilled
		/// minimum). Stocking must stay above idle Wander but STRICTLY below kWorkOrderProvisionFloor
		/// so a colonist always prefers construction/crafting work (and provisioning it) over filling
		/// a box, and only stocks when no such work is available. Sits between kWanderPriority (10)
		/// and kWorkOrderProvisionFloor (20).
		static constexpr float kStorageStockingFloor = 15.0F;

		[[nodiscard]] float calculatePriority() const {
			// Helper to compute total bonus for work tasks
			auto workBonus = [this]() {
				return static_cast<float>(distanceBonus + skillBonus + chainBonus + inProgressBonus + taskAgeBonus);
			};

			// Committed work (provisioning or performing an active work order, or stocking a
			// storage box) is floored above idle Wander so a far source/site can't strand it. The
			// floor only lifts the value when the distance penalty has dragged it down; nearer
			// targets still rank higher, and needs/other work (all >= 36 base) stay above it.
			// Two distinct floors enforce the ordering work-order (20) > stocking (15) > Wander (10):
			// a build/craft job and its provisioning always outrank stocking a container.
			auto floorIfCommitted = [this](float p) {
				if (servesActiveWorkOrder) {
					return std::max(p, kWorkOrderProvisionFloor);
				}
				if (servesStorageStocking) {
					return std::max(p, kStorageStockingFloor);
				}
				return p;
			};

			// A work task keeps its full tier priority whether it's merely Available or already
			// Selected (the in-progress one). Without this the selected task would fall through
			// to 0 below, corrupting task.priority and the switch-threshold gap against Wander.
			const bool actionable = (status == OptionStatus::Available || status == OptionStatus::Selected);

			// Tier 3: Critical needs get highest priority (300-310)
			// Needs only apply distance bonus (skill, chain, age bonuses don't apply to survival needs)
			if (needValue < 10.0F && status != OptionStatus::Satisfied) {
				return 300.0F + (10.0F - needValue) + static_cast<float>(distanceBonus);
			}
			// Tier 5: Actionable needs (100-150ish based on urgency)
			if (needValue < threshold && status != OptionStatus::Satisfied) {
				return 100.0F + (threshold - needValue) + static_cast<float>(distanceBonus);
			}
			// Tier 6 (design): proactive Gather Food. The gather-food option reuses FulfillNeed with
			// needValue==100 && threshold==0 (a sentinel: a real hunger need always has needValue<100
			// and a positive threshold). Per ai-behavior.md this is the "all needs comfortable, nothing
			// else to do" idle activity that sits directly ABOVE Wander and BELOW every real work type
			// (Build 41, Craft 40, Haul 37, Harvest 36, PlacePackaged 38). It is a flat priority with NO
			// bonuses: bonuses (especially the +200 in-progress bonus) would otherwise lift idle food
			// stockpiling above committed construction/crafting once selected, so a colonist with a full
			// belly but empty pockets would gather food forever and never build. The option already
			// targets the nearest edible source, so it needs no distance term to choose well.
			if (taskType == TaskType::FulfillNeed && needValue >= 100.0F && threshold == 0.0F && actionable) {
				return kGatherFoodPriority;
			}
			// Placing packaged items at target locations (priority 38 + distance/in-progress/chain)
			// If colonist is already carrying (needValue > 100), use needValue directly
			// as priority (typically 150) to ensure delivery completes before other tasks
			if (taskType == TaskType::PlacePackaged && actionable) {
				if (needValue > 100.0F) {
					// In-progress delivery - use high priority plus bonuses
					// Note: taskAgeBonus excluded since it's already claimed (in progress)
					return needValue + static_cast<float>(distanceBonus + chainBonus + inProgressBonus);
				}
				return 38.0F + static_cast<float>(distanceBonus + chainBonus + inProgressBonus + taskAgeBonus);
			}
			// Tier 6.4: Hauling loose items to storage - priority 37 + bonuses (no skill bonus).
			// A storage-stocking haul (chop-then-carry-in to fill a box) sits one point lower at 36
			// so that at equal distance a craft/construction provisioning haul always edges it out:
			// stocking ranks strictly below work-order provisioning. (The distinct floors below
			// enforce the same ordering when distance has dragged both down.)
			if (taskType == TaskType::Haul && actionable) {
				const float haulBase = servesStorageStocking ? 36.0F : 37.0F;
				return floorIfCommitted(haulBase + static_cast<float>(distanceBonus + chainBonus + inProgressBonus + taskAgeBonus));
			}
			// Tier 6.45: Construction build work - priority 41 + all bonuses. Sits just above
			// crafting (40) so staged build sites get finished; Construction skill feeds workBonus.
			// Floored above Wander when it serves the work order: a fully-provisioned blueprint is
			// committed work, but a far site's distance penalty (down to -50) would otherwise drag
			// 41 below Wander (10) for a low-skill colonist, who then abandons the ready build and
			// wanders off (the foundation-abandonment bug). Flooring pulls him back to finish it.
			if (taskType == TaskType::Build && actionable) {
				return floorIfCommitted(41.0F + workBonus());
			}
			// Tier 6.45: Deconstruct work - same priority as Build (both are Construction work);
			// Construction skill feeds workBonus identically. Floored above Wander on the same
			// committed-work basis as Build (a demolish job in progress shouldn't lose to idle
			// because the site is far).
			if (taskType == TaskType::Deconstruct && actionable) {
				return floorIfCommitted(41.0F + workBonus());
			}
			// Tier 6.5: Crafting work - priority 40 + all bonuses
			if (taskType == TaskType::Craft && actionable) {
				return 40.0F + workBonus();
			}
			// Tier 6.7: Harvesting resources (cutting trees, etc.) - priority 36 + all bonuses.
			// A storage-stocking harvest (chopping to fill a box) sits one point lower at 35 so that
			// at equal distance/skill a craft/construction provisioning harvest always edges it out:
			// stocking ranks strictly below work-order provisioning.
			if (taskType == TaskType::Harvest && actionable) {
				const float harvestBase = servesStorageStocking ? 35.0F : 36.0F;
				return floorIfCommitted(harvestBase + workBonus());
			}
			// Tier 7: Wander (lowest priority among active options - no bonuses)
			if (taskType == TaskType::Wander) {
				return kWanderPriority;
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
