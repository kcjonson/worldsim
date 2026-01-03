#include "GlobalTaskRegistry.h"

#include <algorithm>
#include <cmath>

namespace ecs {

GlobalTaskRegistry& GlobalTaskRegistry::Get() {
	static GlobalTaskRegistry instance;
	return instance;
}

void GlobalTaskRegistry::clear() {
	tasks.clear();
	worldEntityToTask.clear();
	colonistToTasks.clear();
	typeToTasks.clear();
	nextTaskId = 1;
}

uint64_t GlobalTaskRegistry::onEntityDiscovered(
	EntityID colonist, uint64_t worldEntityKey, uint32_t defNameId, const glm::vec2& position, TaskType taskType, float currentTime
) {
	// Check if task already exists for this world entity
	auto it = worldEntityToTask.find(worldEntityKey);
	if (it != worldEntityToTask.end()) {
		// Task exists - add colonist to knownBy
		uint64_t taskId = it->second;
		auto&	 task = tasks[taskId];
		task.knownBy.insert(colonist);
		colonistToTasks[colonist].insert(taskId);
		return taskId;
	}

	// Create new task
	uint64_t taskId = nextTaskId++;

	GlobalTask task;
	task.id = taskId;
	task.worldEntityKey = worldEntityKey;
	task.defNameId = defNameId;
	task.position = position;
	task.type = taskType;
	task.createdAt = currentTime;
	task.knownBy.insert(colonist);

	// Store task
	tasks[taskId] = task;
	addToIndices(tasks[taskId]);

	return taskId;
}

void GlobalTaskRegistry::onEntityForgotten(EntityID colonist, uint64_t worldEntityKey) {
	auto it = worldEntityToTask.find(worldEntityKey);
	if (it == worldEntityToTask.end()) {
		return; // No task for this entity
	}

	uint64_t taskId = it->second;
	auto	 taskIt = tasks.find(taskId);
	if (taskIt == tasks.end()) {
		return;
	}

	auto& task = taskIt->second;

	// Remove colonist from knownBy
	task.knownBy.erase(colonist);
	colonistToTasks[colonist].erase(taskId);

	// If no colonists know about this task anymore, remove it
	if (task.knownBy.empty()) {
		removeTask(taskId);
	}
}

void GlobalTaskRegistry::onEntityDestroyed(uint64_t worldEntityKey) {
	auto it = worldEntityToTask.find(worldEntityKey);
	if (it == worldEntityToTask.end()) {
		return;
	}

	removeTask(it->second);
}

bool GlobalTaskRegistry::reserve(uint64_t taskId, EntityID colonist, float currentTime) {
	auto it = tasks.find(taskId);
	if (it == tasks.end()) {
		return false;
	}

	auto& task = it->second;

	// Check if colonist knows about this task
	if (!task.isKnownBy(colonist)) {
		return false;
	}

	// Check if already reserved by another colonist
	if (task.isReserved() && !task.isReservedBy(colonist)) {
		return false;
	}

	task.reservedBy = colonist;
	task.reservedAt = currentTime;
	return true;
}

void GlobalTaskRegistry::release(uint64_t taskId) {
	auto it = tasks.find(taskId);
	if (it != tasks.end()) {
		it->second.reservedBy.reset();
		it->second.reservedAt = 0.0F;
	}
}

void GlobalTaskRegistry::releaseAll(EntityID colonist) {
	for (auto& [taskId, task] : tasks) {
		if (task.isReservedBy(colonist)) {
			task.reservedBy.reset();
			task.reservedAt = 0.0F;
		}
	}
}

void GlobalTaskRegistry::releaseStale(float currentTime, float timeout) {
	for (auto& [taskId, task] : tasks) {
		if (task.isReserved()) {
			if (currentTime - task.reservedAt > timeout) {
				task.reservedBy.reset();
				task.reservedAt = 0.0F;
			}
		}
	}
}

const GlobalTask* GlobalTaskRegistry::getTask(uint64_t taskId) const {
	auto it = tasks.find(taskId);
	if (it != tasks.end()) {
		return &it->second;
	}
	return nullptr;
}

std::vector<const GlobalTask*> GlobalTaskRegistry::getTasksFor(EntityID colonist) const {
	std::vector<const GlobalTask*> result;

	auto it = colonistToTasks.find(colonist);
	if (it != colonistToTasks.end()) {
		result.reserve(it->second.size());
		for (uint64_t taskId : it->second) {
			auto taskIt = tasks.find(taskId);
			if (taskIt != tasks.end()) {
				result.push_back(&taskIt->second);
			}
		}
	}

	return result;
}

std::vector<const GlobalTask*> GlobalTaskRegistry::getTasksFor(EntityID colonist, TaskType type) const {
	std::vector<const GlobalTask*> result;

	auto colonistIt = colonistToTasks.find(colonist);
	if (colonistIt == colonistToTasks.end()) {
		return result;
	}

	auto typeIt = typeToTasks.find(type);
	if (typeIt == typeToTasks.end()) {
		return result;
	}

	// Intersect colonist tasks with type tasks
	for (uint64_t taskId : colonistIt->second) {
		if (typeIt->second.find(taskId) != typeIt->second.end()) {
			auto taskIt = tasks.find(taskId);
			if (taskIt != tasks.end()) {
				result.push_back(&taskIt->second);
			}
		}
	}

	return result;
}

std::vector<const GlobalTask*> GlobalTaskRegistry::getTasksMatching(const TaskFilter& filter) const {
	std::vector<const GlobalTask*> result;

	for (const auto& [taskId, task] : tasks) {
		if (filter(task)) {
			result.push_back(&task);
		}
	}

	return result;
}

std::vector<const GlobalTask*> GlobalTaskRegistry::getTasksInRadius(const glm::vec2& center, float radius) const {
	std::vector<const GlobalTask*> result;
	float						   radiusSq = radius * radius;

	for (const auto& [taskId, task] : tasks) {
		float dx = task.position.x - center.x;
		float dy = task.position.y - center.y;
		if (dx * dx + dy * dy <= radiusSq) {
			result.push_back(&task);
		}
	}

	return result;
}

std::vector<const GlobalTask*> GlobalTaskRegistry::getTasksInRadius(const glm::vec2& center, float radius, EntityID colonist) const {
	std::vector<const GlobalTask*> result;
	float						   radiusSq = radius * radius;

	auto colonistIt = colonistToTasks.find(colonist);
	if (colonistIt == colonistToTasks.end()) {
		return result;
	}

	for (uint64_t taskId : colonistIt->second) {
		auto taskIt = tasks.find(taskId);
		if (taskIt != tasks.end()) {
			const auto& task = taskIt->second;
			float		dx = task.position.x - center.x;
			float		dy = task.position.y - center.y;
			if (dx * dx + dy * dy <= radiusSq) {
				result.push_back(&task);
			}
		}
	}

	return result;
}

size_t GlobalTaskRegistry::taskCount(TaskType type) const {
	auto it = typeToTasks.find(type);
	if (it != typeToTasks.end()) {
		return it->second.size();
	}
	return 0;
}

void GlobalTaskRegistry::removeTask(uint64_t taskId) {
	auto it = tasks.find(taskId);
	if (it == tasks.end()) {
		return;
	}

	removeFromIndices(it->second);
	tasks.erase(it);
}

void GlobalTaskRegistry::addToIndices(const GlobalTask& task) {
	// World entity index
	if (task.worldEntityKey != 0) {
		worldEntityToTask[task.worldEntityKey] = task.id;
	}

	// Colonist index
	for (EntityID colonist : task.knownBy) {
		colonistToTasks[colonist].insert(task.id);
	}

	// Type index
	typeToTasks[task.type].insert(task.id);
}

void GlobalTaskRegistry::removeFromIndices(const GlobalTask& task) {
	// World entity index
	if (task.worldEntityKey != 0) {
		worldEntityToTask.erase(task.worldEntityKey);
	}

	// Colonist index
	for (EntityID colonist : task.knownBy) {
		auto it = colonistToTasks.find(colonist);
		if (it != colonistToTasks.end()) {
			it->second.erase(task.id);
		}
	}

	// Type index
	auto typeIt = typeToTasks.find(task.type);
	if (typeIt != typeToTasks.end()) {
		typeIt->second.erase(task.id);
	}
}

} // namespace ecs
