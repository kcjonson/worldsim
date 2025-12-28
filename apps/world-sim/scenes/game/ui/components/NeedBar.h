#pragma once

// NeedBar - Progress bar for displaying colonist need values
//
// Wraps ProgressBar with need-specific coloring:
// - Red (0%) → Yellow (50%) → Green (100%)
// - Low values indicate depleted need (bad), high values indicate satisfied (good)
//
// Uses 0-100 scale for API compatibility (internally converts to 0-1).
//
// Supports two sizes:
// - Normal: Standard need bar for the needs panel (16px height, 75px label)
// - Compact: Smaller bar for header mood display (10px height, 45px label)

#include <components/progress/ProgressBar.h>
#include <graphics/Color.h>
#include <math/Types.h>

#include <string>

namespace world_sim {

/// Size variants for NeedBar
enum class NeedBarSize {
	Normal,	 // Standard needs panel bar (16px height)
	Compact	 // Smaller header mood bar (10px height)
};

class NeedBar : public UI::Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		float			 width = 120.0F;
		float			 height = 0.0F;	 // 0 = use size default (16px normal, 10px compact)
		NeedBarSize		 size = NeedBarSize::Normal;
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

	/// Update width (for dynamic resizing in layouts)
	void setWidth(float newWidth);

	/// Get the total height including label
	[[nodiscard]] float getTotalHeight() const;

  private:
	// Calculate fill color based on value (green → yellow → red)
	[[nodiscard]] static Foundation::Color valueToColor(float newValue);

	// Handle to the progress bar child
	UI::LayerHandle progressBarHandle;

	float		value = 100.0F;
	const float height;		// Set once in constructor, immutable
	const float labelWidth; // Set once based on size variant, immutable

	// Layout constants - Normal size (needs panel)
	static constexpr float kNormalHeight = 16.0F;
	static constexpr float kNormalLabelWidth = 75.0F;  // Wide enough for "Temperature"
	static constexpr float kNormalFontSize = 12.0F;

	// Layout constants - Compact size (header mood bar)
	static constexpr float kCompactHeight = 10.0F;
	static constexpr float kCompactLabelWidth = 45.0F;  // Enough for "Mood"
	static constexpr float kCompactFontSize = 10.0F;

	// Shared constants
	static constexpr float kBarGap = 5.0F;
};

} // namespace world_sim
