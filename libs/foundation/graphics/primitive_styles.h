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

} // namespace Foundation
