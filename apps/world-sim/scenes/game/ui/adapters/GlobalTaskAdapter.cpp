#include "GlobalTaskAdapter.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeRegistry.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>
#include <ecs/components/WorkQueue.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <optional>
#include <sstream>
#include <unordered_map>

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
			auto&			   registry = assets::AssetRegistry::Get();
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
				if (goal.acceptedCategory != assets::ItemCategory::None) {
					// Category-based storage
					std::string catName;
					switch (goal.acceptedCategory) {
						case assets::ItemCategory::Food:
							catName = "Food";
							break;
						case assets::ItemCategory::RawMaterial:
							catName = "Materials";
							break;
						case assets::ItemCategory::Tool:
							catName = "Tools";
							break;
						case assets::ItemCategory::Furniture:
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

		/// Who works each goal row right now, from each colonist's current task, built once per refresh
		/// so a row's worker is one lookup. A cut or a haul names the goal it was picked through (a carry-in
		/// and a cut's own deposit leg name their root); a build, a craft, or a placement names the entity
		/// it works. A suspended record is not current work, so a colonist seeing to a need works nothing.
		struct Workers {
			std::unordered_map<uint64_t, ecs::EntityID> byGoal;
			std::unordered_map<uint64_t, ecs::EntityID> byEntity;
		};

		Workers workersFromTasks(ecs::World& world) {
			Workers workers;
			for (auto [entity, colonist, task] : world.view<ecs::Colonist, ecs::Task>()) {
				(void)colonist;
				if (!task.isActive()) {
					continue;
				}
				switch (task.type) {
					case ecs::TaskType::Harvest:
						workers.byGoal.try_emplace(task.harvestGoalId, entity);
						break;
					case ecs::TaskType::Haul:
						workers.byGoal.try_emplace(task.haulGoalId, entity);
						break;
					case ecs::TaskType::Build:
					case ecs::TaskType::Deconstruct:
						workers.byEntity.try_emplace(task.buildBlueprintEntityId, entity);
						break;
					case ecs::TaskType::Craft:
						workers.byEntity.try_emplace(task.targetStationId, entity);
						break;
					case ecs::TaskType::PlacePackaged:
						workers.byEntity.try_emplace(task.placePackagedEntityId, entity);
						break;
					case ecs::TaskType::FulfillNeed:
					case ecs::TaskType::Wander:
					case ecs::TaskType::None:
						break;
				}
			}
			return workers;
		}

		/// Name of the colonist whose current task serves this row, or "". A Build, Craft, or placement
		/// row is worked by whoever works its entity (the builder, the crafter) and otherwise by whoever
		/// delivers toward it; every other row by whoever picked it.
		std::string colonistWorkingGoal(ecs::World& world, const Workers& workers, const ecs::GoalTask& goal) {
			std::optional<ecs::EntityID> worker;
			const bool					 worksEntity =
				goal.type == ecs::TaskType::Build || goal.type == ecs::TaskType::Craft || goal.type == ecs::TaskType::PlacePackaged;
			if (const auto it = workers.byEntity.find(static_cast<uint64_t>(goal.destinationEntity)); worksEntity && it != workers.byEntity.end()) {
				worker = it->second;
			} else if (const auto byGoal = workers.byGoal.find(goal.id); byGoal != workers.byGoal.end()) {
				worker = byGoal->second;
			}
			const auto* colonist = worker ? world.getComponent<ecs::Colonist>(*worker) : nullptr;
			return colonist != nullptr ? colonist->name : std::string{};
		}

		/// A Craft goal's materials as {landed, required} for its station's next job: the recipe's total
		/// inputs, and how many of them the root no longer needs.
		std::pair<uint32_t, uint32_t> craftMaterials(ecs::World& world, const ecs::GoalTask& goal) {
			const auto*				 queue = world.getComponent<ecs::WorkQueue>(goal.destinationEntity);
			const ecs::CraftingJob* job = queue != nullptr ? queue->getNextJob() : nullptr;
			const auto*				 recipe = job != nullptr ? assets::RecipeRegistry::Get().getRecipe(job->recipeDefName) : nullptr;
			if (recipe == nullptr) {
				return {0, 0};
			}
			const auto& registry = ecs::GoalTaskRegistry::Get();
			uint32_t	required = 0;
			uint32_t	missing = 0;
			for (const auto& input : recipe->inputs) {
				required += input.count;
				missing += registry.remainingNeed(
					world, goal.id, assets::AssetRegistry::Get().getDefNameId(input.defName), ecs::NeedBasis::Stock
				);
			}
			return {required - std::min(missing, required), required};
		}

		/// Convert goal to display data
		GlobalTaskDisplayData goalToDisplayData(
			const ecs::GoalTaskRegistry& goalRegistry,
			const ecs::GoalTask&		  goal,
			const glm::vec2&			  referencePosition,
			ecs::World*					  world, // optional: enables "who's working it" + build progress
			const Workers*				  workers
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
			const std::string worker = (world != nullptr && workers != nullptr) ? colonistWorkingGoal(*world, *workers, goal) : std::string{};

			// Build umbrella: its progress lives on the blueprint's workDone. Surface that plus the worker.
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
					data.statusDetail = worker; // whoever is delivering to the site
					data.isBlocked = true;
				} else if (!worker.empty()) {
					data.status = "Building";
					data.statusDetail = std::format("{} ({}%)", worker, pct);
				} else {
					data.status = "Ready to build";
					data.statusDetail = std::format("{}%", pct);
				}
			}
			// Status based on GoalStatus. A goal exists only while there is work toward it.
			else if (goal.status == ecs::GoalStatus::Blocked) {
				data.status = "Blocked";
				if (world != nullptr && goal.type == ecs::TaskType::Craft) {
					const auto [landed, required] = craftMaterials(*world, goal);
					data.statusDetail = std::format("{}/{} materials", landed, required);
				}
				data.isBlocked = true;
			} else if (goal.status == ecs::GoalStatus::NoSource) {
				data.status = "Waiting (no source found)";
				data.isBlocked = true;
			} else if (!worker.empty()) {
				// A colonist holds claims toward this goal.
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

		/// Every goal row with something to do, Cut/Haul/Build reading as a chain per site and
		/// Craft/PlacePackaged rounding it out. Build is the umbrella (the destination holder), so a site
		/// shows its build step, not just its material children. A full box keeps its umbrella and is left
		/// out until it wants something again.
		std::vector<GlobalTaskDisplayData> goalsForDisplay(ecs::World& world, const glm::vec2& referencePosition) {
			auto&							   registry = ecs::GoalTaskRegistry::Get();
			std::vector<GlobalTaskDisplayData> result;
			const Workers					   workers = workersFromTasks(world);
			for (ecs::TaskType type :
				 {ecs::TaskType::Harvest, ecs::TaskType::Haul, ecs::TaskType::Build, ecs::TaskType::Craft, ecs::TaskType::PlacePackaged}) {
				for (const auto* goal : registry.getGoalsOfType(type)) {
					if (ecs::goalHasWork(world, *goal)) {
						result.push_back(goalToDisplayData(registry, *goal, referencePosition, &world, &workers));
					}
				}
			}
			return result;
		}

	} // anonymous namespace

	std::vector<GlobalTaskDisplayData> getGlobalTasks(ecs::World& world, const glm::vec2& cameraCenter) {
		return goalsForDisplay(world, cameraCenter);
	}

	std::vector<GlobalTaskDisplayData> getTasksForColonist(ecs::World& world, const glm::vec2& colonistPosition) {
		return goalsForDisplay(world, colonistPosition);
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
