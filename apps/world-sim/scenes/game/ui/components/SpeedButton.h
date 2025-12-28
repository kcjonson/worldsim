#pragma once

// SpeedButton - Individual speed control button with SVG icon.
//
// Used in TopBar for pause/1x/3x/10x speed controls.
// Shows highlighted state when this speed is currently active.

#include <components/icon/Icon.h>
#include <input/InputEvent.h>
#include <shapes/Shapes.h>

#include <functional>
#include <memory>
#include <string>

namespace world_sim {

/// Speed control button with active state indicator.
class SpeedButton {
  public:
	struct Args {
		std::string iconPath;  // Path to SVG icon (e.g., "ui/icons/pause.svg")
		Foundation::Vec2 position{0.0F, 0.0F};
		std::function<void()> onClick = nullptr;
		std::string id = "speed_button";
	};

	explicit SpeedButton(const Args& args);

	/// Set whether this button represents the current speed
	void setActive(bool active);

	/// Check if active
	[[nodiscard]] bool isActive() const { return active; }

	/// Update position
	void setPosition(Foundation::Vec2 pos);

	/// Handle input events
	bool handleEvent(UI::InputEvent& event);

	/// Render the button
	void render();

	/// Get width for layout
	[[nodiscard]] float getWidth() const;

	/// Get height for layout
	[[nodiscard]] float getHeight() const;

  private:
	static constexpr float kButtonSize = 28.0F;
	static constexpr float kIconSize = 16.0F;

	Foundation::Vec2 position{0.0F, 0.0F};
	std::function<void()> onClick;
	std::string id;

	std::unique_ptr<UI::Rectangle> background;
	std::unique_ptr<UI::Icon> icon;

	bool active = false;
	bool hovered = false;
	bool pressed = false;

	void updateAppearance();
	[[nodiscard]] bool containsPoint(Foundation::Vec2 point) const;
};

}  // namespace world_sim
