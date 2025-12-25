#pragma once

// NeedBar - Progress bar for displaying colonist need values
//
// Displays a horizontal bar with:
// - Label text (left)
// - Background bar
// - Fill bar (width proportional to value 0-100%)
// - Color coding (green → yellow → red based on value)
//
// Uses Container-based UI tree pattern (extends Component, uses addChild).

#include <component/Component.h>
#include <graphics/Color.h>
#include <layer/Layer.h>
#include <shapes/Shapes.h>

#include <string>

namespace world_sim {

/// A horizontal progress bar for displaying need values
class NeedBar : public UI::Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		float			 width = 120.0F;
		float			 height = 12.0F;
		std::string		 label;
		std::string		 id = "need_bar";
	};

	explicit NeedBar(const Args& args);

	/// Update the bar value (0.0 - 100.0)
	void setValue(float newValue);

	/// Update the label text
	void setLabel(const std::string& newLabel);

	/// Update position (moves all child elements)
	void setPosition(Foundation::Vec2 newPos);

	/// Get the total height including label
	[[nodiscard]] float getTotalHeight() const;

  private:
	/// Calculate fill color based on value (green → yellow → red)
	[[nodiscard]] static Foundation::Color valueToColor(float newValue);

	// Handles to child shapes for dynamic updates
	UI::LayerHandle labelHandle;
	UI::LayerHandle backgroundHandle;
	UI::LayerHandle fillHandle;

	float value = 100.0F;
	float width;
	float height;
	float barWidth; // Cached bar width for setValue updates
	Foundation::Vec2 currentPosition; // Current position for setPosition updates

	// Layout constants
	static constexpr float kLabelWidth = 60.0F;
	static constexpr float kBarGap = 5.0F;
	static constexpr float kLabelFontSize = 12.0F;
};

} // namespace world_sim
