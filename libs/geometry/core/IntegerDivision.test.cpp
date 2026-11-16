#include "IntegerDivision.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

using geometry::ceilDiv;
using geometry::floorDiv;

// Both round toward their infinity on either side of zero, where `/` would round
// toward zero, and are exact on multiples.
TEST(IntegerDivision, FloorAndCeilOnBothSidesOfZero) {
	EXPECT_EQ(floorDiv(7, 2), 3);
	EXPECT_EQ(floorDiv(-7, 2), -4);
	EXPECT_EQ(floorDiv(-1, 16000), -1);
	EXPECT_EQ(floorDiv(-16000, 16000), -1);
	EXPECT_EQ(floorDiv(16000, 16000), 1);
	EXPECT_EQ(floorDiv(0, 16000), 0);
	EXPECT_EQ(floorDiv(15999, 16000), 0);

	EXPECT_EQ(ceilDiv(7, 2), 4);
	EXPECT_EQ(ceilDiv(-7, 2), -3);
	EXPECT_EQ(ceilDiv(-16000, 16000), -1);
	EXPECT_EQ(ceilDiv(16000, 16000), 1);
	EXPECT_EQ(ceilDiv(1, 16000), 1);
	EXPECT_EQ(ceilDiv(0, 16000), 0);
}

// ceilDiv used to compute -floorDiv(-a, b), which negates the numerator and
// overflows for INT64_MIN. These pin both functions at the int64 range
// boundary so that regression can't come back silently.
TEST(IntegerDivision, Int64BoundaryNumerators) {
	constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();
	constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

	// b == 1: identity division, exact either way.
	EXPECT_EQ(floorDiv(kMin, 1), kMin);
	EXPECT_EQ(ceilDiv(kMin, 1), kMin);
	EXPECT_EQ(floorDiv(kMax, 1), kMax);
	EXPECT_EQ(ceilDiv(kMax, 1), kMax);

	// b == 7: inexact for kMin, exact for kMax (2^63 - 1 is a multiple of 7).
	EXPECT_EQ(floorDiv(kMin, 7), -1317624576693539402LL);
	EXPECT_EQ(ceilDiv(kMin, 7), -1317624576693539401LL);
	EXPECT_EQ(floorDiv(kMax, 7), 1317624576693539401LL);
	EXPECT_EQ(ceilDiv(kMax, 7), 1317624576693539401LL);

	// b == 1000: inexact for both.
	EXPECT_EQ(floorDiv(kMin, 1000), -9223372036854776LL);
	EXPECT_EQ(ceilDiv(kMin, 1000), -9223372036854775LL);
	EXPECT_EQ(floorDiv(kMax, 1000), 9223372036854775LL);
	EXPECT_EQ(ceilDiv(kMax, 1000), 9223372036854776LL);
}
