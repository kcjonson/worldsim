#pragma once

// GlobalTaskListModel - ViewModel for the global task list panel
//
// This model:
// - Caches task data from GlobalTaskRegistry via GlobalTaskAdapter
// - Throttles refresh rate to 5Hz (every 0.2s) to reduce cost
// - Detects changes between refreshes to avoid unnecessary UI rebuilds
// - Tracks total task count for collapsed panel display
//
// Usage:
//   GlobalTaskListModel model;
//   if (model.refresh(world, cameraCenter, deltaTime)) {
//       // Data changed, rebuild UI
//       rebuildUI(model.tasks());
//   }

#include "scenes/game/ui/adapters/GlobalTaskAdapter.h"

#include <ecs/World.h>

#include <glm/vec2.hpp>

#include <vector>

namespace world_sim {

class GlobalTaskListModel {
  public:
	using TaskData = adapters::GlobalTaskDisplayData;

	/// Refresh data from GlobalTaskRegistry (throttled to 5Hz)
	/// @param world The ECS world (for colonist name lookups)
	/// @param cameraCenter Camera position for distance calculations
	/// @param deltaTime Time since last frame (seconds)
	/// @return true if data changed since last refresh
	bool refresh(ecs::World& world, const glm::vec2& cameraCenter, float deltaTime);

	/// Get the cached task data (already sorted for display)
	[[nodiscard]] const std::vector<TaskData>& tasks() const { return tasksData; }

	/// Get total task count (for collapsed panel display: "Tasks (N)")
	[[nodiscard]] size_t taskCount() const { return tasksData.size(); }

	/// Force a refresh on next call (bypasses throttle)
	void invalidate() { forceRefresh = true; }

  private:
	/// Compare new data with cached data
	[[nodiscard]] bool hasChanged(const std::vector<TaskData>& newData) const;

	/// Cached task data from last refresh (sorted)
	std::vector<TaskData> tasksData;

	/// Throttle timer (accumulates until kRefreshInterval)
	float timeSinceRefresh = 0.0F;

	/// Refresh interval in seconds (5Hz = 0.2s)
	static constexpr float kRefreshInterval = 0.2F;

	/// Track first refresh (always return true)
	bool isFirstRefresh = true;

	/// Force refresh flag
	bool forceRefresh = false;
};

} // namespace world_sim
