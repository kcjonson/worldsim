#include "AIDecisionSystem.h"

#include "../World.h"
#include "../components/DecisionTrace.h"
#include "../components/Memory.h"
#include "../components/MemoryQueries.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"

#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace ecs {

	namespace {

		/// Map NeedType to the CapabilityType that fulfills it
		[[nodiscard]] engine::assets::CapabilityType needToCapability(NeedType need) {
			switch (need) {
				case NeedType::Hunger:
					return engine::assets::CapabilityType::Edible;
				case NeedType::Thirst:
					return engine::assets::CapabilityType::Drinkable;
				case NeedType::Energy:
					return engine::assets::CapabilityType::Sleepable;
				case NeedType::Bladder:
					return engine::assets::CapabilityType::Toilet;
				case NeedType::Count:
					break;
			}
			// All valid NeedTypes must be handled above - hitting this is a bug
			LOG_ERROR(Engine, "needToCapability: unhandled NeedType %d", static_cast<int>(need));
			return engine::assets::CapabilityType::Edible;
		}

		/// Get a human-readable name for a need type (for debug logging)
		[[nodiscard]] const char* needTypeName(NeedType need) {
			switch (need) {
				case NeedType::Hunger:
					return "Hunger";
				case NeedType::Thirst:
					return "Thirst";
				case NeedType::Energy:
					return "Energy";
				case NeedType::Bladder:
					return "Bladder";
				case NeedType::Count:
					break;
			}
			// All valid NeedTypes must be handled above - hitting this is a bug
			LOG_ERROR(Engine, "needTypeName: unhandled NeedType %d", static_cast<int>(need));
			return "Unknown";
		}

	} // namespace

	AIDecisionSystem::AIDecisionSystem(const engine::assets::AssetRegistry& registry, std::optional<uint32_t> rngSeed)
		: m_registry(registry),
		  m_rng(rngSeed.value_or(std::random_device{}())) {}

	void AIDecisionSystem::update(float deltaTime) {
		// Process all entities with the required components
		for (auto [entity, position, needs, memory, task, movementTarget] :
			 world->view<Position, NeedsComponent, Memory, Task, MovementTarget>()) {

			// Check if we should re-evaluate (uses current timer value)
			if (!shouldReEvaluate(task, needs)) {
				// Only increment timer when NOT re-evaluating (timer tracks time since last eval)
				task.timeSinceEvaluation += deltaTime;
				continue;
			}

			// Clear current task and evaluate from scratch
			task.clear();
			task.timeSinceEvaluation = 0.0F; // Reset timer after re-evaluation

			// Check if entity has DecisionTrace component for trace-based selection
			auto* trace = world->getComponent<DecisionTrace>(entity);
			if (trace != nullptr) {
				// Build full decision trace and select from it
				buildDecisionTrace(entity, position, needs, memory, *trace);
				selectTaskFromTrace(task, movementTarget, *trace, position);

				LOG_INFO(
					Engine,
					"[AI] Entity %llu: %s → (%.1f, %.1f)",
					static_cast<unsigned long long>(entity),
					task.reason.c_str(),
					task.targetPosition.x,
					task.targetPosition.y
				);
				continue;
			}

			// Fallback: Legacy tier-by-tier evaluation for entities without DecisionTrace
			// Helper to check if task uses ground fallback (target == current position)
			auto isGroundFallback = [&position](const Task& t) {
				return t.targetPosition == position.value;
			};

			// Tier 3: Critical needs (highest priority)
			if (evaluateCriticalNeeds(entity, needs, memory, task, position)) {
				movementTarget.target = task.targetPosition;
				// Ground fallback: already at target, set Arrived to avoid infinite re-eval loop
				if (isGroundFallback(task)) {
					movementTarget.active = false;
					task.state = TaskState::Arrived;
				} else {
					movementTarget.active = true;
					task.state = TaskState::Moving;
				}
				LOG_INFO(
					Engine,
					"[AI] Entity %llu: CRITICAL NEED - %s → (%.1f, %.1f)",
					static_cast<unsigned long long>(entity),
					task.reason.c_str(),
					task.targetPosition.x,
					task.targetPosition.y
				);
				continue;
			}

			// Tier 5: Actionable needs
			if (evaluateActionableNeeds(entity, needs, memory, task, position)) {
				movementTarget.target = task.targetPosition;
				// Ground fallback: already at target, set Arrived to avoid infinite re-eval loop
				if (isGroundFallback(task)) {
					movementTarget.active = false;
					task.state = TaskState::Arrived;
				} else {
					movementTarget.active = true;
					task.state = TaskState::Moving;
				}
				LOG_INFO(
					Engine,
					"[AI] Entity %llu: NEED - %s → (%.1f, %.1f)",
					static_cast<unsigned long long>(entity),
					task.reason.c_str(),
					task.targetPosition.x,
					task.targetPosition.y
				);
				continue;
			}

			// Tier 7: Wander (lowest priority)
			assignWander(entity, task, position);
			movementTarget.target = task.targetPosition;
			movementTarget.active = true;
			task.state = TaskState::Moving;
			LOG_INFO(
				Engine,
				"[AI] Entity %llu: WANDER → (%.1f, %.1f)",
				static_cast<unsigned long long>(entity),
				task.targetPosition.x,
				task.targetPosition.y
			);
		}
	}

	bool AIDecisionSystem::shouldReEvaluate(const Task& task, const NeedsComponent& needs) {
		// Always re-evaluate if no active task
		if (!task.isActive()) {
			return true;
		}

		// Re-evaluate if task has arrived (completed movement)
		if (task.state == TaskState::Arrived) {
			return true;
		}

		// Re-evaluate periodically
		if (task.timeSinceEvaluation >= kReEvalInterval) {
			return true;
		}

		// Check if any critical need requires immediate attention (Tier 3 interrupts all lower tiers)
		// This must be checked BEFORE the "don't interrupt" logic below
		bool hasCriticalNeed = false;
		for (size_t i = 0; i < static_cast<size_t>(NeedType::Count); ++i) {
			if (needs.get(static_cast<NeedType>(i)).isCritical()) {
				hasCriticalNeed = true;
				break;
			}
		}

		if (hasCriticalNeed) {
			// If already handling a critical need, don't interrupt for other critical needs
			if (task.type == TaskType::FulfillNeed) {
				const auto& currentNeed = needs.get(task.needToFulfill);
				if (currentNeed.isCritical()) {
					return false;
				}
			}
			// Critical need interrupts non-critical tasks and wander
			return true;
		}

		// No critical needs - don't interrupt wander while moving
		// Wandering gives the colonist a chance to discover new sources (water, food)
		if (task.type == TaskType::Wander && task.state == TaskState::Moving) {
			return false;
		}

		return false;
	}

	bool AIDecisionSystem::evaluateCriticalNeeds(
		EntityID /*entity*/,
		const NeedsComponent& needs,
		const Memory&		  memory,
		Task&				  task,
		const Position&		  position
	) {
		// Find the most critical need (lowest value among all critical needs)
		NeedType mostCritical = NeedType::Count;
		float	 lowestValue = 100.0F;

		for (size_t i = 0; i < static_cast<size_t>(NeedType::Count); ++i) {
			auto		needType = static_cast<NeedType>(i);
			const auto& need = needs.get(needType);

			if (need.isCritical() && need.value < lowestValue) {
				mostCritical = needType;
				lowestValue = need.value;
			}
		}

		if (mostCritical == NeedType::Count) {
			return false; // No critical needs
		}

		const auto& need = needs.get(mostCritical);

		// Find nearest entity that can fulfill this need
		auto capability = needToCapability(mostCritical);
		auto nearest = findNearestWithCapability(memory, m_registry, capability, position.value);

		if (nearest.has_value()) {
			task.type = TaskType::FulfillNeed;
			task.needToFulfill = mostCritical;
			task.targetPosition = nearest->position;
			task.reason = std::string(needTypeName(mostCritical)) + " CRITICAL at " + std::to_string(static_cast<int>(need.value)) + "%";
			return true;
		}

		// For Energy and Bladder, use ground as fallback (current position)
		if (mostCritical == NeedType::Energy || mostCritical == NeedType::Bladder) {
			task.type = TaskType::FulfillNeed;
			task.needToFulfill = mostCritical;
			task.targetPosition = position.value; // Use current position (ground)
			task.reason = std::string(needTypeName(mostCritical)) + " CRITICAL - using ground at " +
						  std::to_string(static_cast<int>(need.value)) + "%";
			return true;
		}

		return false;
	}

	bool AIDecisionSystem::evaluateActionableNeeds(
		EntityID /*entity*/,
		const NeedsComponent& needs,
		const Memory&		  memory,
		Task&				  task,
		const Position&		  position
	) {
		// Find the most urgent need that needs attention
		NeedType urgentNeed = needs.mostUrgentNeed();

		if (urgentNeed == NeedType::Count) {
			return false; // No needs require attention
		}

		const auto& need = needs.get(urgentNeed);

		// Find nearest entity that can fulfill this need
		auto capability = needToCapability(urgentNeed);
		auto nearest = findNearestWithCapability(memory, m_registry, capability, position.value);

		if (nearest.has_value()) {
			task.type = TaskType::FulfillNeed;
			task.needToFulfill = urgentNeed;
			task.targetPosition = nearest->position;
			task.reason = std::string(needTypeName(urgentNeed)) + " at " + std::to_string(static_cast<int>(need.value)) + "%";
			return true;
		}

		// For Energy and Bladder, use ground as fallback (current position)
		if (urgentNeed == NeedType::Energy || urgentNeed == NeedType::Bladder) {
			task.type = TaskType::FulfillNeed;
			task.needToFulfill = urgentNeed;
			task.targetPosition = position.value; // Use current position (ground)
			task.reason =
				std::string(needTypeName(urgentNeed)) + " - using ground at " + std::to_string(static_cast<int>(need.value)) + "%";
			return true;
		}

		// Could not find fulfillment source, fall through to wander
		return false;
	}

	void AIDecisionSystem::assignWander(EntityID /*entity*/, Task& task, const Position& position) {
		task.type = TaskType::Wander;
		task.targetPosition = generateWanderTarget(position.value);
		task.reason = "Wandering (all needs satisfied)";
	}

	glm::vec2 AIDecisionSystem::generateWanderTarget(const glm::vec2& currentPos) {
		// Generate random angle and distance
		std::uniform_real_distribution<float> angleDist(0.0F, 2.0F * std::numbers::pi_v<float>);
		std::uniform_real_distribution<float> distDist(kWanderRadius * 0.3F, kWanderRadius);

		float angle = angleDist(m_rng);
		float distance = distDist(m_rng);

		return currentPos + glm::vec2{std::cos(angle) * distance, std::sin(angle) * distance};
	}

	void AIDecisionSystem::buildDecisionTrace(
		EntityID /*entity*/,
		const Position&		  position,
		const NeedsComponent& needs,
		const Memory&		  memory,
		DecisionTrace&		  trace
	) {
		trace.clear();

		// Evaluate each need type
		for (size_t i = 0; i < static_cast<size_t>(NeedType::Count); ++i) {
			auto		needType = static_cast<NeedType>(i);
			const auto& need = needs.get(needType);

			EvaluatedOption option;
			option.taskType = TaskType::FulfillNeed;
			option.needType = needType;
			option.needValue = need.value;
			option.threshold = need.seekThreshold;

			// Check memory for fulfillment source
			auto capability = needToCapability(needType);
			auto nearest = findNearestWithCapability(memory, m_registry, capability, position.value);

			if (nearest.has_value()) {
				option.targetPosition = nearest->position;
				option.targetDefNameId = nearest->defNameId;
				option.distanceToTarget = glm::distance(position.value, nearest->position);

				if (need.needsAttention()) {
					option.status = OptionStatus::Available;
				} else {
					option.status = OptionStatus::Satisfied;
				}
			} else if (needType == NeedType::Energy || needType == NeedType::Bladder) {
				// Ground fallback for sleep and toilet
				option.targetPosition = position.value;
				option.distanceToTarget = 0.0F;
				option.status = need.needsAttention() ? OptionStatus::Available : OptionStatus::Satisfied;
			} else {
				// No source and no fallback
				option.status = need.needsAttention() ? OptionStatus::NoSource : OptionStatus::Satisfied;
			}

			option.reason = formatOptionReason(option, needTypeName(needType));
			trace.options.push_back(option);
		}

		// Add wander option
		EvaluatedOption wanderOption;
		wanderOption.taskType = TaskType::Wander;
		wanderOption.needType = NeedType::Count; // N/A
		wanderOption.status = OptionStatus::Available;
		wanderOption.reason = "All needs satisfied";
		wanderOption.targetPosition = generateWanderTarget(position.value);
		trace.options.push_back(wanderOption);

		// Sort by priority (highest first)
		std::sort(trace.options.begin(), trace.options.end(), [](const auto& a, const auto& b) {
			return a.calculatePriority() > b.calculatePriority();
		});

		// Mark the first actionable option as Selected
		for (auto& option : trace.options) {
			if (option.status == OptionStatus::Available) {
				option.status = OptionStatus::Selected;
				trace.selectionSummary = "Selected: " + option.reason;
				break;
			}
		}
	}

	void AIDecisionSystem::selectTaskFromTrace(
		Task&				   task,
		MovementTarget&		   movementTarget,
		const DecisionTrace&   trace,
		const Position&		   position
	) {
		const auto* selected = trace.getSelected();
		if (selected == nullptr) {
			// No actionable option - shouldn't happen, but fallback to wander
			task.type = TaskType::Wander;
			task.reason = "No actionable options";
			return;
		}

		task.type = selected->taskType;
		task.needToFulfill = selected->needType;
		task.targetPosition = selected->targetPosition.value_or(position.value);
		task.reason = selected->reason;

		movementTarget.target = task.targetPosition;

		// Check if ground fallback (already at target)
		bool isGroundFallback = (task.targetPosition == position.value);
		if (isGroundFallback) {
			movementTarget.active = false;
			task.state = TaskState::Arrived;
		} else {
			movementTarget.active = true;
			task.state = TaskState::Moving;
		}
	}

	std::string AIDecisionSystem::formatOptionReason(const EvaluatedOption& option, const char* needName) {
		if (option.taskType == TaskType::Wander) {
			return "All needs satisfied";
		}

		std::string reason = needName;
		reason += " at " + std::to_string(static_cast<int>(option.needValue)) + "%";

		if (option.status == OptionStatus::NoSource) {
			reason += " (no known source)";
		} else if (option.needValue < 10.0F) {
			reason += " (critical)";
		} else if (option.status == OptionStatus::Available || option.status == OptionStatus::Selected) {
			if (option.distanceToTarget > 0.0F) {
				reason += " (" + std::to_string(static_cast<int>(option.distanceToTarget)) + "m away)";
			} else if (option.targetPosition.has_value()) {
				reason += " (using ground)";
			}
		} else if (option.status == OptionStatus::Satisfied) {
			reason += " (satisfied)";
		}

		return reason;
	}

} // namespace ecs
