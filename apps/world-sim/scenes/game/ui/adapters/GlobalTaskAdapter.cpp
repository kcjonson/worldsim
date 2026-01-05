#include "GlobalTaskAdapter.h"

#include <assets/AssetRegistry.h>
#include <ecs/GlobalTaskRegistry.h>
#include <ecs/components/Colonist.h>

#include <algorithm>
#include <cmath>
#include <format>

namespace world_sim::adapters {

namespace {

/// Get priority for task type (lower = higher priority in display)
uint8_t getTaskTypePriority(ecs::TaskType type) {
	switch (type) {
		case ecs::TaskType::FulfillNeed:
			return 0; // Highest priority - active survival
		case ecs::TaskType::Craft:
			return 1;
		case ecs::TaskType::Haul:
			return 2;
		case ecs::TaskType::PlacePackaged:
			return 3;
		case ecs::TaskType::Gather:
			return 4;
		case ecs::TaskType::Wander:
			return 5;
		case ecs::TaskType::None:
		default:
			return 255;
	}
}

/// Get task type display prefix (e.g., "Harvest", "Haul")
std::string getTaskTypePrefix(ecs::TaskType type) {
	switch (type) {
		case ecs::TaskType::Gather:
			return "Harvest";
		case ecs::TaskType::Haul:
			return "Haul";
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

/// Get display label for an asset from its defNameId
std::string getAssetLabel(uint32_t defNameId) {
	auto& registry = engine::assets::AssetRegistry::Get();
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

/// Get colonist name from EntityID
std::string getColonistName(ecs::World& world, ecs::EntityID colonistId) {
	for (auto [entity, colonist] : world.view<ecs::Colonist>()) {
		if (entity == colonistId) {
			return colonist.name;
		}
	}
	return "Unknown";
}

/// Build "Known by: X, Y, Z" string from knownBy set
std::string buildKnownByString(ecs::World& world, const std::unordered_set<ecs::EntityID>& knownBy) {
	if (knownBy.empty()) {
		return "";
	}

	std::string result;
	bool first = true;
	for (ecs::EntityID colonistId : knownBy) {
		if (!first) {
			result += ", ";
		}
		result += getColonistName(world, colonistId);
		first = false;
	}
	return result;
}

/// Convert task to display data
GlobalTaskDisplayData taskToDisplayData(
	ecs::World& world,
	const ecs::GlobalTask& task,
	const glm::vec2& referencePosition,
	bool includeKnownBy,
	std::optional<ecs::EntityID> viewingColonist = std::nullopt
) {
	GlobalTaskDisplayData data;
	data.id = task.id;

	// Build description: "Harvest Berry Bush"
	std::string prefix = getTaskTypePrefix(task.type);
	std::string label = getAssetLabel(task.defNameId);
	data.description = prefix.empty() ? label : prefix + " " + label;

	// Format position: "(10, 15)"
	data.position = std::format("({}, {})",
		static_cast<int>(task.position.x),
		static_cast<int>(task.position.y));

	// Calculate distance
	float dx = task.position.x - referencePosition.x;
	float dy = task.position.y - referencePosition.y;
	data.distanceValue = std::sqrt(dx * dx + dy * dy);
	data.distance = std::format("{}m", static_cast<int>(data.distanceValue));

	// Status
	data.isReserved = task.isReserved();
	if (task.isReserved()) {
		if (viewingColonist.has_value() && task.isReservedBy(*viewingColonist)) {
			data.status = "In Progress";
			data.isMine = true;
		} else {
			std::string reserverName = getColonistName(world, *task.reservedBy);
			data.status = "Reserved by " + reserverName;
		}
	} else if (data.distanceValue > 30.0F) {
		data.status = "Far";
	} else {
		data.status = "Available";
	}

	// Known by (only for global view)
	if (includeKnownBy) {
		data.knownBy = buildKnownByString(world, task.knownBy);
	}

	data.taskTypePriority = getTaskTypePriority(task.type);

	return data;
}

} // anonymous namespace

std::vector<GlobalTaskDisplayData> getGlobalTasks(
	ecs::World& world,
	const glm::vec2& cameraCenter
) {
	auto& registry = ecs::GlobalTaskRegistry::Get();

	// Get all tasks
	auto allTasks = registry.getTasksMatching([](const ecs::GlobalTask&) { return true; });

	std::vector<GlobalTaskDisplayData> result;
	result.reserve(allTasks.size());

	for (const ecs::GlobalTask* task : allTasks) {
		result.push_back(taskToDisplayData(world, *task, cameraCenter, true));
	}

	return result;
}

std::vector<GlobalTaskDisplayData> getTasksForColonist(
	ecs::World& world,
	ecs::EntityID colonistId,
	const glm::vec2& colonistPosition
) {
	auto& registry = ecs::GlobalTaskRegistry::Get();

	// Get tasks known by this colonist
	auto colonistTasks = registry.getTasksFor(colonistId);

	std::vector<GlobalTaskDisplayData> result;
	result.reserve(colonistTasks.size());

	for (const ecs::GlobalTask* task : colonistTasks) {
		result.push_back(taskToDisplayData(world, *task, colonistPosition, false, colonistId));
	}

	return result;
}

void sortTasksForDisplay(std::vector<GlobalTaskDisplayData>& tasks) {
	std::sort(tasks.begin(), tasks.end(), [](const GlobalTaskDisplayData& a, const GlobalTaskDisplayData& b) {
		// 1. "Mine" tasks first (for colonist view)
		if (a.isMine != b.isMine) {
			return a.isMine;
		}

		// 2. Reserved tasks first
		if (a.isReserved != b.isReserved) {
			return a.isReserved;
		}

		// 3. By task type priority
		if (a.taskTypePriority != b.taskTypePriority) {
			return a.taskTypePriority < b.taskTypePriority;
		}

		// 4. By distance (closer first)
		if (std::abs(a.distanceValue - b.distanceValue) > 0.5F) {
			return a.distanceValue < b.distanceValue;
		}

		// 5. Stable sort by ID
		return a.id < b.id;
	});
}

} // namespace world_sim::adapters
