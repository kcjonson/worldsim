#include "scenes/game/dev/GroundRefusal.h"

#include "scenes/game/world/CreekWorldFixture.test.h"
#include "scenes/game/world/placement/PlacementSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/PartsAsset.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/Transform.h>

#include <gtest/gtest.h>

#include <memory>
#include <vector>

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
		void TearDown() override { assets::AssetRegistry::Get().clearDefinitions(); }

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
	EXPECT_TRUE(world_sim::wallChainGroundRefusal(&navigation, {{10.0F, kCreekY - 6.0F}, {10.0F, kCreekY - 2.0F}}).empty()) << "on the bank";
	EXPECT_TRUE(world_sim::wallChainGroundRefusal(nullptr, {{10.0F, kCreekY - 3.0F}, {10.0F, kCreekY + 3.0F}}).empty()) << "headless takes anything";
}

// A foundation is not built across the creek either, and is on dry ground beside it.
TEST_F(GroundRefusalTest, FoundationAcrossTheCreekIsRefusedAndBesideItIsTaken) {
	const std::vector<Foundation::Vec2> across{{8.0F, kCreekY - 2.0F}, {12.0F, kCreekY - 2.0F}, {12.0F, kCreekY + 2.0F}, {8.0F, kCreekY + 2.0F}};
	const std::vector<Foundation::Vec2> beside{{8.0F, kCreekY - 6.0F}, {12.0F, kCreekY - 6.0F}, {12.0F, kCreekY - 2.0F}, {8.0F, kCreekY - 2.0F}};
	EXPECT_FALSE(world_sim::foundationGroundRefusal(&navigation, across).empty());
	EXPECT_TRUE(world_sim::foundationGroundRefusal(&navigation, beside).empty());
	EXPECT_TRUE(world_sim::foundationGroundRefusal(nullptr, across).empty()) << "headless takes anything";
}

// The spawn verb sets furniture up where the placement tool would, never in the creek.
TEST_F(GroundRefusalTest, SpawnRefusesFurnitureInTheCreekAndTakesItOnTheBank) {
	ASSERT_TRUE(world_sim::PlacementSystem::isFurniture(kStorageBox));
	EXPECT_FALSE(world_sim::spawnGroundRefusal(placement, &navigation, kStorageBox, {10.0F, kCreekY}, 0).empty());
	EXPECT_TRUE(world_sim::spawnGroundRefusal(placement, &navigation, kStorageBox, {10.0F, kCreekY - 2.0F}, 0).empty());
}

// Furniture is not spawned inside a foundation still to be built, and is on bare ground beside it.
TEST_F(GroundRefusalTest, SpawnRefusesFurnitureInsideAnUnbuiltFoundation) {
	const std::vector<ecs::FootprintBounds> sites{ecs::FootprintBounds{geometry::Ring{{2000, 6000}, {10000, 6000}, {10000, 10000}, {2000, 10000}}}};
	world_sim::PlacementSystem				gated{world_sim::PlacementSystem::Args{
		   .world = &world,
		   .camera = nullptr,
		   .placementExecutor = nullptr,
		   .navigation = &navigation,
		   .worldSeed = 7,
		   .callbacks = {},
		   .insideUnbuiltSite = [&sites](glm::vec2 point) { return ecs::anyFootprintContains(sites, point); }
	}};
	const std::string refusal = world_sim::spawnGroundRefusal(gated, &navigation, kStorageBox, {5.0F, 8.0F}, 0);
	EXPECT_NE(refusal.find("not built yet"), std::string::npos) << refusal;
	EXPECT_TRUE(world_sim::spawnGroundRefusal(gated, &navigation, kStorageBox, {5.0F, 12.0F}, 0).empty());
	EXPECT_TRUE(world_sim::spawnGroundRefusal(gated, &navigation, kLooseStone, {5.0F, 8.0F}, 0).empty()) << "a loose item is put down, not installed";
}

// A parts item is spawned by the placement tool's rules: not into another item's body, and flush beside it.
TEST_F(GroundRefusalTest, SpawnRefusesAPartsItemIntoAnotherItem) {
	constexpr const char* kPartsBox = "Test_GroundRefusalPartsBox";
	assets::AssetDefinition partsBox;
	partsBox.defName = kPartsBox;
	partsBox.assetType = assets::AssetType::Parts;
	partsBox.parts = std::make_shared<const assets::PartsAsset>(assets::PartsAsset{.bodyTopMeters = 0.8F});
	partsBox.partsColliders.push_back(
		{.type = assets::CollisionShapeType::Polygon, .pointsMeters = {{-0.3F, -0.3F}, {0.3F, -0.3F}, {0.3F, 0.3F}, {-0.3F, 0.3F}}}
	);
	assets::AssetRegistry::Get().registerTestDefinition(partsBox);
	const Foundation::Vec2 at{10.0F, kCreekY - 4.0F};
	placement.spawnEntity(kPartsBox, at, 0);
	world.update(0.016F);

	EXPECT_FALSE(world_sim::spawnGroundRefusal(placement, &navigation, kPartsBox, {at.x + 0.3F, at.y}, 0).empty());

	// A crate's install target is in no mesh: the overlap rule refuses a spawn onto it.
	const ecs::EntityID crate = world.createEntity();
	world.addComponent<ecs::Position>(crate, ecs::Position{{at.x - 4.0F, at.y}});
	world.addComponent<ecs::Appearance>(crate, ecs::Appearance{kPartsBox});
	world.addComponent<ecs::Packaged>(crate, ecs::Packaged{.targetPosition = glm::vec2{at.x, at.y + 3.0F}});
	world.update(0.016F);
	const std::string refusal = world_sim::spawnGroundRefusal(placement, &navigation, kPartsBox, {at.x + 0.3F, at.y + 3.0F}, 0);
	EXPECT_NE(refusal.find("Overlaps an item"), std::string::npos) << refusal;
	const std::string flush = world_sim::spawnGroundRefusal(placement, &navigation, kPartsBox, {at.x + 0.6F, at.y}, 0);
	EXPECT_TRUE(flush.empty()) << "flush beside it: " << flush;
}

// Anything else is put down, not built: a loose item goes in the creek, as a dropped one does.
TEST_F(GroundRefusalTest, SpawnPutsANonFurnitureDefInTheCreek) {
	ASSERT_FALSE(world_sim::PlacementSystem::isFurniture(kLooseStone));
	EXPECT_TRUE(world_sim::spawnGroundRefusal(placement, &navigation, kLooseStone, {10.0F, kCreekY}, 0).empty());
	EXPECT_FALSE(world_sim::spawnGroundRefusal(placement, &navigation, kLooseStone, {500.0F, 500.0F}, 0).empty()) << "ground no mover has meshed";
}

// A mover goes anywhere he could stand, and onto ground no mover has meshed, which his own limit meshes.
TEST_F(GroundRefusalTest, AMoverIsTakenOnUnmeshedGroundWhereALooseItemIsNot) {
	EXPECT_TRUE(world_sim::moverGroundRefusal(&navigation, {10.0F, kCreekY}).empty()) << "he wades the creek";
	EXPECT_TRUE(world_sim::moverGroundRefusal(&navigation, {500.0F, 500.0F}).empty()) << "ground no mover has meshed";
	EXPECT_NE(world_sim::putDownRefusal(&navigation, {500.0F, 500.0F}).find("no mover has meshed"), std::string::npos);
	EXPECT_FALSE(world_sim::moverGroundRefusal(nullptr, {10.0F, kCreekY}).empty()) << "no nav mesh to stand on";
}
