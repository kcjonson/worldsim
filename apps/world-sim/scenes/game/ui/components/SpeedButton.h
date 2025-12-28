#pragma once

// SpeedButton - Individual speed control button with SVG icon.
//
// Used in TopBar for pause/1x/3x/10x speed controls.
// Shows highlighted state when this speed is currently active.
// Extends UI::Component to use the Layer system for child management.

#include <component/Component.h>
#include <components/icon/Icon.h>
#include <input/InputEvent.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <functional>
#include <string>

namespace world_sim {

/// Speed control button with active state indicator.
class SpeedButton : public UI::Component {
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
	void setPosition(float x, float y) override;

	/// Handle input events
	bool handleEvent(UI::InputEvent& event) override;

	// render() inherited from Component - auto-renders children

	/// Get width for layout
	float getWidth() const override { return kButtonSize; }

	/// Get height for layout
	float getHeight() const override { return kButtonSize; }

  private:
	static constexpr float kButtonSize = 28.0F;
	static constexpr float kIconSize = 16.0F;

	std::function<void()> onClick;
	std::string id;

	UI::LayerHandle backgroundHandle;
	UI::LayerHandle iconHandle;

	bool active = false;
	bool hovered = false;
	bool pressed = false;

	void updateAppearance();
	void positionElements();
	[[nodiscard]] bool containsPoint(Foundation::Vec2 point) const override;
};

}  // namespace world_sim
