#include "scenes/game/dev/GroundRefusal.h"

#include "scenes/game/world/CreekWorldFixture.test.h"
#include "scenes/game/world/placement/PlacementSystem.h"

#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

namespace {

	using world_sim::testing::kCreekY;

	constexpr const char* kStorageBox = "Test_GroundRefusalStorageBox";
	constexpr const char* kLooseStone = "Test_GroundRefusalLooseStone";

	class GroundRefusalTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetDefinition box;
			box.defName = kStorageBox;
			box.capabilities.storage = assets::StorageCapability{};
			assets::AssetRegistry::Get().registerTestDefinition(box);
			assets::AssetDefinition stone;
			stone.defName = kLooseStone;
			assets::AssetRegistry::Get().registerTestDefinition(stone);
		}

		ecs::World				world;
		ecs::EmptyPlacement		emptyPlacement;
		ecs::NavigationSystem&	navigation = world_sim::testing::meshCreekWorld(world, emptyPlacement);
		world_sim::PlacementSystem placement{world_sim::PlacementSystem::Args{
			.world = &world, .camera = nullptr, .placementExecutor = nullptr, .navigation = &navigation, .worldSeed = 7, .callbacks = {}
		}};
	};

} // namespace

// Nothing is built in a stream a colonist could wade: a freestanding wall across the creek is refused.
TEST_F(GroundRefusalTest, WallChainAcrossTheCreekIsRefused) {
	ASSERT_TRUE(navigation.isValidPosition({10.0F, kCreekY})) << "a colonist wades the creek";
	EXPECT_FALSE(world_sim::wallChainGroundRefusal(&navigation, {{10.0F, kCreekY - 3.0F}, {10.0F, kCreekY + 3.0F}}).empty());
	EXPECT_TRUE(world_sim::wallChainGroundRefusal(nullptr, {{10.0F, kCreekY - 3.0F}, {10.0F, kCreekY + 3.0F}}).empty()) << "headless takes anything";
}

// The spawn verb sets furniture up where the placement tool would, never in the creek.
TEST_F(GroundRefusalTest, SpawnRefusesFurnitureInTheCreekAndTakesItOnTheBank) {
	ASSERT_TRUE(world_sim::PlacementSystem::isFurniture(kStorageBox));
	EXPECT_FALSE(world_sim::spawnGroundRefusal(placement, &navigation, kStorageBox, {10.0F, kCreekY}).empty());
	EXPECT_TRUE(world_sim::spawnGroundRefusal(placement, &navigation, kStorageBox, {10.0F, kCreekY - 2.0F}).empty());
}

// Anything else is put down, not built: a loose item goes in the creek, as a dropped one does.
TEST_F(GroundRefusalTest, SpawnPutsANonFurnitureDefInTheCreek) {
	ASSERT_FALSE(world_sim::PlacementSystem::isFurniture(kLooseStone));
	EXPECT_TRUE(world_sim::spawnGroundRefusal(placement, &navigation, kLooseStone, {10.0F, kCreekY}).empty());
	EXPECT_FALSE(world_sim::spawnGroundRefusal(placement, &navigation, kLooseStone, {500.0F, 500.0F}).empty()) << "ground no mover has seen";
}
