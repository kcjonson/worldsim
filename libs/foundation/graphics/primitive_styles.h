#pragma once

// Primitive Rendering Styles
//
// Style structs for primitive rendering API using C++20 designated initializers.
// Supports optional borders, gradients, and other visual effects.

#include "graphics/color.h"
#include <optional>

namespace Foundation { // NOLINT(readability-identifier-naming)

	// Optional border style for rectangles
	struct BorderStyle {
		Color color = Color::White();
		float width = 1.0F;
		float cornerRadius = 0.0F;
	};

	// Rectangle visual style
	struct RectStyle {
		Color					   fill = Color::White();
		std::optional<BorderStyle> border = std::nullopt;
	};

	// Line visual style
	struct LineStyle {
		Color color = Color::White();
		float width = 1.0F;
	};

	// Circle visual style
	struct CircleStyle {
		Color					   fill = Color::White();
		std::optional<BorderStyle> border = std::nullopt;
	};

	// Text horizontal alignment
	enum class HorizontalAlign {
		Left,
		Center,
		Right
	};

	// Text vertical alignment
	enum class VerticalAlign {
		Top,
		Middle,
		Bottom
	};

	// Text visual style
	struct TextStyle {
		Color			color = Color::White();
		float			fontSize = 16.0F;
		HorizontalAlign hAlign = HorizontalAlign::Left;
		VerticalAlign	vAlign = VerticalAlign::Top;
	};

} // namespace Foundation
