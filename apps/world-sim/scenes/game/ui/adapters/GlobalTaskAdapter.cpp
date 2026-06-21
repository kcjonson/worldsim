#include "GlobalTaskAdapter.h"

#include <assets/AssetRegistry.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>

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
				case ecs::TaskType::Build:
					return 4; // reads Cut -> Haul -> Build for a site
				case ecs::TaskType::PlacePackaged:
					return 5;
				case ecs::TaskType::Gather:
					return 6;
				case ecs::TaskType::Wander:
					return 7;
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
				case ecs::TaskType::Build:
					return "Build";
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

			// Build umbrella: the whole structure (its children are the Cut/Haul rows).
			if (goal.type == ecs::TaskType::Build) {
				return "Build structure";
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

		/// Name of a colonist actively working this goal, or "" if none. Goals are global
		/// (claimed implicitly when a colonist selects the matching task), so we recover the
		/// assignment by scanning colonist tasks for the goal they're servicing.
		std::string colonistWorkingGoal(ecs::World& world, const ecs::GoalTask& goal) {
			for (auto [entity, colonist, task] : world.view<ecs::Colonist, ecs::Task>()) {
				if (!task.isActive()) {
					continue;
				}
				bool match = false;
				switch (goal.type) {
					case ecs::TaskType::Harvest:
						match = (task.type == ecs::TaskType::Harvest && task.harvestGoalId == goal.id);
						break;
					case ecs::TaskType::Haul:
						match = (task.type == ecs::TaskType::Haul && task.haulGoalId == goal.id);
						break;
					case ecs::TaskType::Build:
						// The Build task carries the blueprint entity, which is the umbrella's
						// destination.
						match = (task.type == ecs::TaskType::Build &&
								 task.buildBlueprintEntityId == static_cast<uint64_t>(goal.destinationEntity));
						break;
					default:
						break;
				}
				if (match) {
					return colonist.name;
				}
			}
			return "";
		}

		/// Convert goal to display data
		GlobalTaskDisplayData goalToDisplayData(
			const ecs::GoalTaskRegistry& goalRegistry,
			const ecs::GoalTask&		  goal,
			const glm::vec2&			  referencePosition,
			ecs::World*					  world // optional: enables "who's working it" + build progress
		) {
			GlobalTaskDisplayData data;
			data.id = goal.id;

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

			// Who, if anyone, is actively working this goal right now.
			const std::string worker = (world != nullptr) ? colonistWorkingGoal(*world, goal) : std::string{};

			// Build umbrella: its material counters are unused (targetAmount is just a marker);
			// the real progress lives on the blueprint's workDone. Surface that plus the worker.
			if (goal.type == ecs::TaskType::Build) {
				int	 pct = 0;
				bool underConstruction = false;
				if (world != nullptr) {
					if (const auto* bp = world->getComponent<ecs::StructureBlueprint>(goal.destinationEntity)) {
						pct = static_cast<int>(bp->progress() * 100.0F);
						underConstruction = (bp->phase == ecs::StructureBlueprint::BuildPhase::UnderConstruction);
					}
				}
				if (!underConstruction) {
					data.status = "Needs materials";
					data.isBlocked = true;
				} else if (!worker.empty()) {
					data.status = "Building";
					data.statusDetail = std::format("{} ({}%)", worker, pct);
				} else {
					data.status = "Ready to build";
					data.statusDetail = std::format("{}%", pct);
				}
			}
			// Status based on GoalStatus (only goals with capacity reach here; callers filter
			// out completed ones).
			else if (goal.status == ecs::GoalStatus::Blocked) {
				data.status = "Blocked";
				data.statusDetail = std::format("{}/{} materials", goal.deliveredAmount, goal.targetAmount);
				data.isBlocked = true;
			} else if (goal.status == ecs::GoalStatus::WaitingForItems) {
				data.status = "Waiting for harvest";
				data.isBlocked = true;
			} else if (!worker.empty()) {
				// A colonist is actively servicing this goal (claimed implicitly).
				data.status = "Working";
				data.statusDetail = worker;
			} else if (data.distanceValue > 50.0F) {
				data.status = "Available";
				data.statusDetail = "far";
			} else {
				data.status = "Unassigned";
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

		// Cut/Haul/Build read as a chain per site; Craft/PlacePackaged round it out. Build is the
		// umbrella goal (the destination holder) so a site shows its build step, not just the
		// material children. Skip completed ones (no remaining capacity).
		for (ecs::TaskType type : {ecs::TaskType::Harvest, ecs::TaskType::Haul, ecs::TaskType::Build, ecs::TaskType::Craft,
								   ecs::TaskType::PlacePackaged}) {
			for (const auto* goal : registry.getGoalsOfType(type)) {
				if (goal->availableCapacity() > 0) {
					result.push_back(goalToDisplayData(registry, *goal, cameraCenter, &world));
				}
			}
		}

		return result;
	}

	std::vector<GlobalTaskDisplayData> getTasksForColonist(const glm::vec2& colonistPosition) {
		auto& registry = ecs::GoalTaskRegistry::Get();

		std::vector<GlobalTaskDisplayData> result;

		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Harvest)) {
			if (goal->availableCapacity() == 0) continue;
			result.push_back(goalToDisplayData(registry, *goal, colonistPosition, nullptr));
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Haul)) {
			if (goal->availableCapacity() == 0) continue;
			result.push_back(goalToDisplayData(registry, *goal, colonistPosition, nullptr));
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::Craft)) {
			if (goal->availableCapacity() == 0) continue;
			result.push_back(goalToDisplayData(registry, *goal, colonistPosition, nullptr));
		}
		for (const auto* goal : registry.getGoalsOfType(ecs::TaskType::PlacePackaged)) {
			if (goal->availableCapacity() == 0) continue;
			result.push_back(goalToDisplayData(registry, *goal, colonistPosition, nullptr));
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
