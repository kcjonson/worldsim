#pragma once

// TopBar - Top bar with date/time display and speed controls.
//
// Layout:
// ┌─────────────────────────────────────────────────────────────────┐
// │ Day 15, Summer | 14:32    [⏸][▶][▶▶][▶▶▶]              [Menu] │
// └─────────────────────────────────────────────────────────────────┘
//
// Positioned at the top of the screen, full width.

#include "scenes/game/ui/components/DateTimeDisplay.h"
#include "scenes/game/ui/components/SpeedButton.h"
#include "scenes/game/ui/models/TimeModel.h"

#include <components/button/Button.h>
#include <ecs/systems/TimeSystem.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <shapes/Shapes.h>

#include <functional>
#include <memory>

namespace world_sim {

/// Top bar containing time display and speed controls.
class TopBar {
  public:
	struct Args {
		std::function<void()> onPause = nullptr;
		std::function<void(ecs::GameSpeed)> onSpeedChange = nullptr;
		std::function<void()> onMenuClick = nullptr;
		std::string id = "top_bar";
	};

	explicit TopBar(const Args& args);

	/// Layout the top bar within viewport bounds
	void layout(const Foundation::Rect& viewportBounds);

	/// Update from time model (call each frame)
	void update(const TimeModel& timeModel);

	/// Handle input events
	bool handleEvent(UI::InputEvent& event);

	/// Render the top bar
	void render();

	/// Get height of the top bar
	[[nodiscard]] float getHeight() const { return kBarHeight; }

  private:
	// Layout constants
	static constexpr float kBarHeight = 32.0F;
	static constexpr float kLeftPadding = 12.0F;
	static constexpr float kButtonSpacing = 4.0F;

	// Background
	std::unique_ptr<UI::Rectangle> background;

	// Components
	std::unique_ptr<DateTimeDisplay> dateTimeDisplay;
	std::unique_ptr<SpeedButton> pauseButton;
	std::unique_ptr<SpeedButton> speed1Button;
	std::unique_ptr<SpeedButton> speed2Button;
	std::unique_ptr<SpeedButton> speed3Button;
	std::unique_ptr<UI::Button> menuButton;

	// Callbacks
	std::function<void()> onPause;
	std::function<void(ecs::GameSpeed)> onSpeedChange;
	std::function<void()> onMenuClick;

	// Cached viewport
	Foundation::Rect viewportBounds;

	/// Update speed button active states
	void updateSpeedButtonStates(ecs::GameSpeed currentSpeed);

	/// Position all elements
	void positionElements();
};

}  // namespace world_sim
