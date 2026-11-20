#include "TimeAdapter.h"

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
	const auto			 snapshot = timeSystem.snapshot();
	const ecs::ClockTime clock = timeSystem.clockTime();

	return TimeData{
		.day = clock.day,
		.season = ecs::seasonName(snapshot.season),
		.hour = clock.minuteOfDay / 60,
		.minute = clock.minuteOfDay % 60,
		.speed = snapshot.speed,
		.isPaused = snapshot.isPaused
	};
}

}  // namespace world_sim::adapters
