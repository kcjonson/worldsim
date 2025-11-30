#pragma once

// Primitive Rendering Styles
//
// Style structs for primitive rendering API using C++20 designated initializers.
// Supports optional borders, gradients, and other visual effects.

#include "graphics/color.h"
#include <optional>

namespace Foundation { // NOLINT(readability-identifier-naming)

	// Border positioning mode (CSS-like)
	enum class BorderPosition {
		Inside, // Border drawn inside rect bounds (content area reduced)
		Center, // Border centered on rect edge (default, matches CSS)
		Outside // Border drawn outside rect bounds (total size increased)
	};

	// Optional border style for rectangles
	struct BorderStyle {
		Color		   color = Color::white();
		float		   width = 1.0F;
		float		   cornerRadius = 0.0F;
		BorderPosition position = BorderPosition::Center;
	};

	// Rectangle visual style
	struct RectStyle {
		Color					   fill = Color::white();
		std::optional<BorderStyle> border = std::nullopt;
	};

	// Line visual style
	struct LineStyle {
		Color color = Color::white();
		float width = 1.0F;
	};

	// Circle visual style
	struct CircleStyle {
		Color					   fill = Color::white();
		std::optional<BorderStyle> border = std::nullopt;
	};

	// Text horizontal alignment
	enum class HorizontalAlign { Left, Center, Right };

	// Text vertical alignment
	enum class VerticalAlign { Top, Middle, Bottom };

	// Text visual style
	struct TextStyle {
		Color			color = Color::white();
		float			fontSize = 16.0F;
		HorizontalAlign hAlign = HorizontalAlign::Left;
		VerticalAlign	vAlign = VerticalAlign::Top;
	};

} // namespace Foundation
