#pragma once

// A mover's visibility limit as a ground outline: a regular polygon round where he stands,
// circumscribed so the disc of his range lies inside. Built in integers from a table made at
// compile time (core/DirectionTable.h), so every toolchain draws the same polygon from the same
// centre and range (LimitPolygon.test.cpp pins the table).

#include "../core/DirectionTable.h"
#include "../core/Vec2i64.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace geometry::nav {

	constexpr std::size_t  kLimitSides			= 16;
	constexpr std::int64_t kLimitDirectionScale = std::int64_t{1} << 30;

	// cos and sin of k turns over kLimitSides, times kLimitDirectionScale, rounded to nearest;
	// direction 0 is +x and they run counter-clockwise.
	constexpr std::array<std::array<std::int64_t, 2>, kLimitSides> kLimitDirections = directionTable<kLimitSides>(kLimitDirectionScale);

	// cos(pi / kLimitSides) times kLimitDirectionScale, rounded to nearest: a side's distance from
	// the centre over the circumradius.
	constexpr std::int64_t kLimitCosHalfSide = cosineOfHalfSide<kLimitSides>(kLimitDirectionScale);

	// a / b rounded to the nearest integer, halves away from zero; b > 0.
	inline std::int64_t roundedAway(std::int64_t a, std::int64_t b) {
		return a >= 0 ? (2 * a + b) / (2 * b) : -((-2 * a + b) / (2 * b));
	}

	// The limit polygon of circumradius `circumradius` round `centre`: vertex k is centre +
	// round(direction k x circumradius / kLimitDirectionScale). The products stay in int64
	// below a circumradius of 2^32 mm.
	inline std::vector<Vec2i64> limitPolygonOfCircumradius(const Vec2i64& centre, std::int64_t circumradius) {
		assert(circumradius > 0 && circumradius < (std::int64_t{1} << 32));
		std::vector<Vec2i64> out;
		out.reserve(kLimitSides);
		for (const auto& [c, s] : kLimitDirections) {
			out.push_back({centre.x + roundedAway(c * circumradius, kLimitDirectionScale), centre.y + roundedAway(s * circumradius, kLimitDirectionScale)});
		}
		return out;
	}

	// The circumradius whose limit polygon has every side at least `range` from its centre:
	// ceil(range / cos(pi / kLimitSides)) + 1 mm, the millimetre covering the vertices' rounding.
	inline std::int64_t limitCircumradius(std::int64_t range) {
		return (range * kLimitDirectionScale + kLimitCosHalfSide - 1) / kLimitCosHalfSide + 1;
	}

	// The visibility limit of `range` round `centre`: the disc of that radius lies inside it.
	inline std::vector<Vec2i64> limitPolygon(const Vec2i64& centre, std::int64_t range) {
		return limitPolygonOfCircumradius(centre, limitCircumradius(range));
	}

} // namespace geometry::nav
