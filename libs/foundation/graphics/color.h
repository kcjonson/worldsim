#pragma once

// Color type for RGBA color representation.
// Used throughout the rendering system for specifying colors.
// Stores components as floats in range [0.0, 1.0] with alpha channel.

#include "math/types.h"

namespace Foundation {

	struct Color {
		float r, g, b, a;

		// Constructors
		constexpr Color()
			: r(0.0f),
			  g(0.0f),
			  b(0.0f),
			  a(1.0f) {}

		constexpr Color(float r, float g, float b, float a = 1.0f)
			: r(r),
			  g(g),
			  b(b),
			  a(a) {}

		explicit constexpr Color(const Vec4& v)
			: r(v.x),
			  g(v.y),
			  b(v.z),
			  a(v.w) {}

		// Conversion to Vec4
		Vec4 ToVec4() const { return Vec4(r, g, b, a); }

		// Common colors
		static constexpr Color White() { return Color(1.0f, 1.0f, 1.0f, 1.0f); }
		static constexpr Color Black() { return Color(0.0f, 0.0f, 0.0f, 1.0f); }
		static constexpr Color Red() { return Color(1.0f, 0.0f, 0.0f, 1.0f); }
		static constexpr Color Green() { return Color(0.0f, 1.0f, 0.0f, 1.0f); }
		static constexpr Color Blue() { return Color(0.0f, 0.0f, 1.0f, 1.0f); }
		static constexpr Color Yellow() { return Color(1.0f, 1.0f, 0.0f, 1.0f); }
		static constexpr Color Cyan() { return Color(0.0f, 1.0f, 1.0f, 1.0f); }
		static constexpr Color Magenta() { return Color(1.0f, 0.0f, 1.0f, 1.0f); }
		static constexpr Color Transparent() { return Color(0.0f, 0.0f, 0.0f, 0.0f); }
	};

} // namespace Foundation
