#include "scenes/game/dev/GroundRefusal.h"

#include "scenes/game/world/CreekWorldFixture.test.h"

#include <gtest/gtest.h>

namespace {

	using world_sim::testing::kCreekY;

	class GroundRefusalTest : public ::testing::Test {
	  protected:
		ecs::World				world;
		ecs::EmptyPlacement		emptyPlacement;
		ecs::NavigationSystem&	navigation = world_sim::testing::meshCreekWorld(world, emptyPlacement);
	};

} // namespace

// Nothing is built in a stream a colonist could wade: a freestanding wall across the creek, or ending
// in it, is refused; one on dry ground beside it is not.
TEST_F(GroundRefusalTest, WallChainInTheCreekIsRefusedAndOnDryGroundIsTaken) {
	ASSERT_TRUE(navigation.isValidPosition({10.0F, kCreekY})) << "a colonist wades the creek";
	EXPECT_FALSE(world_sim::wallChainGroundRefusal(&navigation, {{10.0F, kCreekY - 3.0F}, {10.0F, kCreekY + 3.0F}}).empty()) << "across the creek";
	EXPECT_FALSE(world_sim::wallChainGroundRefusal(&navigation, {{6.0F, kCreekY - 3.0F}, {10.0F, kCreekY}}).empty()) << "ending in it";
	EXPECT_TRUE(world_sim::wallChainGroundRefusal(&navigation, {{6.0F, kCreekY - 3.0F}, {14.0F, kCreekY - 3.0F}, {14.0F, kCreekY - 6.0F}}).empty());
	EXPECT_TRUE(world_sim::wallChainGroundRefusal(nullptr, {{10.0F, kCreekY - 3.0F}, {10.0F, kCreekY + 3.0F}}).empty()) << "headless takes anything";
}
