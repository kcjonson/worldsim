#pragma once

// Directions round a circle as integers, made at compile time with + - x / alone, so every
// toolchain builds the same table from the same side count and scale. Each table built from it
// is pinned by a test (LimitPolygon.test.cpp, SightPolygon.test.cpp).

#include <array>
#include <cstddef>
#include <cstdint>

namespace geometry {

	namespace direction_table_detail {
		constexpr double kPi = 3.14159265358979323846;

		// The Taylor series of cos (sine false) or sin (sine true) at |x| <= pi, summed until a term
		// stops changing the sum.
		consteval double series(double x, bool sine) {
			double term = sine ? x : 1.0;
			double sum	= term;
			for (int k = 0; k < 60; ++k) {
				term = -term * x * x / static_cast<double>((2 * k + 1 + (sine ? 1 : 0)) * (2 * k + 2 + (sine ? 1 : 0)));
				if (sum + term == sum) {
					break;
				}
				sum += term;
			}
			return sum;
		}

		consteval std::int64_t roundNearest(double x) {
			return x >= 0.0 ? static_cast<std::int64_t>(x + 0.5) : -static_cast<std::int64_t>(-x + 0.5);
		}
	} // namespace direction_table_detail

	// cos and sin of k turns over Sides, times `scale`, rounded to nearest; direction 0 is +x and
	// they run counter-clockwise.
	template <std::size_t Sides> consteval std::array<std::array<std::int64_t, 2>, Sides> directionTable(std::int64_t scale) {
		std::array<std::array<std::int64_t, 2>, Sides> out{};
		constexpr auto								   n = static_cast<std::int64_t>(Sides);
		for (std::int64_t k = 0; k < n; ++k) {
			const std::int64_t turn	 = 2 * k <= n ? k : k - n;
			const double	   angle = 2.0 * direction_table_detail::kPi * static_cast<double>(turn) / static_cast<double>(n);
			out[static_cast<std::size_t>(k)] = {direction_table_detail::roundNearest(direction_table_detail::series(angle, false) * static_cast<double>(scale)),
												direction_table_detail::roundNearest(direction_table_detail::series(angle, true) * static_cast<double>(scale))};
		}
		return out;
	}

	// cos(pi / Sides) times `scale`, rounded to nearest: a side's distance from the centre over the
	// circumradius.
	template <std::size_t Sides> consteval std::int64_t cosineOfHalfSide(std::int64_t scale) {
		return direction_table_detail::roundNearest(direction_table_detail::series(direction_table_detail::kPi / static_cast<double>(Sides), false) *
													static_cast<double>(scale));
	}

} // namespace geometry
