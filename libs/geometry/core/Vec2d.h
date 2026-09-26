#pragma once

#include <cmath>

// Double-precision 2D point or vector for geometry built before quantization,
// such as river centerlines in world meters. Foundation::Vec2 is float, whose
// spacing is already 3 cm a few hundred kilometers from the origin, too coarse
// to quantize to the millimeter. Committed geometry is still Vec2i64.

namespace geometry {

	struct Vec2d {
		double x = 0.0;
		double y = 0.0;

		bool operator==(const Vec2d& rhs) const { return x == rhs.x && y == rhs.y; }
		bool operator!=(const Vec2d& rhs) const { return !(*this == rhs); }

		Vec2d operator+(const Vec2d& rhs) const { return {x + rhs.x, y + rhs.y}; }
		Vec2d operator-(const Vec2d& rhs) const { return {x - rhs.x, y - rhs.y}; }
		Vec2d operator-() const { return {-x, -y}; }
		Vec2d operator*(double s) const { return {x * s, y * s}; }
	};

	inline Vec2d operator*(double s, const Vec2d& v) { return v * s; }

	inline double length(const Vec2d& v) { return std::hypot(v.x, v.y); }

} // namespace geometry
