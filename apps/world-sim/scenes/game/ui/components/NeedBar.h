#pragma once

// NeedBar - colonist need bar in the Salvage look.
//
// Wraps the unified ProgressBar with auto-toning: the value bands the fill color
// (crit/warn/ok), so a depleted need reads red and a satisfied one green. Uses a
// 0-100 scale for API compatibility (internally converts to 0-1).

#include <components/progress/ProgressBar.h>
#include <graphics/Color.h>
#include <math/Types.h>

#include <string>

namespace world_sim {

/// Size variants for NeedBar
enum class NeedBarSize {
	Normal, // Standard needs panel bar
	Compact // Smaller header mood bar
};

class NeedBar : public UI::Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		float			 width = 120.0F;
		float			 height = 0.0F; // 0 = size default
		NeedBarSize		 size = NeedBarSize::Normal;
		std::string		 label;
		std::string		 id = "need_bar";
	};

	explicit NeedBar(const Args& args);

	/// Update the bar value (0.0 - 100.0)
	void setValue(float newValue);

	/// Update the label text
	void setLabel(const std::string& newLabel);

	/// Update position (moves the bar)
	void setPosition(Foundation::Vec2 newPos);

	/// Update width (for dynamic resizing in layouts)
	void setWidth(float newWidth);

	/// Get the total height
	[[nodiscard]] float getTotalHeight() const;

  private:
	UI::LayerHandle progressBarHandle;

	float		value = 100.0F;
	const float height; // Set once in constructor, immutable

	static constexpr float kNormalHeight = 16.0F;
	static constexpr float kCompactHeight = 10.0F;
};

} // namespace world_sim
