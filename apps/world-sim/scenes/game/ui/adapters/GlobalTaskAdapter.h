#pragma once

// GlobalTaskAdapter - Query layer for global task list UI
//
// This adapter isolates ECS/GlobalTaskRegistry knowledge from the ViewModel.
// Queries all tasks and transforms them into display-ready data.
//
// Used by both:
// - GlobalTaskListView (colony-wide panel)
// - TasksTabView (colonist-specific dialog tab)

#include <ecs/EntityID.h>
#include <ecs/World.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace world_sim::adapters {

/// Display data for a single task
struct GlobalTaskDisplayData {
	uint64_t id = 0;				// Task ID (for sorting stability)
	std::string description;		// "Cut Tree (for Axe)"
	std::string position;			// "(10, 15)" or empty if no specific location
	std::string distance;			// "5m" or empty if no specific location
	std::string status;				// "Available" / "Waiting for harvest" / "Blocked"
	std::string statusDetail;		// "Alice working" / "0/3 materials"
	std::string knownBy;			// "Bob, Alice" (empty for colonist-specific view)
	float distanceValue = 0.0F;		// For sorting (meters)
	uint8_t taskTypePriority = 255; // For sorting by type (lower = higher priority)
	bool isReserved = false;		// For sorting (reserved first)
	bool isMine = false;			// For colonist view: this colonist owns it
	bool isBlocked = false;			// Goal is blocked (waiting for dependencies)
	bool isUnassigned = false;		// Work pool entry (no colonist assigned yet)
	uint32_t quantity = 1;			// Amount: "Cut 2 Trees" or "Haul 3 Wood"
};

/// Query all tasks from GlobalTaskRegistry (for colony-wide view)
/// @param world The ECS world (for colonist name lookups)
/// @param cameraCenter Position to calculate distances from
/// @return Vector of task display data, unsorted
[[nodiscard]] std::vector<GlobalTaskDisplayData> getGlobalTasks(
	ecs::World& world,
	const glm::vec2& cameraCenter
);

/// Query tasks known by a specific colonist (for colonist details tab)
/// @param world The ECS world (for colonist name lookups)
/// @param colonistId The colonist whose known tasks to query
/// @param colonistPosition Position to calculate distances from
/// @return Vector of task display data, unsorted
[[nodiscard]] std::vector<GlobalTaskDisplayData> getTasksForColonist(
	ecs::World& world,
	ecs::EntityID colonistId,
	const glm::vec2& colonistPosition
);

/// Sort tasks for display (reserved first, then by type, then by distance)
void sortTasksForDisplay(std::vector<GlobalTaskDisplayData>& tasks);

} // namespace world_sim::adapters
