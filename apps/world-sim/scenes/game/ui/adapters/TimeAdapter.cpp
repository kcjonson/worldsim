#include "TimeAdapter.h"

#include <cmath>
#include <iomanip>
#include <sstream>

namespace world_sim::adapters {

std::string TimeData::formatDisplay() const {
	std::ostringstream ss;
	ss << "Day " << day << ", " << season << " | ";
	ss << std::setfill('0') << std::setw(2) << hour << ":";
	ss << std::setfill('0') << std::setw(2) << minute;
	return ss.str();
}

TimeData getTimeData(const ecs::World& world) {
	const auto& timeSystem = world.getSystem<ecs::TimeSystem>();
	auto snapshot = timeSystem.snapshot();

	// Convert float timeOfDay (in hours) to hour:minute using integer arithmetic
	// to avoid floating-point precision issues with fmod
	int totalMinutes = static_cast<int>(snapshot.timeOfDay * 60.0F);
	int hour = totalMinutes / 60;
	int minute = totalMinutes % 60;

	return TimeData{
		.day = snapshot.day,
		.season = ecs::seasonName(snapshot.season),
		.hour = hour,
		.minute = minute,
		.speed = snapshot.speed,
		.isPaused = snapshot.isPaused
	};
}

}  // namespace world_sim::adapters
