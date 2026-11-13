#include "ColonyGoalWork.h"

#include "ChainClaims.h"

#include "../World.h"
#include "../components/Colonist.h"
#include "../components/DecisionTrace.h"
#include "../components/Task.h"

#include <algorithm>

namespace ecs {

	ColonyGoalWork::ColonyGoalWork(World& world, ClaimIndex& claimIndex)
		: claims(claimIndex) {
		claimIndex.rebuild(world);
		const auto addWorker = [](std::unordered_map<uint64_t, std::vector<EntityID>>& byKey, uint64_t key, EntityID colonist) {
			byKey[key].push_back(colonist);
		};
		// An option that works an entity (a station, a site, a package) is an offer toward the goal whose
		// destination it is, the goal workersOf names its workers for.
		const auto goalWorking = [](uint64_t entity, TaskType type) -> uint64_t {
			const GoalTask* goal = GoalTaskRegistry::Get().getGoalByDestination(static_cast<EntityID>(entity));
			return goal != nullptr && goal->type == type ? goal->id : 0U;
		};
		for (auto [colonist, identity, task] : world.view<Colonist, Task>()) {
			switch (task.type) {
				case TaskType::Harvest:
					addWorker(workersByGoal, task.harvestGoalId, colonist);
					break;
				case TaskType::Haul:
					addWorker(workersByGoal, task.haulGoalId, colonist);
					break;
				case TaskType::Build:
				case TaskType::Deconstruct:
					addWorker(workersByEntity, task.buildBlueprintEntityId, colonist);
					break;
				case TaskType::Craft:
					addWorker(workersByEntity, task.targetStationId, colonist);
					break;
				case TaskType::PlacePackaged:
					addWorker(workersByEntity, task.placePackagedEntityId, colonist);
					break;
				case TaskType::FulfillNeed:
				case TaskType::Wander:
				case TaskType::None:
					break;
			}

			const auto* trace = world.getComponent<DecisionTrace>(colonist);
			if (trace == nullptr) {
				continue;
			}
			for (const EvaluatedOption& option : trace->options) {
				const bool open = option.status == OptionStatus::Selected || option.status == OptionStatus::Available;
				const bool offered = open || option.status == OptionStatus::NoRoom || option.status == OptionStatus::Reserved;
				const bool sourceFree = open || option.status == OptionStatus::NoRoom || option.status == OptionStatus::Regrowing;
				if (!offered && !sourceFree) {
					continue;
				}
				uint64_t goal = 0;
				switch (option.taskType) {
					case TaskType::Haul:
						goal = option.haulGoalId;
						break;
					case TaskType::Harvest:
						goal = option.harvestGoalId;
						break;
					case TaskType::Build:
					case TaskType::Deconstruct:
						goal = goalWorking(option.buildBlueprintEntityId, option.taskType);
						break;
					case TaskType::Craft:
						goal = goalWorking(option.stationEntityId, TaskType::Craft);
						break;
					case TaskType::PlacePackaged:
						goal = goalWorking(option.placePackagedEntityId, TaskType::PlacePackaged);
						break;
					case TaskType::FulfillNeed:
					case TaskType::Wander:
					case TaskType::None:
						break;
				}
				if (goal == 0) {
					continue;
				}
				Offers& toGoal = offers[goal];
				// Every option of one colonist comes before the next colonist's, so this lists each once.
				if (offered && (toGoal.colonists.empty() || toGoal.colonists.back() != colonist)) {
					toGoal.colonists.push_back(colonist);
				}
				toGoal.sourceFree = toGoal.sourceFree || sourceFree;
				toGoal.reserved = toGoal.reserved || option.status == OptionStatus::Reserved;
			}
		}
	}

	std::span<const EntityID> ColonyGoalWork::workersOf(const GoalTask& goal) const {
		const bool worksEntity = goal.type == TaskType::Build || goal.type == TaskType::Craft || goal.type == TaskType::PlacePackaged;
		if (const auto it = workersByEntity.find(static_cast<uint64_t>(goal.destinationEntity)); worksEntity && it != workersByEntity.end()) {
			return it->second;
		}
		if (const auto it = workersByGoal.find(goal.id); it != workersByGoal.end()) {
			return it->second;
		}
		return {};
	}

	std::span<const EntityID> ColonyGoalWork::offeredTo(uint64_t goalId) const {
		const auto it = offers.find(goalId);
		return it != offers.end() ? std::span<const EntityID>{it->second.colonists} : std::span<const EntityID>{};
	}

	bool ColonyGoalWork::allSourcesClaimed(uint64_t goalId) const {
		const auto it = offers.find(goalId);
		return it != offers.end() && it->second.reserved && !it->second.sourceFree;
	}

	bool ColonyGoalWork::hasWork(const GoalTask& goal) const {
		return goal.owner != GoalOwner::StorageGoalSystem || !offeredTo(goal.id).empty();
	}

	std::optional<GoalItemCount> ColonyGoalWork::itemCount(World& world, const GoalTask& goal) const {
		const bool oneItem = goal.acceptedDefNameIds.size() == 1 && goal.acceptedCategory == assets::ItemCategory::None;
		if ((goal.type != TaskType::Harvest && goal.type != TaskType::Haul) || goal.clearFootprint.has_value() || !oneItem) {
			return std::nullopt;
		}
		const auto&		registry = GoalTaskRegistry::Get();
		const GoalTask* root = registry.getGoal(registry.rootOf(goal.id));
		if (root == nullptr) {
			return std::nullopt;
		}
		const uint32_t item = goal.acceptedDefNameIds.front();
		const uint32_t onTheWay = claims.inFlight(root->id, item, ClaimExclusion{});
		if (goal.type == TaskType::Haul && goal.owner == GoalOwner::StorageGoalSystem) {
			return GoalItemCount{.onTheWay = onTheWay};
		}
		const NeedBasis basis = goal.type == TaskType::Harvest ? harvestBasis(*root) : NeedBasis::Stock;
		const uint32_t	need = registry.remainingNeed(world, root->id, item, basis);
		return GoalItemCount{.needed = need - std::min(need, onTheWay), .onTheWay = onTheWay};
	}

} // namespace ecs
