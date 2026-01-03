#pragma once

// GlobalTaskRegistry - Central catalog of all colony work
//
// Tasks exist for entities known to ANY colonist. This makes the system
// infinite-world safe since we only track what colonists have discovered.
//
// See /docs/design/game-systems/colonists/task-registry.md for design details.

#include "EntityID.h"
#include "components/Task.h"

#include <glm/vec2.hpp>

#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace ecs {

/// A task that exists because at least one colonist knows about it
struct GlobalTask {
	// Identity
	uint64_t id = 0;			 // Unique task ID
	EntityID targetEntity;		 // Entity this task operates on (0 for world entities)
	uint64_t worldEntityKey = 0; // Hash key for world entities (from Memory)
	TaskType type = TaskType::None;
	uint32_t defNameId = 0; // For filtering/display

	// Location (from Memory, not entity query)
	glm::vec2 position{0.0F, 0.0F};

	// Which colonists know about this task's target
	std::unordered_set<EntityID> knownBy;

	// For multi-target tasks (e.g., haul: source → destination)
	std::optional<EntityID> secondaryTarget;
	std::optional<glm::vec2> secondaryPosition;

	// Reservation
	std::optional<EntityID> reservedBy;
	float reservedAt = 0.0F;

	// Task chain tracking (Phase 5)
	std::optional<uint64_t> chainId;
	uint8_t chainStep = 0;

	// Metadata
	float createdAt = 0.0F;

	// Check if a specific colonist knows about this task
	[[nodiscard]] bool isKnownBy(EntityID colonist) const { return knownBy.find(colonist) != knownBy.end(); }

	// Check if task is reserved
	[[nodiscard]] bool isReserved() const { return reservedBy.has_value(); }

	// Check if reserved by a specific colonist
	[[nodiscard]] bool isReservedBy(EntityID colonist) const { return reservedBy.has_value() && reservedBy.value() == colonist; }
};

/// Filter predicate for task queries
using TaskFilter = std::function<bool(const GlobalTask&)>;

/// Global registry of all colony tasks
/// Singleton - access via GlobalTaskRegistry::Get()
class GlobalTaskRegistry {
  public:
	// Singleton access
	static GlobalTaskRegistry& Get();

	// Clear all tasks (for game restart)
	void clear();

	// --- Task Management ---

	/// Add or update a task when a colonist discovers an entity
	/// @param colonist The colonist who discovered the entity
	/// @param worldEntityKey Hash key from Memory (for world entities)
	/// @param defNameId Asset definition ID
	/// @param position World position
	/// @param taskType Type of task this entity generates
	/// @param currentTime Current game time (for createdAt)
	/// @return The task ID
	uint64_t onEntityDiscovered(
		EntityID colonist, uint64_t worldEntityKey, uint32_t defNameId, const glm::vec2& position, TaskType taskType, float currentTime
	);

	/// Remove a colonist from a task's knownBy set
	/// If no colonists know about the task anymore, it is removed
	/// @param colonist The colonist who forgot the entity
	/// @param worldEntityKey Hash key from Memory
	void onEntityForgotten(EntityID colonist, uint64_t worldEntityKey);

	/// Remove all tasks for a destroyed entity
	void onEntityDestroyed(uint64_t worldEntityKey);

	// --- Reservation ---

	/// Reserve a task for a colonist
	/// @return true if reservation succeeded, false if already reserved by another colonist
	bool reserve(uint64_t taskId, EntityID colonist, float currentTime);

	/// Release a reservation
	void release(uint64_t taskId);

	/// Release all reservations held by a colonist
	void releaseAll(EntityID colonist);

	/// Release stale reservations (no progress for timeout seconds)
	void releaseStale(float currentTime, float timeout = 10.0F);

	// --- Queries ---

	/// Get a task by ID
	[[nodiscard]] const GlobalTask* getTask(uint64_t taskId) const;

	/// Get all tasks known by a colonist
	[[nodiscard]] std::vector<const GlobalTask*> getTasksFor(EntityID colonist) const;

	/// Get all tasks of a specific type known by a colonist
	[[nodiscard]] std::vector<const GlobalTask*> getTasksFor(EntityID colonist, TaskType type) const;

	/// Get all tasks matching a filter
	[[nodiscard]] std::vector<const GlobalTask*> getTasksMatching(const TaskFilter& filter) const;

	/// Get tasks within radius of a position
	[[nodiscard]] std::vector<const GlobalTask*> getTasksInRadius(const glm::vec2& center, float radius) const;

	/// Get tasks within radius known by a colonist
	[[nodiscard]] std::vector<const GlobalTask*> getTasksInRadius(const glm::vec2& center, float radius, EntityID colonist) const;

	/// Get count of all tasks
	[[nodiscard]] size_t taskCount() const { return tasks.size(); }

	/// Get count of tasks by type
	[[nodiscard]] size_t taskCount(TaskType type) const;

  private:
	GlobalTaskRegistry() = default;

	// Task storage
	std::unordered_map<uint64_t, GlobalTask> tasks; // taskId → task

	// Index: worldEntityKey → taskId (for fast lookup when forgetting/destroying)
	std::unordered_map<uint64_t, uint64_t> worldEntityToTask;

	// Index: colonist → set of taskIds they know about
	std::unordered_map<EntityID, std::unordered_set<uint64_t>> colonistToTasks;

	// Index: TaskType → set of taskIds
	std::unordered_map<TaskType, std::unordered_set<uint64_t>> typeToTasks;

	// Next task ID
	uint64_t nextTaskId = 1;

	// Internal helpers
	void removeTask(uint64_t taskId);
	void addToIndices(const GlobalTask& task);
	void removeFromIndices(const GlobalTask& task);
};

} // namespace ecs
