#pragma once

// Color type for RGBA color representation.
// Used throughout the rendering system for specifying colors.
// Stores components as floats in range [0.0, 1.0] with alpha channel.

#include "math/Types.h"

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
		Vec4 toVec4() const { return {r, g, b, a}; }

		// Common colors
		static constexpr Color white() { return {1.0F, 1.0F, 1.0F, 1.0F}; }
		static constexpr Color black() { return {0.0F, 0.0F, 0.0F, 1.0F}; }
		static constexpr Color red() { return {1.0F, 0.0F, 0.0F, 1.0F}; }
		static constexpr Color green() { return {0.0F, 1.0F, 0.0F, 1.0F}; }
		static constexpr Color blue() { return {0.0F, 0.0F, 1.0F, 1.0F}; }
		static constexpr Color yellow() { return {1.0F, 1.0F, 0.0F, 1.0F}; }
		static constexpr Color cyan() { return {0.0F, 1.0F, 1.0F, 1.0F}; }
		static constexpr Color magenta() { return {1.0F, 0.0F, 1.0F, 1.0F}; }
		static constexpr Color transparent() { return {0.0F, 0.0F, 0.0F, 0.0F}; }
	};

} // namespace Foundation
