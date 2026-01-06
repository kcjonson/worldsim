#include "GoalTaskRegistry.h"

#include <utils/Log.h>

#include <cmath>

namespace ecs {

	GoalTaskRegistry& GoalTaskRegistry::Get() {
		static GoalTaskRegistry instance;
		return instance;
	}

	void GoalTaskRegistry::clear() {
		LOG_DEBUG(Engine, "[GoalRegistry] clear() called - clearing all goals!");
		goals.clear();
		destinationToGoal.clear();
		typeToGoals.clear();
		itemToGoal.clear();
		parentToChildren.clear();
		goalToDependents.clear();
		nextGoalId = 1;
	}

	uint64_t GoalTaskRegistry::createGoal(GoalTask goal) {
		// Assign ID
		goal.id = nextGoalId++;

		// Debug: log goal creation
		const char* typeName = "Unknown";
		switch (goal.type) {
			case TaskType::Harvest: typeName = "Harvest"; break;
			case TaskType::Haul: typeName = "Haul"; break;
			case TaskType::Craft: typeName = "Craft"; break;
			case TaskType::PlacePackaged: typeName = "PlacePackaged"; break;
			default: break;
		}
		LOG_DEBUG(Engine, "[GoalRegistry] Creating %s goal %llu for entity %llu (parent=%s)",
			typeName,
			static_cast<unsigned long long>(goal.id),
			static_cast<unsigned long long>(goal.destinationEntity),
			goal.parentGoalId.has_value() ? std::to_string(goal.parentGoalId.value()).c_str() : "none");

		// Check for duplicate destination - only for top-level goals (no parent)
		// Child goals (Harvest/Haul) can share a destination with their parent Craft goal
		if (!goal.parentGoalId.has_value()) {
			if (destinationToGoal.find(goal.destinationEntity) != destinationToGoal.end()) {
				// Goal already exists for this destination - update instead
				uint64_t existingId = destinationToGoal[goal.destinationEntity];
				LOG_DEBUG(Engine, "[GoalRegistry] Duplicate detected, updating existing goal %llu",
					static_cast<unsigned long long>(existingId));
				goals[existingId] = goal;
				goals[existingId].id = existingId; // Preserve original ID
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

		LOG_DEBUG(Engine, "[GoalRegistry] removeGoal(%llu) - type=%d, parentGoalId=%s",
			static_cast<unsigned long long>(goalId),
			static_cast<int>(it->second.type),
			it->second.parentGoalId.has_value()
				? std::to_string(it->second.parentGoalId.value()).c_str()
				: "none");

		removeFromIndices(it->second);
		goals.erase(it);
	}

	void GoalTaskRegistry::removeGoalByDestination(EntityID destinationEntity) {
		LOG_DEBUG(Engine, "[GoalRegistry] removeGoalByDestination(%llu) called",
			static_cast<unsigned long long>(destinationEntity));
		auto it = destinationToGoal.find(destinationEntity);
		if (it != destinationToGoal.end()) {
			LOG_DEBUG(Engine, "[GoalRegistry] removeGoalByDestination found goal %llu",
				static_cast<unsigned long long>(it->second));
			removeGoal(it->second);
		}
	}

	bool GoalTaskRegistry::reserveItem(uint64_t goalId, uint64_t worldEntityKey, EntityID colonist) {
		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return false;
		}

		auto& goal = it->second;

		// Check if item is already reserved
		if (goal.isItemReserved(worldEntityKey)) {
			return goal.isItemReservedBy(worldEntityKey, colonist); // Allow re-reservation by same colonist
		}

		// Check if goal has capacity
		if (goal.availableCapacity() == 0) {
			return false;
		}

		// Reserve
		goal.itemReservations[worldEntityKey] = colonist;
		itemToGoal[worldEntityKey] = goalId;

		return true;
	}

	void GoalTaskRegistry::releaseItem(uint64_t goalId, uint64_t worldEntityKey) {
		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return;
		}

		it->second.itemReservations.erase(worldEntityKey);
		itemToGoal.erase(worldEntityKey);
	}

	void GoalTaskRegistry::releaseAllForColonist(EntityID colonist) {
		for (auto& [goalId, goal] : goals) {
			// Find and remove all reservations by this colonist
			std::vector<uint64_t> toRemove;
			for (const auto& [worldEntityKey, reservedBy] : goal.itemReservations) {
				if (reservedBy == colonist) {
					toRemove.push_back(worldEntityKey);
				}
			}
			for (uint64_t worldEntityKey : toRemove) {
				goal.itemReservations.erase(worldEntityKey);
				itemToGoal.erase(worldEntityKey);
			}
		}
	}

	void GoalTaskRegistry::recordDelivery(uint64_t goalId, uint64_t worldEntityKey) {
		auto it = goals.find(goalId);
		if (it == goals.end()) {
			return;
		}

		auto& goal = it->second;

		// Release the reservation
		goal.itemReservations.erase(worldEntityKey);
		itemToGoal.erase(worldEntityKey);

		// Increment delivered count
		goal.deliveredAmount++;
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
		LOG_DEBUG(Engine, "[GoalRegistry] getGoalByDestination(%llu): index has %zu entries",
			static_cast<unsigned long long>(destinationEntity),
			destinationToGoal.size());
		auto it = destinationToGoal.find(destinationEntity);
		if (it != destinationToGoal.end()) {
			LOG_DEBUG(Engine, "[GoalRegistry] Found goal %llu for entity %llu",
				static_cast<unsigned long long>(it->second),
				static_cast<unsigned long long>(destinationEntity));
			return getGoal(it->second);
		}
		LOG_DEBUG(Engine, "[GoalRegistry] No goal found for entity %llu",
			static_cast<unsigned long long>(destinationEntity));
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

	std::optional<uint64_t> GoalTaskRegistry::findItemReservation(uint64_t worldEntityKey) const {
		auto it = itemToGoal.find(worldEntityKey);
		if (it != itemToGoal.end()) {
			return it->second;
		}
		return std::nullopt;
	}

	void GoalTaskRegistry::addToIndices(const GoalTask& goal) {
		// Destination index - only for top-level goals
		// Child goals share their parent's destination
		if (!goal.parentGoalId.has_value()) {
			destinationToGoal[goal.destinationEntity] = goal.id;
			LOG_DEBUG(Engine, "[GoalRegistry] Added to destinationToGoal: entity %llu -> goal %llu (index now has %zu entries)",
				static_cast<unsigned long long>(goal.destinationEntity),
				static_cast<unsigned long long>(goal.id),
				destinationToGoal.size());
		}

		// Type index
		typeToGoals[goal.type].insert(goal.id);

		// Parent-child index
		if (goal.parentGoalId.has_value()) {
			parentToChildren[goal.parentGoalId.value()].insert(goal.id);
		}

		// Dependency index
		if (goal.dependsOnGoalId.has_value()) {
			goalToDependents[goal.dependsOnGoalId.value()].insert(goal.id);
		}

		// Item reservations are added individually via reserveItem()
	}

	void GoalTaskRegistry::removeFromIndices(const GoalTask& goal) {
		// Destination index - only for top-level goals
		if (!goal.parentGoalId.has_value()) {
			LOG_DEBUG(Engine, "[GoalRegistry] Removing from destinationToGoal: entity %llu (goal %llu, type=%d)",
				static_cast<unsigned long long>(goal.destinationEntity),
				static_cast<unsigned long long>(goal.id),
				static_cast<int>(goal.type));
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

		// Clear item reservations from index
		for (const auto& [worldEntityKey, colonist] : goal.itemReservations) {
			itemToGoal.erase(worldEntityKey);
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
		LOG_DEBUG(Engine, "[GoalRegistry] removeGoalWithChildren(%llu) called",
			static_cast<unsigned long long>(goalId));

		// First, collect all children (recursively)
		std::vector<uint64_t> toRemove;
		std::vector<uint64_t> queue = {goalId};

		while (!queue.empty()) {
			uint64_t current = queue.back();
			queue.pop_back();
			toRemove.push_back(current);

			// Add children to queue
			auto childIt = parentToChildren.find(current);
			if (childIt != parentToChildren.end()) {
				LOG_DEBUG(Engine, "[GoalRegistry] Goal %llu has %zu children",
					static_cast<unsigned long long>(current),
					childIt->second.size());
				for (uint64_t childId : childIt->second) {
					queue.push_back(childId);
				}
			}
		}

		LOG_DEBUG(Engine, "[GoalRegistry] Collected %zu goals to remove", toRemove.size());

		// Remove all collected goals
		for (uint64_t id : toRemove) {
			removeGoal(id);
		}

		LOG_DEBUG(Engine, "[GoalRegistry] removeGoalWithChildren complete, total goals now: %zu",
			goals.size());
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

} // namespace ecs
