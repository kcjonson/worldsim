#include "scenes/game/dev/PointList.h"

#include <gtest/gtest.h>

TEST(PointList, PairsComeBackInOrderAndBadPairsAreSkipped) {
	const auto points = world_sim::parsePointList("1,2;oops;3.5,-4;5,x;,6;7,8");
	ASSERT_EQ(points.size(), 3U);
	EXPECT_FLOAT_EQ(points[0].x, 1.0F);
	EXPECT_FLOAT_EQ(points[0].y, 2.0F);
	EXPECT_FLOAT_EQ(points[1].x, 3.5F);
	EXPECT_FLOAT_EQ(points[1].y, -4.0F);
	EXPECT_FLOAT_EQ(points[2].x, 7.0F);
	EXPECT_FLOAT_EQ(points[2].y, 8.0F);
	EXPECT_TRUE(world_sim::parsePointList("").empty());
}

TEST(PointList, TooFewPointsAreRefusedWithTheCountTheVerbNeeds) {
	EXPECT_EQ(
		world_sim::pointCountRefusal("foundation", 3, world_sim::parsePointList("0,0;4,0").size()),
		"foundation: pts needs at least 3 'x,y' pairs (got 2)"
	);
	EXPECT_EQ(world_sim::pointCountRefusal("walls", 2, 0), "walls: pts needs at least 2 'x,y' pairs (got 0)");
	EXPECT_TRUE(world_sim::pointCountRefusal("foundation", 3, 3).empty());
	EXPECT_TRUE(world_sim::pointCountRefusal("walls", 2, 5).empty());
}
