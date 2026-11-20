#include "ColliderEntityIndex.h"

#include "World.h"
#include "components/AgentRadius.h"
#include "components/Appearance.h"
#include "components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

using namespace ecs;
using assets::AssetDefinition;
using assets::AssetRegistry;
using assets::CollisionShapeType;

namespace {

	constexpr const char* kSolid = "Test_ColliderEntitySolid";
	constexpr const char* kOpen = "Test_ColliderEntityOpen";

	class ColliderEntityIndexTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			AssetDefinition solid;
			solid.defName = kSolid;
			solid.setCollision(0, {.type = CollisionShapeType::Rect, .halfExtentsMeters = {0.5F, 0.5F}});
			AssetRegistry::Get().registerTestDefinition(solid);
			AssetDefinition open;
			open.defName = kOpen;
			AssetRegistry::Get().registerTestDefinition(open);
		}
		void TearDown() override { AssetRegistry::Get().clearDefinitions(); }

		static EntityID place(World& world, const char* defName, glm::vec2 at) {
			const EntityID entity = world.createEntity();
			world.addComponent<Position>(entity, Position{at});
			world.addComponent<Rotation>(entity, Rotation{0.0F});
			world.addComponent<Appearance>(entity, Appearance{defName});
			return entity;
		}
	};

} // namespace

// Only an entity whose def has a collider is held: not one without, and not a mover whatever its def.
TEST_F(ColliderEntityIndexTest, HoldsEntitiesWithAColliderAndNoMover) {
	World		   world;
	const EntityID solid = place(world, kSolid, {3.0F, 4.0F});
	place(world, kOpen, {5.0F, 4.0F});
	const EntityID mover = place(world, kSolid, {7.0F, 4.0F});
	world.addComponent<AgentRadius>(mover, AgentRadius{});

	ColliderEntityIndex				   index;
	const ColliderEntityIndex::Changes changes = index.refresh(world, AssetRegistry::Get());
	ASSERT_EQ(changes.added.size(), 1U);
	EXPECT_TRUE(changes.removed.empty());
	EXPECT_EQ(changes.added.front().entity, solid);
	EXPECT_EQ(changes.added.front().placed.defName, kSolid);
	EXPECT_EQ(changes.added.front().placed.position, glm::vec2(3.0F, 4.0F));
	EXPECT_EQ(index.size(), 1U);
	EXPECT_EQ(index.find(mover), nullptr);

	std::vector<const ColliderEntityIndex::Entry*> found;
	index.queryRect({2.0F, 3.0F}, {4.0F, 5.0F}, found);
	ASSERT_EQ(found.size(), 1U);
	EXPECT_EQ(found.front()->entity, solid);
	found.clear();
	index.queryRect({4.0F, 3.0F}, {9.0F, 5.0F}, found);
	EXPECT_TRUE(found.empty());
}

// A refresh with nothing changed reports nothing; a move is the old entry out and the new one in; a
// destroyed entity is its entry out.
TEST_F(ColliderEntityIndexTest, DiffsMovesAndRemovals) {
	World				world;
	const EntityID		moved = place(world, kSolid, {1.0F, 1.0F});
	const EntityID		destroyed = place(world, kSolid, {9.0F, 9.0F});
	ColliderEntityIndex index;
	static_cast<void>(index.refresh(world, AssetRegistry::Get()));

	ColliderEntityIndex::Changes changes = index.refresh(world, AssetRegistry::Get());
	EXPECT_TRUE(changes.added.empty());
	EXPECT_TRUE(changes.removed.empty());

	world.getComponent<Position>(moved)->value = {2.0F, 1.0F};
	world.destroyEntity(destroyed);
	changes = index.refresh(world, AssetRegistry::Get());
	ASSERT_EQ(changes.removed.size(), 2U);
	EXPECT_EQ(changes.removed[0].entity, moved);
	EXPECT_EQ(changes.removed[0].placed.position, glm::vec2(1.0F, 1.0F));
	EXPECT_EQ(changes.removed[1].entity, destroyed);
	ASSERT_EQ(changes.added.size(), 1U);
	EXPECT_EQ(changes.added[0].entity, moved);
	EXPECT_EQ(changes.added[0].placed.position, glm::vec2(2.0F, 1.0F));
	EXPECT_EQ(index.size(), 1U);
}
