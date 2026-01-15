#include "GlobalTaskAdapter.h"

#include <assets/AssetRegistry.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/components/Colonist.h>

#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <sstream>

namespace world_sim::adapters {

	namespace {

		/// Get priority for task type (lower = higher priority in display)
		uint8_t getTaskTypePriority(ecs::TaskType type) {
			switch (type) {
				case ecs::TaskType::FulfillNeed:
					return 0; // Highest priority - active survival
				case ecs::TaskType::Harvest:
					return 1; // Harvesting for crafting
				case ecs::TaskType::Craft:
					return 2;
				case ecs::TaskType::Haul:
					return 3;
				case ecs::TaskType::PlacePackaged:
					return 4;
				case ecs::TaskType::Gather:
					return 5;
				case ecs::TaskType::Wander:
					return 6;
				case ecs::TaskType::None:
				default:
					return 255;
			}
		}

		/// Get task type display prefix (e.g., "Cut", "Haul")
		std::string getTaskTypePrefix(ecs::TaskType type) {
			switch (type) {
				case ecs::TaskType::Harvest:
					return "Cut"; // "Cut Tree", "Harvest Bush"
				case ecs::TaskType::Gather:
					return "Gather";
				case ecs::TaskType::Haul:
					return "Haul";
				case ecs::TaskType::Craft:
					return "Craft";
				case ecs::TaskType::PlacePackaged:
					return "Place";
				case ecs::TaskType::FulfillNeed:
					return "Use";
				case ecs::TaskType::Wander:
					return "Explore";
				case ecs::TaskType::None:
				default:
					return "";
			}
		}

		/// Get parent context string (e.g., "(for Axe)")
		std::string getParentContext(const ecs::GoalTaskRegistry& registry, const ecs::GoalTask& goal) {
			if (!goal.parentGoalId.has_value()) {
				return "";
			}

			const auto* parentGoal = registry.getGoal(goal.parentGoalId.value());
			if (parentGoal == nullptr || parentGoal->type != ecs::TaskType::Craft) {
				return "";
			}

			// For now, just show generic context - could be enhanced with recipe name
			return " (for crafting)";
		}

		/// Get display label for an asset from its defNameId
		std::string getAssetLabel(uint32_t defNameId) {
			auto&			   registry = engine::assets::AssetRegistry::Get();
			const std::string& defName = registry.getDefName(defNameId);
			if (defName.empty()) {
				return "Unknown";
			}

			const auto* def = registry.getDefinition(defName);
			if (def && !def->label.empty()) {
				return def->label;
			}

			// Fallback: use defName (strip prefix like "Flora_")
			size_t underscore = defName.find('_');
			if (underscore != std::string::npos && underscore + 1 < defName.size()) {
				return defName.substr(underscore + 1);
			}
			return defName;
		}

		/// Get colonist name from EntityID
		std::string getColonistName(ecs::World& world, ecs::EntityID colonistId) {
			auto* colonist = world.getComponent<ecs::Colonist>(colonistId);
			if (colonist != nullptr) {
				return colonist->name;
			}
			return "Unknown";
		}

		/// Build description for a goal based on type and accepted items
		std::string buildGoalDescription(const ecs::GoalTask& goal, const ecs::GoalTaskRegistry& goalRegistry) {
			std::string prefix = getTaskTypePrefix(goal.type);
			std::string context = getParentContext(goalRegistry, goal);

			// For Harvest goals, describe what's being harvested
			if (goal.type == ecs::TaskType::Harvest) {
				if (goal.yieldDefNameId != 0) {
					// Show what item this harvest yields (e.g., "Cut Tree" for Wood)
					return prefix + " for " + getAssetLabel(goal.yieldDefNameId) + context;
				}
				return prefix + context;
			}

			// For Haul goals, describe what's being hauled
			if (goal.type == ecs::TaskType::Haul) {
				if (!goal.acceptedDefNameIds.empty()) {
					// Specific item types (e.g., "Haul Wood")
					return prefix + " " + getAssetLabel(goal.acceptedDefNameIds[0]) + context;
				}
				if (goal.acceptedCategory != engine::assets::ItemCategory::None) {
					// Category-based storage
					std::string catName;
					switch (goal.acceptedCategory) {
						case engine::assets::ItemCategory::Food:
							catName = "Food";
							break;
						case engine::assets::ItemCategory::RawMaterial:
							catName = "Materials";
							break;
						case engine::assets::ItemCategory::Tool:
							catName = "Tools";
							break;
						case engine::assets::ItemCategory::Furniture:
							catName = "Furniture";
							break;
						default:
							catName = "Items";
					}
					return prefix + " " + catName + context;
				}
				return prefix + context;
			}

			// For Craft goals, show crafting
			if (goal.type == ecs::TaskType::Craft) {
				return "Craft";
			}

			// For PlacePackaged, show generic placement
			if (goal.type == ecs::TaskType::PlacePackaged) {
				return "Place Item";
			}

			// For destination-based goals, use the destination name
			if (goal.destinationDefNameId != 0) {
				return prefix + " " + getAssetLabel(goal.destinationDefNameId);
			}

			return prefix;
		}

		/// Convert goal to display data
		GlobalTaskDisplayData goalToDisplayData(
			ecs::World&					  world,
			const ecs::GoalTaskRegistry& goalRegistry,
			const ecs::GoalTask&		  goal,
			const glm::vec2&			  referencePosition
		) {
			GlobalTaskDisplayData data;
			data.id = goal.id;
			data.quantity = goal.targetAmount > 0 ? goal.targetAmount : 1;

			// Build description with parent context
			data.description = buildGoalDescription(goal, goalRegistry);

			// Format position (destination position)
			data.position = std::format("({}, {})",
										static_cast<int>(goal.destinationPosition.x),
										static_cast<int>(goal.destinationPosition.y));

			// Calculate distance to destination
			float dx = goal.destinationPosition.x - referencePosition.x;
			float dy = goal.destinationPosition.y - referencePosition.y;
			data.distanceValue = std::sqrt(dx * dx + dy * dy);
			data.distance = std::format("{}m", static_cast<int>(data.distanceValue));

			// Status based on GoalStatus and reservations
			size_t	 reservationCount = goal.itemReservations.size();
			uint32_t available = goal.availableCapacity();

			// First check goal status for blocking conditions
			if (goal.status == ecs::GoalStatus::Blocked) {
				data.status = "Blocked";
				data.statusDetail = std::format("{}/{} materials", goal.deliveredAmount, goal.targetAmount);
				data.isBlocked = true;
			} else if (goal.status == ecs::GoalStatus::WaitingForItems) {
				data.status = "Waiting for harvest";
				data.statusDetail = "";
				data.isBlocked = true;
			} else if (reservationCount > 0) {
				// Show who's working on it
				auto		it = goal.itemReservations.begin();
				std::string workerName = getColonistName(world, it->second);
				if (reservationCount == 1) {
					data.status = workerName;
					data.statusDetail = "working";
				} else {
					data.status = workerName;
					data.statusDetail = std::format("+ {} more", reservationCount - 1);
				}
				data.isReserved = true;
			} else if (available == 0) {
				data.status = "Complete";
				data.statusDetail = "";
			} else if (data.distanceValue > 50.0F) {
				data.status = "Available";
				data.statusDetail = "far";
			} else {
				data.status = "Unassigned";
				data.statusDetail = "";
				data.isUnassigned = true;
			}

			// Known by - for goals, all colonists who know items that can fulfill
			data.knownBy = ""; // Goal-driven: fulfilled by Memory queries, not tracked per-goal

			data.taskTypePriority = getTaskTypePriority(goal.type);

			return data;
		}

	} // anonymous namespace

	std::vector<GlobalTaskDisplayData> getGlobalTasks(ecs::World& world, const glm::vec2& cameraCenter) {
		auto& registry = ecs::GoalTaskRegistry::Get();

		std::vector<GlobalTaskDisplayData> result;

		// Get all goals (Harvest, Haul, Craft, PlacePackaged) - skip completed ones
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Harvest)) {
			if (goal->availableCapacity() > 0) {
				result.push_back(goalToDisplayData(world, registry, *goal, cameraCenter));
			}
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Haul)) {
			if (goal->availableCapacity() > 0) {
				result.push_back(goalToDisplayData(world, registry, *goal, cameraCenter));
			}
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Craft)) {
			if (goal->availableCapacity() > 0) {
				result.push_back(goalToDisplayData(world, registry, *goal, cameraCenter));
			}
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::PlacePackaged)) {
			if (goal->availableCapacity() > 0) {
				result.push_back(goalToDisplayData(world, registry, *goal, cameraCenter));
			}
		}

		return result;
	}

	std::vector<GlobalTaskDisplayData> getTasksForColonist(ecs::World& world, ecs::EntityID colonistId, const glm::vec2& colonistPosition) {
		auto& registry = ecs::GoalTaskRegistry::Get();

		std::vector<GlobalTaskDisplayData> result;

		// For colonist view, show goals where this colonist has a reservation
		auto checkReservation = [colonistId](const ecs::GoalTask& goal, GlobalTaskDisplayData& data) {
			for (const auto& [itemKey, reserver] : goal.itemReservations) {
				if (reserver == colonistId) {
					data.isMine = true;
					data.status = "In Progress";
					break;
				}
			}
		};

		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Harvest)) {
			if (goal->availableCapacity() == 0) continue;
			auto data = goalToDisplayData(world, registry, *goal, colonistPosition);
			checkReservation(*goal, data);
			result.push_back(data);
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Haul)) {
			if (goal->availableCapacity() == 0) continue;
			auto data = goalToDisplayData(world, registry, *goal, colonistPosition);
			checkReservation(*goal, data);
			result.push_back(data);
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Craft)) {
			if (goal->availableCapacity() == 0) continue;
			auto data = goalToDisplayData(world, registry, *goal, colonistPosition);
			checkReservation(*goal, data);
			result.push_back(data);
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::PlacePackaged)) {
			if (goal->availableCapacity() == 0) continue;
			auto data = goalToDisplayData(world, registry, *goal, colonistPosition);
			checkReservation(*goal, data);
			result.push_back(data);
		}

		return result;
	}

	void sortTasksForDisplay(std::vector<GlobalTaskDisplayData>& tasks) {
		std::sort(tasks.begin(), tasks.end(), [](const GlobalTaskDisplayData& a, const GlobalTaskDisplayData& b) {
			// 1. "Mine" tasks first (for colonist view)
			if (a.isMine != b.isMine) {
				return a.isMine;
			}

			// 2. Reserved tasks first
			if (a.isReserved != b.isReserved) {
				return a.isReserved;
			}

			// 3. By task type priority
			if (a.taskTypePriority != b.taskTypePriority) {
				return a.taskTypePriority < b.taskTypePriority;
			}

			// 4. By distance (closer first)
			if (std::abs(a.distanceValue - b.distanceValue) > 0.5F) {
				return a.distanceValue < b.distanceValue;
			}

			// 5. Stable sort by ID
			return a.id < b.id;
		});
	}

} // namespace world_sim::adapters
