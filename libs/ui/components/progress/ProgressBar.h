#pragma once

// ProgressBar - Generic progress/status bar component
//
// Displays a horizontal bar with:
// - Optional label text (left side)
// - Background bar with border
// - Fill bar (width proportional to value 0.0-1.0)
//
// Uses normalized 0.0-1.0 value range for flexibility.
// For need-specific coloring (red→yellow→green), see NeedBar which wraps this.

#include "component/Component.h"
#include "graphics/Color.h"
#include "layer/Layer.h"
#include "shapes/Shapes.h"
#include "theme/Theme.h"

#include <string>

namespace UI {

class ProgressBar : public Component {
  public:
	struct Args {
		Foundation::Vec2 position{0.0F, 0.0F};
		Foundation::Vec2 size{100.0F, 12.0F};
		float			 value{1.0F}; // 0.0 to 1.0 (normalized)
		Foundation::Color fillColor{Theme::Colors::statusActive};
		Foundation::Color backgroundColor{0.2F, 0.2F, 0.25F, 1.0F};
		Foundation::Color borderColor{0.3F, 0.3F, 0.35F, 1.0F};
		float			  borderWidth{1.0F};

		// Optional label (empty string = no label, bar takes full width)
		std::string		  label{};
		float			  labelWidth{60.0F};  // Width reserved for label
		float			  labelGap{5.0F};	   // Gap between label and bar
		Foundation::Color labelColor{1.0F, 1.0F, 1.0F, 1.0F};
		float			  labelFontSize{12.0F};

		const char* id = nullptr;
		float		margin{0.0F};
	};

	explicit ProgressBar(const Args& args);
	~ProgressBar() override = default;

	// Disable copy (owns child elements)
	ProgressBar(const ProgressBar&) = delete;
	ProgressBar& operator=(const ProgressBar&) = delete;

	// Allow move
	ProgressBar(ProgressBar&&) noexcept = default;
	ProgressBar& operator=(ProgressBar&&) noexcept = default;

	// Value control (0.0 to 1.0, clamped)
	void  setValue(float newValue);
	float getValue() const { return value; }

	// Appearance
	void setFillColor(Foundation::Color color);
	void setLabel(const std::string& newLabel);

	// Position update (moves all child elements)
	void setPosition(Foundation::Vec2 newPos);
	void setPosition(float x, float y) override { setPosition({x, y}); }

  private:
	float value;
	bool  hasLabel;
	float barWidth;		   // Computed bar width (total or after label)
	float borderWidth;	   // Cached for fill calculations
	float labelWidth;	   // Cached for position updates
	float labelGap;		   // Cached for position updates

	// Handles to child shapes for dynamic updates
	LayerHandle labelHandle;	  // Only valid if hasLabel
	LayerHandle backgroundHandle;
	LayerHandle fillHandle;
};

} // namespace UI
