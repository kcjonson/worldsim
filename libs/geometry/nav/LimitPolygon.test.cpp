#include "nav/LimitPolygon.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

using geometry::Vec2i64;
using namespace geometry::nav;

// The direction table, made at compile time, is the same on every toolchain: a compiler whose
// constant evaluation rounds differently fails here, not in a mesh that differs by machine.
TEST(LimitPolygon, DirectionTableIsPinned) {
	static_assert(kLimitSides == 16, "the pinned table is for kLimitSides; pin the new table when it changes");
	constexpr std::array<std::array<std::int64_t, 2>, 16> kPinned{{{1073741824, 0},
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
	EXPECT_EQ(kLimitDirections, kPinned);
	EXPECT_EQ(kLimitCosHalfSide, 1053110176);
}

// Every side of a limit stands at least its range from the centre, and the polygon is
// counter-clockwise from a vertex along +x.
TEST(LimitPolygon, DiscOfTheRangeLiesInside) {
	for (const std::int64_t range : {std::int64_t{1}, std::int64_t{999}, std::int64_t{46000}, std::int64_t{150000}, std::int64_t{2172000}}) {
		for (const Vec2i64 centre : {Vec2i64{0, 0}, Vec2i64{-123457, 98765}, Vec2i64{511999, -1}}) {
			const std::vector<Vec2i64> limit = limitPolygon(centre, range);
			ASSERT_EQ(limit.size(), kLimitSides);
			EXPECT_GT(limit[0].x, centre.x);
			EXPECT_EQ(limit[0].y, centre.y);
			for (std::size_t k = 0; k < limit.size(); ++k) {
				const Vec2i64& a = limit[k];
				const Vec2i64& b = limit[(k + 1) % limit.size()];
				// The centre left of a->b by at least range: cross^2 >= range^2 x |ab|^2, in doubles
				// with a margin far below the millimetre the construction adds.
				const double dx	   = static_cast<double>(b.x - a.x);
				const double dy	   = static_cast<double>(b.y - a.y);
				const double cross = dx * static_cast<double>(centre.y - a.y) - dy * static_cast<double>(centre.x - a.x);
				EXPECT_GT(cross, 0.0) << "counter-clockwise";
				EXPECT_GE(cross / std::sqrt(dx * dx + dy * dy), static_cast<double>(range) + 0.25) << "range " << range << " side " << k;
			}
		}
	}
}
