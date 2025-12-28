#pragma once

// DateTimeDisplay - Shows current game date and time.
//
// Displays: "Day 15, Summer | 14:32"
// Used in the TopBar for time information.

#include <shapes/Shapes.h>

#include <memory>
#include <string>

namespace world_sim {

/// Component for displaying game date and time.
class DateTimeDisplay {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		std::string id = "datetime_display";
	};

	explicit DateTimeDisplay(const Args& args);

	/// Update the display text
	void setDateTime(const std::string& formattedTime);

	/// Update position
	void setPosition(Foundation::Vec2 pos);

	/// Render the display
	void render();

	/// Get width for layout
	[[nodiscard]] float getWidth() const;

	/// Get height for layout
	[[nodiscard]] float getHeight() const;

  private:
	std::unique_ptr<UI::Text> timeText;
	Foundation::Vec2 position;
};

}  // namespace world_sim
