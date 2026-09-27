#include "IntegerDivision.h"

#include <gtest/gtest.h>

using geometry::ceilDiv;
using geometry::floorDiv;

// Both round toward their infinity on either side of zero, where `/` would round
// toward zero, and are exact on multiples.
TEST(IntegerDivision, FloorAndCeilOnBothSidesOfZero) {
	EXPECT_EQ(floorDiv(7, 2), 3);
	EXPECT_EQ(floorDiv(-7, 2), -4);
	EXPECT_EQ(floorDiv(-1, 16000), -1);
	EXPECT_EQ(floorDiv(-16000, 16000), -1);
	EXPECT_EQ(floorDiv(0, 16000), 0);
	EXPECT_EQ(floorDiv(15999, 16000), 0);

	EXPECT_EQ(ceilDiv(7, 2), 4);
	EXPECT_EQ(ceilDiv(-7, 2), -3);
	EXPECT_EQ(ceilDiv(-16000, 16000), -1);
	EXPECT_EQ(ceilDiv(1, 16000), 1);
	EXPECT_EQ(ceilDiv(0, 16000), 0);
}
