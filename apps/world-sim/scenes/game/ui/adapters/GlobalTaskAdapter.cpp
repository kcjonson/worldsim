#include "GlobalTaskAdapter.h"
#include "TaskRowText.h"

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
#include <span>
#include <string>
#include <vector>

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

		/// The colonists' names, joined for a row ("Alice, Bob +2").
		std::string colonistNames(ecs::World& world, std::span<const ecs::EntityID> colonists) {
			std::vector<std::string> names;
			names.reserve(colonists.size());
			for (const ecs::EntityID entity : colonists) {
				if (const auto* colonist = world.getComponent<ecs::Colonist>(entity)) {
					names.push_back(colonist->name);
				}
			}
			return joinNames(names);
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
			ecs::World&					 world,
			const ecs::GoalTaskRegistry& goalRegistry,
			const ecs::ColonyGoalWork&	 work,
			const ecs::GoalTask&		 goal,
			const glm::vec2&			 referencePosition
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

			// Who, if anyone, is working this goal right now.
			const std::string workers = colonistNames(world, work.workersOf(goal));

			// The status's own detail, then what the root still needs of the item, then who knows of work
			// toward it when nobody works it; the parts are joined with "; ".
			std::string detail;
			// Build umbrella: its progress lives on the blueprint's workDone. Surface that plus the workers.
			if (goal.type == ecs::TaskType::Build) {
				int	 pct = 0;
				bool underConstruction = false;
				if (const auto* bp = world.getComponent<ecs::StructureBlueprint>(goal.destinationEntity)) {
					pct = static_cast<int>(bp->progress() * 100.0F);
					underConstruction = (bp->phase == ecs::StructureBlueprint::BuildPhase::UnderConstruction);
				}
				if (!underConstruction) {
					data.status = "Needs materials";
					detail = workers; // whoever is delivering to the site
					data.isBlocked = true;
				} else if (!workers.empty()) {
					data.status = "Building";
					detail = std::format("{} ({}%)", workers, pct);
				} else {
					data.status = "Ready to build";
					detail = std::format("{}%", pct);
				}
			}
			// Status based on GoalStatus. A goal exists only while there is work toward it.
			else if (goal.status == ecs::GoalStatus::Blocked) {
				data.status = "Blocked";
				if (goal.type == ecs::TaskType::Craft) {
					const auto [landed, required] = craftMaterials(world, goal);
					detail = std::format("{}/{} materials", landed, required);
				}
				data.isBlocked = true;
			} else if (goal.status == ecs::GoalStatus::NoSource) {
				data.status = "Waiting (no source found)";
				data.isBlocked = true;
			} else if (!workers.empty()) {
				data.status = "Working";
				detail = workers;
			} else if (work.allSourcesClaimed(goal.id)) {
				data.status = "Waiting (all known sources claimed)";
				data.isBlocked = true;
			} else if (data.distanceValue > 50.0F) {
				data.status = "Available";
				detail = "far";
			} else {
				data.status = "Unassigned";
				data.isUnassigned = true;
			}

			std::vector<std::string> parts;
			if (!detail.empty()) {
				parts.push_back(std::move(detail));
			}
			if (const auto count = work.itemCount(world, goal)) {
				if (std::string quantity = quantityText(count->needed, count->onTheWay); !quantity.empty()) {
					parts.push_back(std::move(quantity));
				}
			}
			if (workers.empty()) {
				if (const std::string known = colonistNames(world, work.offeredTo(goal.id)); !known.empty()) {
					parts.push_back("known to " + known);
				}
			}
			for (const std::string& part : parts) {
				data.statusDetail += data.statusDetail.empty() ? part : "; " + part;
			}

			data.taskTypePriority = getTaskTypePriority(goal.type);

			return data;
		}

		/// Every goal row with something to do, Cut/Haul/Build reading as a chain per site and
		/// Craft/PlacePackaged rounding it out. Build is the umbrella (the destination holder), so a site
		/// shows its build step, not just its material children. A box keeps its umbrella while it has rules
		/// and is left out until some colonist is offered work toward it. With `knownTo`, only the goals
		/// that colonist works or its last decision offered work toward.
		std::vector<GlobalTaskDisplayData>
		goalsForDisplay(ecs::World& world, const glm::vec2& referencePosition, std::optional<ecs::EntityID> knownTo) {
			auto&							   registry = ecs::GoalTaskRegistry::Get();
			std::vector<GlobalTaskDisplayData> result;
			const ecs::ColonyGoalWork		   work(world);
			for (ecs::TaskType type :
				 {ecs::TaskType::Harvest, ecs::TaskType::Haul, ecs::TaskType::Build, ecs::TaskType::Craft, ecs::TaskType::PlacePackaged}) {
				for (const auto* goal : registry.getGoalsOfType(type)) {
					const auto workers = work.workersOf(*goal);
					const bool works = knownTo.has_value() && std::ranges::find(workers, *knownTo) != workers.end();
					if (!works && !work.hasWork(*goal)) {
						continue;
					}
					const auto offered = work.offeredTo(goal->id);
					if (knownTo.has_value() && !works && std::ranges::find(offered, *knownTo) == offered.end()) {
						continue;
					}
					result.push_back(goalToDisplayData(world, registry, work, *goal, referencePosition));
				}
			}
			return result;
		}

	} // anonymous namespace

	std::vector<GlobalTaskDisplayData> getGlobalTasks(ecs::World& world, const glm::vec2& referencePosition) {
		return goalsForDisplay(world, referencePosition, std::nullopt);
	}

	std::vector<GlobalTaskDisplayData> getKnownTasks(ecs::World& world, ecs::EntityID colonist, const glm::vec2& referencePosition) {
		return goalsForDisplay(world, referencePosition, colonist);
	}

	void sortTasksForDisplay(std::vector<GlobalTaskDisplayData>& tasks) {
		std::sort(tasks.begin(), tasks.end(), [](const GlobalTaskDisplayData& a, const GlobalTaskDisplayData& b) {
			// 1. By task type priority
			if (a.taskTypePriority != b.taskTypePriority) {
				return a.taskTypePriority < b.taskTypePriority;
			}

			// 2. By distance (closer first)
			if (std::abs(a.distanceValue - b.distanceValue) > 0.5F) {
				return a.distanceValue < b.distanceValue;
			}

			// 3. Stable sort by ID
			return a.id < b.id;
		});
	}

} // namespace world_sim::adapters
