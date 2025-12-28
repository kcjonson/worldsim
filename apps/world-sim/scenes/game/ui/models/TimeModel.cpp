#include "TimeModel.h"

namespace world_sim {

bool TimeModel::refresh(const ecs::World& world) {
	timeData = adapters::getTimeData(world);

	// Check if anything display-relevant changed
	bool changed = isFirstRefresh || timeData.day != lastDay || timeData.hour != lastHour ||
				   timeData.minute != lastMinute || timeData.speed != lastSpeed;

	if (changed) {
		lastDay = timeData.day;
		lastHour = timeData.hour;
		lastMinute = timeData.minute;
		lastSpeed = timeData.speed;
		cachedDisplayString = timeData.formatDisplay();
		isFirstRefresh = false;
	}

	return changed;
}

}  // namespace world_sim
