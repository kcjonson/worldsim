#include "GlobalTaskListModel.h"

#include <cmath>

namespace world_sim {

namespace {

// Distance values within this threshold are considered equal
// This prevents UI updates from minor camera movements
constexpr float kDistanceChangeThreshold = 1.0F;

bool distanceChanged(float oldDist, float newDist) {
	return std::abs(oldDist - newDist) > kDistanceChangeThreshold;
}

} // namespace

bool GlobalTaskListModel::refresh(ecs::World& world, const glm::vec2& cameraCenter, float deltaTime) {
	// Throttle refreshes to 5Hz unless forced
	if (!forceRefresh) {
		timeSinceRefresh += deltaTime;
		if (timeSinceRefresh < kRefreshInterval) {
			return false;
		}
	}

	// Reset throttle timer
	timeSinceRefresh = 0.0F;
	forceRefresh = false;

	// Query fresh data
	auto newData = adapters::getGlobalTasks(world, cameraCenter);
	adapters::sortTasksForDisplay(newData);

	// First refresh always triggers rebuild
	if (isFirstRefresh) {
		isFirstRefresh = false;
		tasksData = std::move(newData);
		return true;
	}

	// Check for changes
	if (hasChanged(newData)) {
		tasksData = std::move(newData);
		return true;
	}

	return false;
}

bool GlobalTaskListModel::hasChanged(const std::vector<TaskData>& newData) const {
	// Structural change: different number of tasks
	if (newData.size() != tasksData.size()) {
		return true;
	}

	// Value changes: check each task
	for (size_t i = 0; i < newData.size(); ++i) {
		const auto& oldTask = tasksData[i];
		const auto& newTask = newData[i];

		// Different task (ID changed or reordered)
		if (oldTask.id != newTask.id) {
			return true;
		}

		// Status changed
		if (oldTask.status != newTask.status) {
			return true;
		}

		// Reserved state changed
		if (oldTask.isReserved != newTask.isReserved) {
			return true;
		}

		// Distance changed significantly
		if (distanceChanged(oldTask.distanceValue, newTask.distanceValue)) {
			return true;
		}

		// Known by changed (colonist discovered/forgot)
		if (oldTask.knownBy != newTask.knownBy) {
			return true;
		}
	}

	return false;
}

} // namespace world_sim
