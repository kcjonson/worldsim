#pragma once

// The visibility limit's polygon for tests and benchmarks: 16 sides round a centre, in
// integers so every toolchain draws the same.

#include "../core/Vec2i64.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace geometry::nav::testdata {

	// cos and sin of k x 22.5 degrees times 2^30, rounded to nearest, and cos(11.25 degrees)
	// likewise.
	constexpr std::int64_t									kDirectionScale = std::int64_t{1} << 30;
	constexpr std::array<std::array<std::int64_t, 2>, 16> kDirections{{{1073741824, 0},
																		{992008094, 410903207},
																		{759250125, 759250125},
																		{410903207, 992008094},
																		{0, 1073741824},
																		{-410903207, 992008094},
																		{-759250125, 759250125},
																		{-992008094, 410903207},
																		{-1073741824, 0},
																		{-992008094, -410903207},
																		{-759250125, -759250125},
																		{-410903207, -992008094},
																		{0, -1073741824},
																		{410903207, -992008094},
																		{759250125, -759250125},
																		{992008094, -410903207}}};
	constexpr std::int64_t									kCosHalfSide = 1053110176;

	// a / b rounded to the nearest integer, halves away from zero; b > 0.
	inline std::int64_t roundedAway(std::int64_t a, std::int64_t b) {
		return a >= 0 ? (2 * a + b) / (2 * b) : -((-2 * a + b) / (2 * b));
	}

	// A regular polygon with 16 sides of circumradius `radius` round `centre`, counter-
	// clockwise from a vertex along +x: vertex k is centre + round(direction k x radius /
	// 2^30). The products stay in int64 below a circumradius of 2^32 mm.
	inline std::vector<Vec2i64> sixteenGon(const Vec2i64& centre, std::int64_t radius) {
		assert(radius > 0 && radius < (std::int64_t{1} << 32));
		std::vector<Vec2i64> out;
		for (const auto& [c, s] : kDirections) {
			out.push_back({centre.x + roundedAway(c * radius, kDirectionScale), centre.y + roundedAway(s * radius, kDirectionScale)});
		}
		return out;
	}

	// The circumradius whose 16-gon has every side at least `range` from the centre:
	// ceil(range / cos(11.25 degrees)) + 1 mm, the millimetre covering the vertices' rounding.
	inline std::int64_t limitCircumradius(std::int64_t range) {
		return (range * kDirectionScale + kCosHalfSide - 1) / kCosHalfSide + 1;
	}

	inline std::vector<Vec2i64> limitPolygon(const Vec2i64& centre, std::int64_t range) {
		return sixteenGon(centre, limitCircumradius(range));
	}

} // namespace geometry::nav::testdata
