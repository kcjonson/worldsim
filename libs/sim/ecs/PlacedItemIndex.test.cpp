#include "PlacedItemIndex.h"

#include "ColliderEntityIndex.h"
#include "World.h"
#include "components/Appearance.h"
#include "components/Packaged.h"
#include "components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/PartsAsset.h>

#include <gtest/gtest.h>

#include <memory>
#include <numbers>

using namespace ecs;
using assets::AssetDefinition;
using assets::AssetRegistry;

namespace {

	constexpr const char* kItem = "Test_PlacedItem";
	constexpr const char* kSimple = "Test_PlacedItemSimple";

	// A parts item 1.2 m by 0.6 m (`halfWidth` 0.6) with 0.5 m of clearance in front (+y).
	void registerItem(float halfWidth) {
		AssetDefinition item;
		item.defName = kItem;
		item.assetType = assets::AssetType::Parts;
		assets::PartsAsset parts{.bodyTopMeters = 0.8F};
		parts.clearance.polygons.push_back({.outer = {{-halfWidth, 0.3F}, {halfWidth, 0.3F}, {halfWidth, 0.8F}, {-halfWidth, 0.8F}}});
		item.parts = std::make_shared<const assets::PartsAsset>(std::move(parts));
		item.partsColliders.push_back(
			{.type = assets::CollisionShapeType::Polygon, .pointsMeters = {{-halfWidth, -0.3F}, {halfWidth, -0.3F}, {halfWidth, 0.3F}, {-halfWidth, 0.3F}}}
		);
		AssetRegistry::Get().registerTestDefinition(item);
	}

	// The item, and a def with a collider that isn't parts.
	class PlacedItemIndexTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			registerItem(0.6F);
			AssetDefinition simple;
			simple.defName = kSimple;
			simple.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {0.5F, 0.5F}});
			AssetRegistry::Get().registerTestDefinition(simple);
		}
		void TearDown() override { AssetRegistry::Get().clearDefinitions(); }

		static EntityID place(World& world, const char* defName, glm::vec2 at, float turnRadians = 0.0F) {
			const EntityID entity = world.createEntity();
			world.addComponent<Position>(entity, Position{at});
			world.addComponent<Rotation>(entity, Rotation{turnRadians});
			world.addComponent<Appearance>(entity, Appearance{defName});
			return entity;
		}

		// What a tick does: nav refreshes the obstacles, then the placed item system the index.
		void refresh() {
			static_cast<void>(obstacles.refresh(world, AssetRegistry::Get()));
			index.refresh(obstacles, world, AssetRegistry::Get());
		}

		World				world;
		ColliderEntityIndex obstacles;
		PlacedItemIndex		index;
	};

} // namespace

// An installed parts item is held with its collider and its reserved region (collider and clearance) in the world;
// a def that isn't parts is not. Its box answers a query; a box beside it doesn't.
TEST_F(PlacedItemIndexTest, HoldsInstalledPartsItemsWithTheirOutlines) {
	const EntityID item = place(world, kItem, {10.0F, 20.0F});
	place(world, kSimple, {14.0F, 20.0F});
	refresh();
	ASSERT_EQ(index.size(), 1U);
	const PlacedItemIndex::Entry* entry = index.find(item);
	ASSERT_NE(entry, nullptr);
	EXPECT_FALSE(entry->pending);
	ASSERT_EQ(entry->world.collider.size(), 1U);
	ASSERT_EQ(entry->world.reserved.size(), 1U);
	EXPECT_NEAR(entry->lower.x, 9.4F, 1e-4F);
	EXPECT_NEAR(entry->lower.y, 19.7F, 1e-4F);
	EXPECT_NEAR(entry->upper.y, 20.8F, 1e-4F) << "the clearance reaches 0.5 m past the front";

	std::vector<const PlacedItemIndex::Entry*> found;
	index.query({10.0F, 20.7F}, {10.1F, 20.75F}, found);
	ASSERT_EQ(found.size(), 1U) << "a box in the clearance alone";
	found.clear();
	index.query({11.0F, 20.0F}, {12.0F, 21.0F}, found);
	EXPECT_TRUE(found.empty());
}

// The revision moves on an install, a move, a turn, and a removal, and on nothing else; the entry follows each.
TEST_F(PlacedItemIndexTest, TheRevisionMovesOnAddMoveTurnAndRemove) {
	refresh();
	std::uint64_t revision = index.revision();
	const EntityID item = place(world, kItem, {10.0F, 20.0F});
	refresh();
	EXPECT_GT(index.revision(), revision) << "installed";
	revision = index.revision();
	refresh();
	EXPECT_EQ(index.revision(), revision) << "nothing changed";

	world.getComponent<Position>(item)->value = {12.0F, 20.0F};
	refresh();
	EXPECT_GT(index.revision(), revision) << "moved";
	revision = index.revision();
	EXPECT_NEAR(index.find(item)->lower.x, 11.4F, 1e-4F);

	world.getComponent<Rotation>(item)->radians = std::numbers::pi_v<float> * 0.5F;
	refresh();
	EXPECT_GT(index.revision(), revision) << "turned";
	revision = index.revision();
	EXPECT_NEAR(index.find(item)->lower.x, 11.2F, 1e-4F) << "a quarter turn puts the clearance on the -x side";
	EXPECT_NEAR(index.find(item)->upper.y, 20.6F, 1e-4F);

	world.destroyEntity(item);
	refresh();
	EXPECT_GT(index.revision(), revision) << "removed";
	EXPECT_EQ(index.size(), 0U);
}

// A crate waiting for the player stands nowhere; given a target it is held there, pending, at the target's turn;
// installed (Packaged gone) it is held where it stands.
TEST_F(PlacedItemIndexTest, APendingTargetIsHeldAtItsTarget) {
	const EntityID crate = place(world, kItem, {2.0F, 2.0F});
	world.addComponent<Packaged>(crate, Packaged{});
	refresh();
	EXPECT_EQ(index.size(), 0U);

	world.getComponent<Packaged>(crate)->targetPosition = glm::vec2{30.0F, 40.0F};
	world.getComponent<Packaged>(crate)->targetRotation = std::numbers::pi_v<float>;
	const std::uint64_t revision = index.revision();
	refresh();
	EXPECT_GT(index.revision(), revision);
	const PlacedItemIndex::Entry* entry = index.find(crate);
	ASSERT_NE(entry, nullptr);
	EXPECT_TRUE(entry->pending);
	EXPECT_EQ(entry->position, glm::vec2(30.0F, 40.0F));
	EXPECT_NEAR(entry->lower.y, 39.2F, 1e-4F) << "turned half round, the clearance is to the north";

	world.removeComponent<Packaged>(crate);
	world.getComponent<Position>(crate)->value = {30.0F, 40.0F};
	world.getComponent<Rotation>(crate)->radians = std::numbers::pi_v<float>;
	refresh();
	entry = index.find(crate);
	ASSERT_NE(entry, nullptr);
	EXPECT_FALSE(entry->pending);
	EXPECT_EQ(index.size(), 1U) << "installed, its pending target gone";
}

// A target held for a crate not yet spawned answers queries until the next refresh, which by then sees the crate.
TEST_F(PlacedItemIndexTest, AHeldTargetLastsUntilTheNextRefresh) {
	refresh();
	const std::uint64_t revision = index.revision();
	index.holdTarget(kItem, {5.0F, 5.0F}, 0.0F, AssetRegistry::Get());
	EXPECT_GT(index.revision(), revision);
	std::vector<const PlacedItemIndex::Entry*> found;
	index.query({4.9F, 4.9F}, {5.1F, 5.1F}, found);
	ASSERT_EQ(found.size(), 1U);
	EXPECT_TRUE(found.front()->pending);
	refresh();
	found.clear();
	index.query({4.9F, 4.9F}, {5.1F, 5.1F}, found);
	EXPECT_TRUE(found.empty());
}

// A reload is seen by the refresh even when placement read the outlines after it first (a pointer move comes before
// the tick): the installed item is re-read at the def's new size without moving.
TEST_F(PlacedItemIndexTest, AReloadIsSeenWhateverReadTheOutlinesFirst) {
	const EntityID item = place(world, kItem, {10.0F, 20.0F});
	refresh();
	ASSERT_NEAR(index.find(item)->lower.x, 9.4F, 1e-4F);

	AssetRegistry::Get().clearDefinitions();
	registerItem(1.0F);
	EXPECT_NEAR(index.outlinesOf(kItem, AssetRegistry::Get()).collider.front().front().x, -1.0F, 1e-4F) << "the pointer's read";
	refresh();
	ASSERT_NE(index.find(item), nullptr);
	EXPECT_NEAR(index.find(item)->lower.x, 9.0F, 1e-4F) << "re-read at 2 m wide";
}
