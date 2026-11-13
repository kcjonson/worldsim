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
#include <ecs/systems/ColonyGoalWork.h>

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

		/// A Craft goal's materials for its station's next job: the recipe's total inputs, and how many of
		/// them the root no longer needs.
		CraftMaterials craftMaterials(ecs::World& world, const ecs::GoalTask& goal) {
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
			return {.landed = required - std::min(missing, required), .required = required};
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

			TaskRowFacts facts{
				.buildSite = goal.type == ecs::TaskType::Build,
				.blocked = goal.status == ecs::GoalStatus::Blocked,
				.noSource = goal.status == ecs::GoalStatus::NoSource,
				.allSourcesClaimed = work.allSourcesClaimed(goal.id),
				.distance = data.distanceValue,
				.workers = colonistNames(world, work.workersOf(goal)),
			};
			if (facts.workers.empty()) {
				facts.knownTo = colonistNames(world, work.offeredTo(goal.id));
			}
			// Build umbrella: its progress lives on the blueprint's workDone.
			if (const auto* bp = world.getComponent<ecs::StructureBlueprint>(goal.destinationEntity); facts.buildSite && bp != nullptr) {
				facts.buildPercent = static_cast<int>(bp->progress() * 100.0F);
				facts.underConstruction = bp->phase == ecs::StructureBlueprint::BuildPhase::UnderConstruction;
			}
			if (facts.blocked && goal.type == ecs::TaskType::Craft) {
				facts.materials = craftMaterials(world, goal);
			}
			if (const auto count = work.itemCount(world, goal)) {
				facts.needed = count->needed;
				facts.onTheWay = count->onTheWay;
			}
			TaskRowText row = taskRowText(facts);
			data.status = std::move(row.status);
			data.statusDetail = std::move(row.detail);
			data.isBlocked = row.blocked;
			data.isUnassigned = row.unassigned;

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
			// Kept across refreshes so a rebuild reuses its storage; the panel runs on the main thread only.
			static ecs::ClaimIndex			   claims;
			const ecs::ColonyGoalWork		   work(world, claims);
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
