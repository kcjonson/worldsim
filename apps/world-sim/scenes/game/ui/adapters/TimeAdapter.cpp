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

	// Convert float timeOfDay to hour:minute
	int hour = static_cast<int>(snapshot.timeOfDay);
	int minute = static_cast<int>(std::fmod(snapshot.timeOfDay, 1.0F) * 60.0F);

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
