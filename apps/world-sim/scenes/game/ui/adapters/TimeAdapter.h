#pragma once

// TimeAdapter - Centralizes time queries from ECS TimeSystem.
//
// This adapter isolates ECS knowledge from the UI layer.
// Views call getTimeData() and receive domain types,
// without needing to know about ecs::TimeSystem internals.

#include <ecs/World.h>
#include <ecs/systems/TimeSystem.h>
#include <string>

namespace world_sim::adapters {

/// Data for time display (extracted from ECS TimeSystem)
struct TimeData {
	int day;			   // Day count since colony founding
	std::string season;	   // "Spring", "Summer", etc.
	int hour;			   // 0-23
	int minute;			   // 0-59
	ecs::GameSpeed speed;  // Current game speed
	bool isPaused;		   // Convenience for speed == Paused

	/// Format as "Day 15, Summer | 14:32"
	[[nodiscard]] std::string formatDisplay() const;
};

/// Query current time from the ECS world
/// @param world The ECS world to query
/// @return Time data for display
[[nodiscard]] TimeData getTimeData(const ecs::World& world);

}  // namespace world_sim::adapters
