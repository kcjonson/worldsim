#include "TimeSystem.h"

#include "../World.h"

#include <utils/Log.h>

#include <cassert>
#include <cmath>
#include <cstdlib>

namespace ecs {

namespace {
	// Number of valid GameSpeed enum values
	constexpr int kSpeedMultiplierCount = 4;
}  // namespace

const char* seasonName(Season season) {
	switch (season) {
		case Season::Spring:
			return "Spring";
		case Season::Summer:
			return "Summer";
		case Season::Fall:
			return "Fall";
		case Season::Winter:
			return "Winter";
	}
	// All enum values are handled above. This should be unreachable
	// unless the enum value is corrupted or cast from an invalid int.
	return "Unknown";
}

void TimeSystem::update(float deltaTime) {
	if (currentSpeed == GameSpeed::Paused) {
		return;	 // Time frozen
	}

	int speedIndex = static_cast<int>(currentSpeed);
	assert(speedIndex >= 0 && speedIndex < kSpeedMultiplierCount && "Invalid GameSpeed enum value");
	float speedMultiplier = kSpeedMultipliers[speedIndex];
	float gameMinutes = deltaTime * baseTimeScale * speedMultiplier;
	advanceTime(gameMinutes);
}

void TimeSystem::advanceTime(float gameMinutes) {
	// A game minute on the day clock is one scaled second of simulation (deltaTime * effectiveTimeScale).
	elapsedSimulationSeconds += static_cast<double>(gameMinutes);

	// Convert minutes to hours and add to time of day
	float hoursToAdd = gameMinutes / 60.0F;
	currentTimeOfDay += hoursToAdd;

	// Handle day wrap
	while (currentTimeOfDay >= 24.0F) {
		currentTimeOfDay -= 24.0F;
		dayCount++;
	}

	// Calculate season from day count
	// Each year has 4 seasons of daysPerSeason each
	int totalDays = daysPerSeason * 4;
	int dayInYear = (dayCount - 1) % totalDays;
	int seasonIndex = dayInYear / daysPerSeason;
	currentSeason = static_cast<Season>(seasonIndex);
}

void TimeSystem::setSpeed(GameSpeed speed) {
	if (speed != GameSpeed::Paused) {
		previousSpeed = speed;
	}
	currentSpeed = speed;
}

void TimeSystem::pause() {
	if (currentSpeed != GameSpeed::Paused) {
		previousSpeed = currentSpeed;
		currentSpeed = GameSpeed::Paused;
	}
}

void TimeSystem::resume() {
	if (currentSpeed == GameSpeed::Paused) {
		currentSpeed = previousSpeed;
	}
}

void TimeSystem::togglePause() {
	if (isPaused()) {
		resume();
	} else {
		pause();
	}
}

void TimeSystem::setTimeOfDay(float hours) {
	hours = std::fmod(hours, 24.0F);
	if (hours < 0.0F) {
		hours += 24.0F;
	}
	currentTimeOfDay = hours;
}

void TimeSystem::skipTime(float gameMinutes) {
	if (gameMinutes > 0.0F) {
		advanceTime(gameMinutes);
	}
}

float TimeSystem::effectiveTimeScale() const {
	int speedIndex = static_cast<int>(currentSpeed);
	assert(speedIndex >= 0 && speedIndex < kSpeedMultiplierCount && "Invalid GameSpeed enum value");
	return baseTimeScale * kSpeedMultipliers[speedIndex];
}

GameTimeSnapshot TimeSystem::snapshot() const {
	return {
		.day = dayCount,
		.season = currentSeason,
		.timeOfDay = currentTimeOfDay,
		.speed = currentSpeed,
		.isPaused = isPaused()
	};
}

double simulationSeconds(World& world) {
	const auto* timeSystem = world.tryGetSystem<TimeSystem>();
	if (timeSystem == nullptr) {
		LOG_ERROR(Engine, "simulationSeconds: the world has no TimeSystem, so a regrowth would never end");
#ifdef DEVELOPMENT_BUILD
		std::abort();
#endif
		return 0.0;
	}
	return timeSystem->simulationSeconds();
}

}  // namespace ecs
