#include "World.h"

#include <gtest/gtest.h>

#include <vector>

using namespace ecs;

namespace {

	struct Marker {
		int value = 0;
	};

	class StaleEntityIdTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			stale = world.createEntity();
			world.addComponent<Marker>(stale, Marker{1});
			world.destroyEntity(stale);
			reuser = world.createEntity();
			ASSERT_EQ(getIndex(reuser), getIndex(stale)) << "precondition: the index is reused";
			ASSERT_NE(reuser, stale);
			world.addComponent<Marker>(reuser, Marker{2});
		}

		World	 world;
		EntityID stale = kInvalidEntity;
		EntityID reuser = kInvalidEntity;
	};

	TEST_F(StaleEntityIdTest, FindsNoComponent) {
		EXPECT_EQ(world.getComponent<Marker>(stale), nullptr);
		EXPECT_FALSE(world.hasComponent<Marker>(stale));
		EXPECT_EQ(world.getComponent<Marker>(kInvalidEntity), nullptr) << "the invalid id is index 0's stale id";
		ASSERT_NE(world.getComponent<Marker>(reuser), nullptr);
		EXPECT_EQ(world.getComponent<Marker>(reuser)->value, 2);
	}

	TEST_F(StaleEntityIdTest, RemoveLeavesTheNewOccupantsComponent) {
		world.removeComponent<Marker>(stale);
		ASSERT_NE(world.getComponent<Marker>(reuser), nullptr);
		EXPECT_EQ(world.getComponent<Marker>(reuser)->value, 2);
	}

	TEST_F(StaleEntityIdTest, AddIsRefused) {
		EXPECT_DEBUG_DEATH(world.addComponent<Marker>(stale, Marker{3}), "");
		EXPECT_FALSE(world.hasComponent<Marker>(stale));
		ASSERT_NE(world.getComponent<Marker>(reuser), nullptr);
		EXPECT_EQ(world.getComponent<Marker>(reuser)->value, 2);
		EXPECT_EQ(world.getRegistry().getPool<Marker>()->size(), 1U);
	}

	TEST_F(StaleEntityIdTest, ViewYieldsOnlyTheNewId) {
		std::vector<EntityID> seen;
		for (auto [entity, marker] : world.view<Marker>()) {
			seen.push_back(entity);
		}
		EXPECT_EQ(seen, std::vector<EntityID>{reuser});
	}

} // namespace
