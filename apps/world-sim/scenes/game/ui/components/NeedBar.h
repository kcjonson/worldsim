#pragma once

// NeedBar - Progress bar for displaying colonist need values
//
// Wraps ProgressBar with need-specific coloring:
// - Red (0%) → Yellow (50%) → Green (100%)
// - Low values indicate depleted need (bad), high values indicate satisfied (good)
//
// Uses 0-100 scale for API compatibility (internally converts to 0-1).

#include <components/progress/ProgressBar.h>
#include <graphics/Color.h>
#include <math/Types.h>

#include <string>

namespace world_sim {

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

	// Update the bar value (0.0 - 100.0)
	void setValue(float newValue);

	// Update the label text
	void setLabel(const std::string& newLabel);

	// Update position (moves all child elements)
	void setPosition(Foundation::Vec2 newPos);

	// Get the total height including label
	[[nodiscard]] float getTotalHeight() const;

  private:
	// Calculate fill color based on value (green → yellow → red)
	[[nodiscard]] static Foundation::Color valueToColor(float newValue);

	// Handle to the progress bar child
	UI::LayerHandle progressBarHandle;

	float value = 100.0F;
	float height;

	// Layout constants (same as before for API compatibility)
	static constexpr float kLabelWidth = 60.0F;
	static constexpr float kBarGap = 5.0F;
	static constexpr float kLabelFontSize = 12.0F;
};

} // namespace world_sim
