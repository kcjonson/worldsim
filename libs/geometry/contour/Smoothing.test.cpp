#include "Smoothing.h"
#include "../polygon/Polygon.h"

#include <algorithm>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

TEST(Chaikin, SquareCutsQuarterPoints) {
	Ring r = {{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}};
	chaikin(r, 1);
	const Ring expected = {
		{1000, 0}, {3000, 0}, {4000, 1000}, {4000, 3000}, {3000, 4000}, {1000, 4000}, {0, 3000}, {0, 1000}
	};
	EXPECT_EQ(r, expected);
	EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
}

TEST(Chaikin, RoundsHalvesUpRegardlessOfSign) {
	// (3a + b) / 4 for a = (-2, 2), b = (0, 0) is (-1.5, 1.5): halves go up.
	Ring r = {{-2, 2}, {0, 0}, {5, 7}};
	chaikin(r, 1);
	EXPECT_EQ(r[0], (Vec2i64{-1, 2}));
}

TEST(Chaikin, DirectionIndependent) {
	// The same edges walked the other way produce the same points (as a set).
	Ring forward = {{3, -7}, {1003, 11}, {2291, 1717}, {-51, 2003}, {-977, 999}};
	Ring reverse(forward.rbegin(), forward.rend());
	chaikin(forward, 1);
	chaikin(reverse, 1);
	std::sort(forward.begin(), forward.end());
	std::sort(reverse.begin(), reverse.end());
	EXPECT_EQ(forward, reverse);
}

TEST(Chaikin, IterationsDoubleVertexCount) {
	Ring r = {{0, 0}, {8000, 0}, {8000, 8000}, {0, 8000}};
	chaikin(r, 2);
	EXPECT_EQ(r.size(), 16u);
	EXPECT_TRUE(isSimple(r).pass);
}

TEST(Chaikin, CollapsesDuplicatesOnTinyEdges) {
	// A 1 mm edge rounds both of its cut points onto integers already emitted.
	Ring r = {{0, 0}, {1, 0}, {1000, 1000}};
	chaikin(r, 1);
	for (std::size_t i = 0; i < r.size(); ++i) {
		EXPECT_NE(r[i], r[(i + 1) % r.size()]);
	}
}
