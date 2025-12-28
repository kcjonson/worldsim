#pragma once

// TimeSystem - Manages game time advancement with speed control.
//
// This system runs first (priority 10) and provides:
// - Day/season/time tracking
// - Game speed control (pause, 1x, 3x, 10x)
// - Effective time scale for other systems to query
//
// Other systems should call effectiveTimeScale() to get
// speed-adjusted dt rather than using raw deltaTime.

#include "../ISystem.h"

namespace ecs {

/// Game speed settings
enum class GameSpeed {
	Paused = 0,
	Normal = 1,	  // 1x
	Fast = 2,	  // 3x
	VeryFast = 3  // 10x
};

/// Season enumeration
enum class Season {
	Spring = 0,
	Summer = 1,
	Fall = 2,
	Winter = 3
};

/// Returns display name for a season
[[nodiscard]] const char* seasonName(Season season);

/// Game time state snapshot (for UI/serialization)
struct GameTimeSnapshot {
	int day;			// Days since colony founding (1-indexed)
	Season season;		// Current season
	float timeOfDay;	// Hours (0.0 - 24.0)
	GameSpeed speed;	// Current speed setting
	bool isPaused;		// Convenience: speed == Paused
};

/// Manages game time advancement with speed control.
/// Priority: 10 (runs first, before all other systems)
class TimeSystem : public ISystem {
  public:
	void update(float deltaTime) override;

	[[nodiscard]] int priority() const override { return 10; }
	[[nodiscard]] const char* name() const override { return "Time"; }

	// --- Speed Control ---
	void setSpeed(GameSpeed speed);
	void pause();
	void resume();	// Returns to previous non-paused speed
	void togglePause();
	[[nodiscard]] GameSpeed speed() const { return currentSpeed; }
	[[nodiscard]] bool isPaused() const { return currentSpeed == GameSpeed::Paused; }

	// --- Time Queries ---
	[[nodiscard]] int day() const { return dayCount; }
	[[nodiscard]] Season season() const { return currentSeason; }
	[[nodiscard]] float timeOfDay() const { return currentTimeOfDay; }
	[[nodiscard]] GameTimeSnapshot snapshot() const;

	// --- Time Scale (for other systems) ---
	/// Returns the effective time multiplier for this frame (game-minutes per real-second)
	/// Returns 0.0 when paused
	[[nodiscard]] float effectiveTimeScale() const;

	// --- Configuration ---
	/// Set game-minutes per real-second at 1x speed (default: 1.0)
	void setBaseTimeScale(float gameMinutesPerSecond) { baseTimeScale = gameMinutesPerSecond; }

	/// Days per season (default: 15)
	void setDaysPerSeason(int days) { daysPerSeason = days; }

  private:
	// Time state
	int dayCount = 1;						   // Day 1 is the first day
	Season currentSeason = Season::Spring;
	float currentTimeOfDay = 6.0F;			   // Start at 6:00 AM

	// Speed state
	GameSpeed currentSpeed = GameSpeed::Normal;
	GameSpeed previousSpeed = GameSpeed::Normal;  // For pause/resume

	// Configuration
	float baseTimeScale = 1.0F;	 // Game-minutes per real-second at 1x
	int daysPerSeason = 15;

	// Speed multipliers
	static constexpr float kSpeedMultipliers[] = {0.0F, 1.0F, 3.0F, 10.0F};

	void advanceTime(float gameMinutes);
};

}  // namespace ecs
