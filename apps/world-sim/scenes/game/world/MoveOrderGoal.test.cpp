#include "scenes/game/world/MoveOrderGoal.h"

#include "scenes/game/world/PlayerVisibilityFixture.test.h"

#include <gtest/gtest.h>

namespace {

	using world_sim::testing::kSeenPoint;
	using world_sim::testing::kSeenRadiusMeters;

	class MoveOrderGoalTest : public ::testing::Test {
	  protected:
		world_sim::testing::SeenWorld seen;
		ecs::EntityID				  colonist = seen.addColonist({0.0F, 0.0F}, 5.0F);
	};

} // namespace

TEST_F(MoveOrderGoalTest, SeenGoalIsOrderedAsClicked) {
	const auto goal = world_sim::moveOrderGoal(seen.world, colonist, kSeenPoint);
	ASSERT_TRUE(goal.has_value());
	EXPECT_EQ(*goal, kSeenPoint);
}

TEST_F(MoveOrderGoalTest, UnseenGoalBecomesTheSeenEdgeOnTheWayThere) {
	const glm::vec2 clicked{kSeenRadiusMeters * 3.0F, 0.0F};
	const auto		goal = world_sim::moveOrderGoal(seen.world, colonist, clicked);
	ASSERT_TRUE(goal.has_value());
	EXPECT_TRUE(seen.visibility.isSeen(*goal));
	EXPECT_NEAR(goal->y, 0.0F, 0.01F);
	EXPECT_GT(goal->x, kSeenRadiusMeters * 0.9F);
	EXPECT_LE(goal->x, kSeenRadiusMeters);
}

TEST_F(MoveOrderGoalTest, NothingSeenOnTheWayOrdersNothing) {
	const ecs::EntityID stranded = seen.addColonist({500.0F, 0.0F}, 5.0F);
	EXPECT_FALSE(world_sim::moveOrderGoal(seen.world, stranded, {600.0F, 0.0F}).has_value());
}

TEST_F(MoveOrderGoalTest, RevealOrdersTheClickedPoint) {
	seen.visibility.setRevealed(true);
	const glm::vec2 clicked{kSeenRadiusMeters * 3.0F, 0.0F};
	const auto		goal = world_sim::moveOrderGoal(seen.world, colonist, clicked);
	ASSERT_TRUE(goal.has_value());
	EXPECT_EQ(*goal, clicked);
}
