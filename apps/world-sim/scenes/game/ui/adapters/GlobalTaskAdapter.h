#pragma once

// GlobalTaskAdapter - Query layer for global task list UI
//
// This adapter isolates ECS/GoalTaskRegistry knowledge from the ViewModel.
// Queries all goals and transforms them into display-ready data.
//
// Used by both:
// - GlobalTaskListView (colony-wide panel)
// - TasksTabView (colonist-specific dialog tab)

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace ecs {
	class World;
}

namespace world_sim::adapters {

/// Display data for a single task
struct GlobalTaskDisplayData {
	uint64_t id = 0;				// Task ID (for sorting stability)
	std::string description;		// "Cut Tree (for Axe)"
	std::string position;			// "(10, 15)" or empty if no specific location
	std::string distance;			// "5m" or empty if no specific location
	std::string status;				// "Available" / "Waiting for harvest" / "Blocked"
	std::string statusDetail;		// "Alice working" / "0/3 materials"
	float distanceValue = 0.0F;		// For sorting (meters)
	uint8_t taskTypePriority = 255; // For sorting by type (lower = higher priority)
	bool isBlocked = false;			// Goal is blocked (waiting for dependencies)
	bool isUnassigned = false;		// Work pool entry (no colonist assigned yet)
};

/// Query all goals from GoalTaskRegistry, for the colony-wide panel and the colonist details tab
/// @param world ECS world, used to surface which colonist is working each goal + build progress
/// @param referencePosition Position to calculate distances from (the camera center, or the colonist)
/// @return Vector of task display data, unsorted
[[nodiscard]] std::vector<GlobalTaskDisplayData> getGlobalTasks(
	ecs::World&		 world,
	const glm::vec2& referencePosition
);

/// Sort tasks for display (by type, then by distance)
void sortTasksForDisplay(std::vector<GlobalTaskDisplayData>& tasks);

} // namespace world_sim::adapters
