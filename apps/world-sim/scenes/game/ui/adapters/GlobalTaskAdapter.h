#pragma once

// GlobalTaskAdapter - Query layer for global task list UI
//
// This adapter isolates ECS/GoalTaskRegistry knowledge from the ViewModel.
// Queries all goals and transforms them into display-ready data.
//
// Used by both:
// - GlobalTaskListView (colony-wide panel)
// - TasksTabView (colonist-specific dialog tab)

#include <ecs/EntityID.h>
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
	std::string status;				// "Working" / "Unassigned" / "Waiting (no source found)" / ...
	std::string statusDetail;		// "Alice, Bob; needs 12, 4 on the way" / "far; known to Alice"
	float distanceValue = 0.0F;		// For sorting (meters)
	uint8_t taskTypePriority = 255; // For sorting by type (lower = higher priority)
	bool isBlocked = false;			// Goal is blocked (waiting for dependencies)
	bool isUnassigned = false;		// Work pool entry (no colonist assigned yet)
};

/// Every goal with something to do, for the colony-wide panel, unsorted, distances from
/// `referencePosition` (the camera center).
[[nodiscard]] std::vector<GlobalTaskDisplayData> getGlobalTasks(
	ecs::World&		 world,
	const glm::vec2& referencePosition
);

/// The same rows, for the colonist details tab: only the goals the colonist's last decision offered
/// work toward, unsorted, distances from `referencePosition` (the colonist).
[[nodiscard]] std::vector<GlobalTaskDisplayData> getKnownTasks(
	ecs::World&		 world,
	ecs::EntityID	 colonist,
	const glm::vec2& referencePosition
);

/// Sort tasks for display (by type, then by distance)
void sortTasksForDisplay(std::vector<GlobalTaskDisplayData>& tasks);

} // namespace world_sim::adapters
