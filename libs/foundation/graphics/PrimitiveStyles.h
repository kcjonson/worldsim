#pragma once

// Primitive Rendering Styles
//
// Style structs for primitive rendering API using C++20 designated initializers.
// Supports optional borders, gradients, and other visual effects.

#include "graphics/Color.h"
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

	// Two-stop linear gradient for rectangle fills. The gradient is realized by
	// the batch renderer as per-corner vertex colors interpolated across the quad,
	// so it composes with the rounded-corner/border SDF.
	struct LinearGradient {
		Color from;				 // start color (top, or left if horizontal)
		Color to;				 // end color (bottom, or right if horizontal)
		bool  horizontal = false; // false = vertical (top->bottom)
	};

	// Outer box-shadow / glow (CSS box-shadow, outset only). Rendered by the SDF
	// shader as a soft falloff over `blur` px outside the (optionally `spread`-
	// grown) shape, so it is one draw, perfectly rounded, no stacked geometry.
	struct BoxShadow {
		Color color = Color(0.0F, 0.0F, 0.0F, 0.5F);
		float blur = 8.0F;	   // softness radius (px)
		float spread = 0.0F;   // grow the shadow shape before blurring (px)
		Vec2  offset = {0.0F, 0.0F}; // shadow displacement (px); +y is down
	};

	// Rectangle visual style
	struct RectStyle {
		Color						  fill = Color::white(); // fallback when no gradient
		std::optional<BorderStyle>	  border = std::nullopt;
		std::optional<LinearGradient> gradient = std::nullopt;
		std::optional<BoxShadow>	  boxShadow = std::nullopt;
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
		bool			wordWrap = false; // CSS word-wrap: when true and width is set, text wraps
	};

} // namespace Foundation
