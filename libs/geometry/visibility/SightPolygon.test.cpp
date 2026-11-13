#include "visibility/SightPolygon.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using geometry::Vec2i64;

// The direction table, made at compile time, is the same on every toolchain: a compiler whose
// constant evaluation rounds differently fails here, not in a sight shape that differs by machine.
TEST(SightPolygon, DirectionTableIsPinned) {
	static_assert(geometry::kSightSides == 64, "the pinned table is for kSightSides; pin the new table when it changes");
	constexpr std::array<std::array<std::int64_t, 2>, 64> kPinned{{
		{1073741824, 0}, {1068571464, 105245103}, {1053110176, 209476638}, {1027506862, 311690799},
		{992008094, 410903207}, {946955747, 506158392}, {892783698, 596538995}, {830013654, 681174602},
		{759250125, 759250125}, {681174602, 830013654}, {596538995, 892783698}, {506158392, 946955747},
		{410903207, 992008094}, {311690799, 1027506862}, {209476638, 1053110176}, {105245103, 1068571464},
		{0, 1073741824}, {-105245103, 1068571464}, {-209476638, 1053110176}, {-311690799, 1027506862},
		{-410903207, 992008094}, {-506158392, 946955747}, {-596538995, 892783698}, {-681174602, 830013654},
		{-759250125, 759250125}, {-830013654, 681174602}, {-892783698, 596538995}, {-946955747, 506158392},
		{-992008094, 410903207}, {-1027506862, 311690799}, {-1053110176, 209476638}, {-1068571464, 105245103},
		{-1073741824, 0}, {-1068571464, -105245103}, {-1053110176, -209476638}, {-1027506862, -311690799},
		{-992008094, -410903207}, {-946955747, -506158392}, {-892783698, -596538995}, {-830013654, -681174602},
		{-759250125, -759250125}, {-681174602, -830013654}, {-596538995, -892783698}, {-506158392, -946955747},
		{-410903207, -992008094}, {-311690799, -1027506862}, {-209476638, -1053110176}, {-105245103, -1068571464},
		{0, -1073741824}, {105245103, -1068571464}, {209476638, -1053110176}, {311690799, -1027506862},
		{410903207, -992008094}, {506158392, -946955747}, {596538995, -892783698}, {681174602, -830013654},
		{759250125, -759250125}, {830013654, -681174602}, {892783698, -596538995}, {946955747, -506158392},
		{992008094, -410903207}, {1027506862, -311690799}, {1053110176, -209476638}, {1068571464, -105245103}}};
	EXPECT_EQ(geometry::kSightDirections, kPinned);
	EXPECT_EQ(geometry::kSightCosHalfSide, 1072448455);
}

// Every vertex lies within the radius, every side at least sightInradius from the centre, and the
// polygon runs counter-clockwise from a vertex along +x.
TEST(SightPolygon, LiesInsideTheCircle) {
	std::mt19937			  rng(64);
	std::vector<std::int64_t> radii{1000, 1999, 29950, 30000, 150000, 2172000};
	for (int i = 0; i < 200; ++i) {
		const std::int64_t radius = 1000 + static_cast<std::int64_t>(rng() % 300000);
		radii.push_back(radius);
	}
	for (const std::int64_t radius : radii) {
		for (const Vec2i64 centre : {Vec2i64{0, 0}, Vec2i64{-123457, 98765}, Vec2i64{37123456, -18456789}}) {
			const std::vector<Vec2i64> polygon = geometry::sightPolygon(centre, radius);
			ASSERT_EQ(polygon.size(), geometry::kSightSides);
			EXPECT_GT(polygon[0].x, centre.x);
			EXPECT_EQ(polygon[0].y, centre.y);
			for (std::size_t k = 0; k < polygon.size(); ++k) {
				const Vec2i64 offset = polygon[k] - centre;
				EXPECT_LE(geometry::dot(offset, offset), geometry::Int128::product(radius, radius)) << "radius " << radius << " vertex " << k;
				const Vec2i64& from = polygon[k];
				const Vec2i64& to	= polygon[(k + 1) % polygon.size()];
				const double   sideX	= static_cast<double>(to.x - from.x);
				const double   sideY	= static_cast<double>(to.y - from.y);
				const double   side = (sideX * static_cast<double>(centre.y - from.y) - sideY * static_cast<double>(centre.x - from.x)) / std::sqrt(sideX * sideX + sideY * sideY);
				EXPECT_GE(side, static_cast<double>(geometry::sightInradius(radius))) << "radius " << radius << " side " << k;
			}
		}
	}
}
