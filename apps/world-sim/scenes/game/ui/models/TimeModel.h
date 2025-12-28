#pragma once

// TimeModel - ViewModel for time display and speed control.
//
// This model:
// - Caches time data from the ECS TimeSystem
// - Detects changes between frames to avoid unnecessary UI rebuilds
// - Provides formatted display string for UI

#include "scenes/game/ui/adapters/TimeAdapter.h"
#include <ecs/World.h>

namespace world_sim {

class TimeModel {
  public:
	using TimeData = adapters::TimeData;

	/// Refresh data from ECS world
	/// @return true if data changed since last refresh
	bool refresh(const ecs::World& world);

	/// Get the cached time data
	[[nodiscard]] const TimeData& data() const { return timeData; }

	/// Get formatted display string "Day 15, Summer | 14:32"
	[[nodiscard]] const std::string& displayString() const { return cachedDisplayString; }

  private:
	TimeData timeData;
	std::string cachedDisplayString;

	// Change detection
	int lastDay = -1;
	int lastHour = -1;
	int lastMinute = -1;
	ecs::GameSpeed lastSpeed = ecs::GameSpeed::Normal;
	bool isFirstRefresh = true;
};

}  // namespace world_sim
