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
		EXPECT_DEBUG_DEATH(
			world.addComponent<Marker>(stale, Marker{3}), "addComponent on an entity that is not alive"
		);
		EXPECT_FALSE(world.hasComponent<Marker>(stale));
		ASSERT_NE(world.getComponent<Marker>(reuser), nullptr);
		EXPECT_EQ(world.getComponent<Marker>(reuser)->value, 2);
		EXPECT_EQ(world.getRegistry().getPool<Marker>()->size(), 1U);
	}

	TEST_F(StaleEntityIdTest, StaleRemoveDuringSwapAndPopTouchesNothing) {
		EntityID other = world.createEntity();
		world.addComponent<Marker>(other, Marker{3});
		// Dense order is now [reuser, other]; removing reuser's slot would swap other into it.
		world.removeComponent<Marker>(stale);
		EXPECT_EQ(world.getRegistry().getPool<Marker>()->size(), 2U);
		ASSERT_NE(world.getComponent<Marker>(reuser), nullptr);
		EXPECT_EQ(world.getComponent<Marker>(reuser)->value, 2);
		ASSERT_NE(world.getComponent<Marker>(other), nullptr);
		EXPECT_EQ(world.getComponent<Marker>(other)->value, 3);
	}

	TEST(RegistrySwapAndPopTest, StaleRemoveThenLiveRemoveLeavesTheReuser) {
		World	 world;
		EntityID first = world.createEntity();
		EntityID other = world.createEntity();
		world.addComponent<Marker>(first, Marker{1});
		world.addComponent<Marker>(other, Marker{2});
		world.destroyEntity(first);
		EntityID reuser = world.createEntity();
		ASSERT_EQ(getIndex(reuser), getIndex(first));
		world.addComponent<Marker>(reuser, Marker{3});

		world.removeComponent<Marker>(first);
		EXPECT_EQ(world.getRegistry().getPool<Marker>()->size(), 2U);

		world.removeComponent<Marker>(other);
		EXPECT_EQ(world.getRegistry().getPool<Marker>()->size(), 1U);
		EXPECT_FALSE(world.hasComponent<Marker>(first));
		EXPECT_EQ(world.getComponent<Marker>(first), nullptr);
		EXPECT_FALSE(world.hasComponent<Marker>(other));
		EXPECT_EQ(world.getComponent<Marker>(other), nullptr);
		ASSERT_TRUE(world.hasComponent<Marker>(reuser));
		EXPECT_EQ(world.getComponent<Marker>(reuser)->value, 3);
	}

	TEST(RegistrySwapAndPopTest, RemoveThenReAddOnTheSameLiveId) {
		World	 world;
		EntityID entity = world.createEntity();
		world.addComponent<Marker>(entity, Marker{1});
		world.removeComponent<Marker>(entity);
		EXPECT_FALSE(world.hasComponent<Marker>(entity));
		world.addComponent<Marker>(entity, Marker{5});
		ASSERT_TRUE(world.hasComponent<Marker>(entity));
		EXPECT_EQ(world.getComponent<Marker>(entity)->value, 5);
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
