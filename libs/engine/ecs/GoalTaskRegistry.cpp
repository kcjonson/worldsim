#include "GoalTaskRegistry.h"

namespace ecs {

	GoalTaskRegistry& GoalTaskRegistry::Get() {
		static GoalTaskRegistry instance;
		return instance;
	}

	void GoalTaskRegistry::clear() {
		goals.clear();
		destinationToGoal.clear();
		typeToGoals.clear();
		ownerToGoals.clear();
		parentToChildren.clear();
		goalToDependents.clear();
		nextGoalId = 1;
	}

	uint64_t GoalTaskRegistry::createGoal(GoalTask goal) {
		// Assign ID
		goal.id = nextGoalId++;

		// Check for duplicate destination - only for top-level goals (no parent)
		// Child goals (Harvest/Haul) can share a destination with their parent Craft goal
		if (!goal.parentGoalId.has_value()) {
			auto existing = destinationToGoal.find(goal.destinationEntity);
			if (existing != destinationToGoal.end()) {
				// Goal already exists for this destination - update in place but keep
				// the original ID stable (call sites and indices rely on it).
				uint64_t existingId = existing->second;
				auto	 goalIt = goals.find(existingId);
				if (goalIt != goals.end()) {
					// type/owner/parent may differ, so re-index to avoid stale entries
					removeFromIndices(goalIt->second);
					goal.id = existingId;
					goalIt->second = std::move(goal);
					addToIndices(goalIt->second);
				}
				return existingId;
			}
		}

		// Store goal
		uint64_t goalId = goal.id;
		goals[goalId] = std::move(goal);
		addToIndices(goals[goalId]);

		return goalId;
	}

	void GoalTaskRegistry::updateGoal(uint64_t goalId, const std::function<void(GoalTask&)>& updater) {
		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return;
		}

		// Remove from type index (type might change)
		auto typeIt = typeToGoals.find(it->second.type);
		if (typeIt != typeToGoals.end()) {
			typeIt->second.erase(goalId);
			if (typeIt->second.empty()) {
				typeToGoals.erase(typeIt);
			}
		}

		// Apply update
		updater(it->second);

		// Re-add to type index
		typeToGoals[it->second.type].insert(goalId);
	}

	void GoalTaskRegistry::removeGoal(uint64_t goalId) {
		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return;
		}

		removeFromIndices(it->second);
		goals.erase(it);
	}

	void GoalTaskRegistry::removeGoalByDestination(EntityID destinationEntity) {
		auto it = destinationToGoal.find(destinationEntity);
		if (it != destinationToGoal.end()) {
			removeGoal(it->second);
		}
	}

	void GoalTaskRegistry::recordDelivery(uint64_t goalId, uint32_t amount) {
		if (amount == 0) {
			return;
		}

		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return;
		}

		it->second.deliveredAmount += amount;

		// Credit the parent Craft goal too: its target is the total material count, and a
		// material reaches the station only when a Haul child deposits it there. Harvest
		// deliveries land in the colonist's inventory, not the station, so they must NOT
		// credit the parent (that would double-count once the matching Haul completes).
		// Once materials are satisfied, the Craft leaves Blocked so the crafting work
		// itself can be picked up.
		if (it->second.type == TaskType::Haul && it->second.parentGoalId.has_value()) {
			auto parentIt = goals.find(it->second.parentGoalId.value());
			if (parentIt != goals.end() && parentIt->second.type == TaskType::Craft) {
				parentIt->second.deliveredAmount += amount;
				if (parentIt->second.status == GoalStatus::Blocked &&
					parentIt->second.deliveredAmount >= parentIt->second.targetAmount) {
					parentIt->second.status = GoalStatus::Available;
				}
			}
		}
	}

	const GoalTask* GoalTaskRegistry::getGoal(uint64_t goalId) const {
		auto it = goals.find(goalId);
		if (it != goals.end()) {
			return &it->second;
		}
		return nullptr;
	}

	GoalTask* GoalTaskRegistry::getGoalMutable(uint64_t goalId) {
		auto it = goals.find(goalId);
		if (it != goals.end()) {
			return &it->second;
		}
		return nullptr;
	}

	const GoalTask* GoalTaskRegistry::getGoalByDestination(EntityID destinationEntity) const {
		auto it = destinationToGoal.find(destinationEntity);
		if (it != destinationToGoal.end()) {
			return getGoal(it->second);
		}
		return nullptr;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getGoalsOfType(TaskType type) const {
		std::vector<const GoalTask*> result;

		auto it = typeToGoals.find(type);
		if (it != typeToGoals.end()) {
			result.reserve(it->second.size());
			for (uint64_t goalId : it->second) {
				auto goalIt = goals.find(goalId);
				if (goalIt != goals.end()) {
					result.push_back(&goalIt->second);
				}
			}
		}

		return result;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getGoalsMatching(const GoalFilter& filter) const {
		std::vector<const GoalTask*> result;

		for (const auto& [goalId, goal] : goals) {
			if (filter(goal)) {
				result.push_back(&goal);
			}
		}

		return result;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getGoalsInRadius(const glm::vec2& center, float radius) const {
		std::vector<const GoalTask*> result;
		float						 radiusSq = radius * radius;

		for (const auto& [goalId, goal] : goals) {
			float dx = goal.destinationPosition.x - center.x;
			float dy = goal.destinationPosition.y - center.y;
			if (dx * dx + dy * dy <= radiusSq) {
				result.push_back(&goal);
			}
		}

		return result;
	}

	size_t GoalTaskRegistry::goalCount(TaskType type) const {
		auto it = typeToGoals.find(type);
		if (it != typeToGoals.end()) {
			return it->second.size();
		}
		return 0;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getGoalsByOwner(GoalOwner owner) const {
		std::vector<const GoalTask*> result;

		auto it = ownerToGoals.find(owner);
		if (it != ownerToGoals.end()) {
			result.reserve(it->second.size());
			for (uint64_t goalId : it->second) {
				auto goalIt = goals.find(goalId);
				if (goalIt != goals.end()) {
					result.push_back(&goalIt->second);
				}
			}
		}

		return result;
	}

	size_t GoalTaskRegistry::goalCount(GoalOwner owner) const {
		auto it = ownerToGoals.find(owner);
		if (it != ownerToGoals.end()) {
			return it->second.size();
		}
		return 0;
	}

	void GoalTaskRegistry::addToIndices(const GoalTask& goal) {
		// Destination index - only for top-level goals
		// Child goals share their parent's destination
		if (!goal.parentGoalId.has_value()) {
			destinationToGoal[goal.destinationEntity] = goal.id;
		}

		// Type index
		typeToGoals[goal.type].insert(goal.id);

		// Owner index
		if (goal.owner != GoalOwner::None) {
			ownerToGoals[goal.owner].insert(goal.id);
		}

		// Parent-child index
		if (goal.parentGoalId.has_value()) {
			parentToChildren[goal.parentGoalId.value()].insert(goal.id);
		}

		// Dependency index
		if (goal.dependsOnGoalId.has_value()) {
			goalToDependents[goal.dependsOnGoalId.value()].insert(goal.id);
		}
	}

	void GoalTaskRegistry::removeFromIndices(const GoalTask& goal) {
		// Destination index - only for top-level goals
		if (!goal.parentGoalId.has_value()) {
			destinationToGoal.erase(goal.destinationEntity);
		}

		// Type index
		auto typeIt = typeToGoals.find(goal.type);
		if (typeIt != typeToGoals.end()) {
			typeIt->second.erase(goal.id);
			if (typeIt->second.empty()) {
				typeToGoals.erase(typeIt);
			}
		}

		// Owner index
		if (goal.owner != GoalOwner::None) {
			auto ownerIt = ownerToGoals.find(goal.owner);
			if (ownerIt != ownerToGoals.end()) {
				ownerIt->second.erase(goal.id);
				if (ownerIt->second.empty()) {
					ownerToGoals.erase(ownerIt);
				}
			}
		}

		// Parent-child index
		if (goal.parentGoalId.has_value()) {
			auto parentIt = parentToChildren.find(goal.parentGoalId.value());
			if (parentIt != parentToChildren.end()) {
				parentIt->second.erase(goal.id);
				if (parentIt->second.empty()) {
					parentToChildren.erase(parentIt);
				}
			}
		}

		// Dependency index
		if (goal.dependsOnGoalId.has_value()) {
			auto depIt = goalToDependents.find(goal.dependsOnGoalId.value());
			if (depIt != goalToDependents.end()) {
				depIt->second.erase(goal.id);
				if (depIt->second.empty()) {
					goalToDependents.erase(depIt);
				}
			}
		}
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getChildGoals(uint64_t parentId) const {
		std::vector<const GoalTask*> result;

		auto it = parentToChildren.find(parentId);
		if (it != parentToChildren.end()) {
			result.reserve(it->second.size());
			for (uint64_t childId : it->second) {
				auto goalIt = goals.find(childId);
				if (goalIt != goals.end()) {
					result.push_back(&goalIt->second);
				}
			}
		}

		return result;
	}

	std::vector<const GoalTask*> GoalTaskRegistry::getDependentGoals(uint64_t goalId) const {
		std::vector<const GoalTask*> result;

		auto it = goalToDependents.find(goalId);
		if (it != goalToDependents.end()) {
			result.reserve(it->second.size());
			for (uint64_t depId : it->second) {
				auto goalIt = goals.find(depId);
				if (goalIt != goals.end()) {
					result.push_back(&goalIt->second);
				}
			}
		}

		return result;
	}

	void GoalTaskRegistry::removeGoalWithChildren(uint64_t goalId) {
		// Collect this goal and all its descendants (recursively)
		std::vector<uint64_t> toRemove;
		std::vector<uint64_t> queue = {goalId};

		while (!queue.empty()) {
			uint64_t current = queue.back();
			queue.pop_back();
			toRemove.push_back(current);

			auto childIt = parentToChildren.find(current);
			if (childIt != parentToChildren.end()) {
				for (uint64_t childId : childIt->second) {
					queue.push_back(childId);
				}
			}
		}

		for (uint64_t id : toRemove) {
			removeGoal(id);
		}
	}

	void GoalTaskRegistry::notifyGoalCompleted(uint64_t completedGoalId) {
		// Find all goals that depend on this one
		auto depIt = goalToDependents.find(completedGoalId);
		if (depIt == goalToDependents.end()) {
			return;
		}

		// Update status of dependent goals
		for (uint64_t dependentId : depIt->second) {
			auto goalIt = goals.find(dependentId);
			if (goalIt != goals.end() && goalIt->second.status == GoalStatus::WaitingForItems) {
				goalIt->second.status = GoalStatus::Available;
			}
		}
	}

	uint64_t GoalTaskRegistry::createHaulForCompletedHarvest(uint64_t harvestGoalId) {
		auto it = goals.find(harvestGoalId);
		if (it == goals.end() || it->second.type != TaskType::Harvest) {
			return 0;
		}

		const GoalTask& harvest = it->second;
		GoalTask		haul;
		haul.type = TaskType::Haul;
		haul.owner = harvest.owner;
		haul.destinationEntity = harvest.destinationEntity;
		haul.destinationPosition = harvest.destinationPosition;
		haul.destinationDefNameId = harvest.destinationDefNameId;
		haul.acceptedDefNameIds = harvest.acceptedDefNameIds;
		haul.acceptedCategory = harvest.acceptedCategory;
		haul.targetAmount = harvest.targetAmount;
		haul.deliveredAmount = 0;
		haul.createdAt = harvest.createdAt;
		haul.parentGoalId = harvest.parentGoalId;
		haul.status = GoalStatus::Available;
		haul.chainId = harvest.chainId;

		return createGoal(std::move(haul));
	}

} // namespace ecs
