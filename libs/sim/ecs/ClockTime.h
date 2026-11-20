#pragma once

namespace ecs {

/// A moment on the day clock, to the minute.
struct ClockTime {
	int day = 1;
	int minuteOfDay = 0;

	bool operator==(const ClockTime& other) const = default;
};

} // namespace ecs
