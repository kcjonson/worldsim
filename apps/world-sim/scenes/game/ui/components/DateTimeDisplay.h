#pragma once

// DateTimeDisplay - Shows current game date and time.
//
// Displays: "Day 15, Summer | 14:32"
// Used in the TopBar for time information.
// Extends UI::Component to use the Layer system for child management.

#include <component/Component.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <string>

namespace world_sim {

/// Component for displaying game date and time.
class DateTimeDisplay : public UI::Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		std::string id = "datetime_display";
	};

	explicit DateTimeDisplay(const Args& args);

	/// Update the display text
	void setDateTime(const std::string& formattedTime);

	/// Update position
	void setPosition(float x, float y) override;

	// render() inherited from Component - auto-renders children

	/// Get width for layout
	[[nodiscard]] float getWidth() const override;

	/// Get height for layout
	[[nodiscard]] float getHeight() const override;

  private:
	UI::LayerHandle timeTextHandle;
};

}  // namespace world_sim
