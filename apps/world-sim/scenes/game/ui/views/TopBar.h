#pragma once

// TopBar - the colony command strip across the top of the screen (Salvage look).
//
// Left:   colony identity (name + "N survivors / Sol D" sub-line)
// Center: clock (Day D / season / HH:MM) followed by the speed-control pill
// Right:  Menu button
//
// The bar paints its own background, identity text, and clock inline via the
// Primitives API; the speed buttons and the menu button are child components.
// Full width, anchored to the top of the viewport.

#include "scenes/game/ui/components/SpeedButton.h"
#include "scenes/game/ui/models/TimeModel.h"

#include <component/Component.h>
#include <components/button/Button.h>
#include <ecs/systems/TimeSystem.h>
#include <graphics/Rect.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>

#include <functional>
#include <string>

namespace world_sim {

/// Top command strip: colony identity, clock + speed controls, menu.
class TopBar : public UI::Component {
  public:
	struct Args {
		std::function<void()> onPause = nullptr;
		std::function<void(ecs::GameSpeed)> onSpeedChange = nullptr;
		std::function<void()> onMenuClick = nullptr;
		std::string colonyName = "Hollow Reach";
		std::string id = "top_bar";
	};

	explicit TopBar(const Args& args);

	/// Layout the top bar within viewport bounds
	void layout(const Foundation::Rect& bounds) override;

	/// Refresh from the time model and current survivor count (call each frame)
	void updateData(const TimeModel& timeModel, int survivorCount);

	/// Handle input events - delegates to children via dispatchEvent
	bool handleEvent(UI::InputEvent& event) override;

	/// Paint the bar background, identity, and clock, then the child controls
	void render() override;

	/// Get height of the top bar
	[[nodiscard]] float getHeight() const override { return kBarHeight; }

  private:
	// Layout constants
	static constexpr float kBarHeight = 52.0F;
	static constexpr float kPadH = 12.0F;	   // horizontal edge padding
	static constexpr float kSpeedSpacing = 2.0F;
	static constexpr float kPillPadding = 4.0F;
	static constexpr float kMenuWidth = 64.0F;
	static constexpr float kMenuHeight = 28.0F;

	// Child handles (interactive controls only)
	UI::LayerHandle pauseButtonHandle;
	UI::LayerHandle speed1ButtonHandle;
	UI::LayerHandle speed2ButtonHandle;
	UI::LayerHandle speed3ButtonHandle;
	UI::LayerHandle menuButtonHandle;

	// Callbacks
	std::function<void()> onPause;
	std::function<void(ecs::GameSpeed)> onSpeedChange;
	std::function<void()> onMenuClick;

	// Cached display data (built in updateData, drawn in render)
	std::string colonyName;
	std::string dayStr;		   // "Day 14"
	std::string seasonStr;	   // "SPRING" (uppercased)
	std::string timeStr;	   // "09:42"
	std::string survivorStr;   // "3 survivors / Sol 14"

	// Computed layout (set by positionElements)
	float rowY = 0.0F;		   // vertical center of the bar
	float dayX = 0.0F;
	float seasonX = 0.0F;
	float timeX = 0.0F;
	Foundation::Rect pillRect{};

	void updateSpeedButtonStates(ecs::GameSpeed currentSpeed);

	/// Measure widths and place the clock cluster, speed pill, and menu button
	void positionElements();
};

}  // namespace world_sim
