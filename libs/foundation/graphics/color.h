#pragma once

// Color type for RGBA color representation.
// Used throughout the rendering system for specifying colors.
// Stores components as floats in range [0.0, 1.0] with alpha channel.

#include "math/types.h"

namespace Foundation { // NOLINT(readability-identifier-naming)

	struct Color {
		float r, g, b, a;

		// Constructors
		constexpr Color()
			: r(0.0F),
			  g(0.0F),
			  b(0.0F),
			  a(1.0F) {}

		constexpr Color(float r, float g, float b, float a = 1.0F)
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
		static constexpr Color White() { return {1.0F, 1.0F, 1.0F, 1.0F}; }
		static constexpr Color Black() { return {0.0F, 0.0F, 0.0F, 1.0F}; }
		static constexpr Color Red() { return {1.0F, 0.0F, 0.0F, 1.0F}; }
		static constexpr Color Green() { return {0.0F, 1.0F, 0.0F, 1.0F}; }
		static constexpr Color Blue() { return {0.0F, 0.0F, 1.0F, 1.0F}; }
		static constexpr Color Yellow() { return {1.0F, 1.0F, 0.0F, 1.0F}; }
		static constexpr Color Cyan() { return {0.0F, 1.0F, 1.0F, 1.0F}; }
		static constexpr Color Magenta() { return {1.0F, 0.0F, 1.0F, 1.0F}; }
		static constexpr Color Transparent() { return {0.0F, 0.0F, 0.0F, 0.0F}; }
	};

} // namespace Foundation
