#include "TimeSystem.h"

namespace ecs {

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
	return "Unknown";
}

void TimeSystem::update(float deltaTime) {
	if (currentSpeed == GameSpeed::Paused) {
		return;	 // Time frozen
	}

	float speedMultiplier = kSpeedMultipliers[static_cast<int>(currentSpeed)];
	float gameMinutes = deltaTime * baseTimeScale * speedMultiplier;
	advanceTime(gameMinutes);
}

void TimeSystem::advanceTime(float gameMinutes) {
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

float TimeSystem::effectiveTimeScale() const {
	return baseTimeScale * kSpeedMultipliers[static_cast<int>(currentSpeed)];
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

}  // namespace ecs
