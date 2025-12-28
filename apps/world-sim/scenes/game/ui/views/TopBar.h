#pragma once

// TopBar - Top bar with date/time display and speed controls.
//
// Layout:
// ┌─────────────────────────────────────────────────────────────────┐
// │ Day 15, Summer | 14:32    [⏸][▶][▶▶][▶▶▶]              [Menu] │
// └─────────────────────────────────────────────────────────────────┘
//
// Positioned at the top of the screen, full width.
// Extends UI::Component to use the Layer system for child management.

#include "scenes/game/ui/components/DateTimeDisplay.h"
#include "scenes/game/ui/components/SpeedButton.h"
#include "scenes/game/ui/models/TimeModel.h"

#include <component/Component.h>
#include <components/button/Button.h>
#include <ecs/systems/TimeSystem.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>

namespace world_sim {

/// Top bar containing time display and speed controls.
class TopBar : public UI::Component {
  public:
	struct Args {
		std::function<void()> onPause = nullptr;
		std::function<void(ecs::GameSpeed)> onSpeedChange = nullptr;
		std::function<void()> onMenuClick = nullptr;
		std::string id = "top_bar";
	};

	explicit TopBar(const Args& args);

	/// Layout the top bar within viewport bounds
	void layout(const Foundation::Rect& bounds) override;

	/// Update from time model (call each frame)
	void updateData(const TimeModel& timeModel);

	/// Handle input events - delegates to children via dispatchEvent
	bool handleEvent(UI::InputEvent& event) override;

	// render() inherited from Component - auto-renders children

	/// Get height of the top bar
	[[nodiscard]] float getHeight() const override { return kBarHeight; }

  private:
	// Layout constants
	static constexpr float kBarHeight = 32.0F;
	static constexpr float kLeftPadding = 12.0F;
	static constexpr float kButtonSpacing = 4.0F;

	// Child handles
	UI::LayerHandle backgroundHandle;
	UI::LayerHandle dateTimeDisplayHandle;
	UI::LayerHandle pauseButtonHandle;
	UI::LayerHandle speed1ButtonHandle;
	UI::LayerHandle speed2ButtonHandle;
	UI::LayerHandle speed3ButtonHandle;
	UI::LayerHandle menuButtonHandle;

	// Callbacks
	std::function<void()> onPause;
	std::function<void(ecs::GameSpeed)> onSpeedChange;
	std::function<void()> onMenuClick;

	/// Update speed button active states
	void updateSpeedButtonStates(ecs::GameSpeed currentSpeed);

	/// Position all elements
	void positionElements();
};

}  // namespace world_sim
