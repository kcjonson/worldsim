#include "scenes/game/world/placement/PlacementSystem.h"

#include "scenes/game/world/CreekWorldFixture.test.h"
#include "scenes/game/world/PlayerVisibilityFixture.test.h"

#include <assets/AssetRegistry.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/NavigationSystem.h>
#include <ecs/systems/PlacedNothing.test.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <gtest/gtest.h>

#include <memory>

namespace {

	using world_sim::testing::kSeenPoint;
	using world_sim::testing::kUnseenPoint;
	using world_sim::testing::kViewportHeight;
	using world_sim::testing::kViewportWidth;

	constexpr const char* kCrate = "Test_PlacementCrate";
	constexpr float		  kCrateHalfExtent = 3.0F;

	// All land, the chunks round the origin generated once and shared by every test here.
	world::ChunkManager* landChunks() {
		static const std::unique_ptr<world::ChunkManager> chunks = [] {
			auto manager = std::make_unique<world::ChunkManager>(std::make_unique<world::SyntheticWorldSampler>(7, world::SyntheticMap{}));
			manager->setLoadRadius(2);
			manager->setUnloadRadius(4);
			manager->update({0.0F, 0.0F});
			manager->finishPendingGeneration();
			return manager;
		}();
		return chunks.get();
	}

	class PlacementVisibilityTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetDefinition crate;
			crate.defName = kCrate;
			crate.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {kCrateHalfExtent, kCrateHalfExtent}});
			assets::AssetRegistry::Get().registerTestDefinition(crate);

			navigation = &seen.world.registerSystem<ecs::NavigationSystem>();
			navigation->setChunkManager(landChunks());
			navigation->setPlacementData(&emptyPlacement.executor, &emptyPlacement.processed);
			const ecs::EntityID mesher = seen.world.createEntity();
			seen.world.addComponent<ecs::Position>(mesher, ecs::Position{{0.0F, 0.0F}});
			seen.world.addComponent<ecs::AgentRadius>(mesher, ecs::AgentRadius{});
			ecs::Memory memory;
			memory.sightRadius = 100.0F;
			seen.world.addComponent<ecs::Memory>(mesher, std::move(memory));
			seen.world.update(0.016F);
			navigation->finishPendingBuilds();

			placement = std::make_unique<world_sim::PlacementSystem>(world_sim::PlacementSystem::Args{
				.world = &seen.world, .camera = &seen.camera, .placementExecutor = nullptr, .navigation = navigation, .worldSeed = 7, .callbacks = {}
			});
			placement->selectBuildItem(kCrate);
		}

		void hover(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			placement->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight);
		}

		world_sim::testing::SeenWorld seen;
		ecs::EmptyPlacement			  emptyPlacement;
		ecs::NavigationSystem*		  navigation = nullptr;
		std::unique_ptr<world_sim::PlacementSystem> placement;
	};

} // namespace

TEST_F(PlacementVisibilityTest, GhostIsValidOnSeenGroundAndInvalidOnUnseenGround) {
	ASSERT_TRUE(navigation->isValidPosition(kSeenPoint));
	ASSERT_TRUE(navigation->isValidPosition(kUnseenPoint)) << "the mesh reaches past what is seen";
	hover(kSeenPoint);
	EXPECT_TRUE(placement->ghostValid());
	hover(kUnseenPoint);
	EXPECT_FALSE(placement->ghostValid());
}

TEST_F(PlacementVisibilityTest, FootprintHalfOnUnseenGroundIsInvalid) {
	const glm::vec2 edge{world_sim::testing::kSeenRadiusMeters - 2.0F, 0.0F};
	ASSERT_TRUE(seen.visibility.isSeen(edge));
	hover(edge);
	EXPECT_FALSE(placement->ghostValid());
}

TEST_F(PlacementVisibilityTest, ClickRefusesOnUnseenGroundAndPlacesOnSeenGround) {
	hover(kUnseenPoint);
	EXPECT_FALSE(placement->handleClick());
	EXPECT_EQ(placement->state(), world_sim::PlacementState::Placing);
	hover(kSeenPoint);
	EXPECT_TRUE(placement->handleClick());
}

TEST_F(PlacementVisibilityTest, RevealTurnsTheRuleOff) {
	seen.visibility.setRevealed(true);
	hover(kUnseenPoint);
	EXPECT_TRUE(placement->ghostValid());
	EXPECT_TRUE(placement->handleClick());
}

namespace {

	using world_sim::testing::kCreekHalfWidthMeters;
	using world_sim::testing::kCreekY;

	constexpr const char* kBox = "Test_PlacementBox";
	constexpr float		  kBoxHalfExtent = 0.4F;

	// Nothing is set up in a stream a colonist could wade: the placement tool refuses a target whose
	// footprint touches the creek, the way it refuses water.
	class PlacementFordTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetDefinition box;
			box.defName = kBox;
			box.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {kBoxHalfExtent, kBoxHalfExtent}});
			assets::AssetRegistry::Get().registerTestDefinition(box);

			navigation = &world_sim::testing::meshCreekWorld(seen.world, emptyPlacement);
			placement = std::make_unique<world_sim::PlacementSystem>(world_sim::PlacementSystem::Args{
				.world = &seen.world, .camera = &seen.camera, .placementExecutor = nullptr, .navigation = navigation, .worldSeed = 7, .callbacks = {}
			});
			placement->selectBuildItem(kBox);
		}

		void hover(glm::vec2 meters) {
			const glm::vec2 screen = seen.screenOf(meters);
			placement->handleMouseMove(screen.x, screen.y, kViewportWidth, kViewportHeight);
		}

		world_sim::testing::SeenWorld seen;
		ecs::EmptyPlacement			  emptyPlacement;
		ecs::NavigationSystem*		  navigation = nullptr;
		std::unique_ptr<world_sim::PlacementSystem> placement;
	};

} // namespace

TEST_F(PlacementFordTest, TargetInTheCreekIsRefusedAndBesideItIsPlaced) {
	const glm::vec2 inCreek{10.0F, kCreekY};
	ASSERT_TRUE(navigation->isValidPosition(inCreek)) << "a colonist wades the creek";
	ASSERT_FALSE(navigation->isPointBuildable(inCreek)) << "the creek is a ford in the mesh";

	hover(inCreek);
	EXPECT_FALSE(placement->ghostValid());
	EXPECT_FALSE(placement->handleClick());
	EXPECT_EQ(placement->state(), world_sim::PlacementState::Placing);

	// The centre is on the bank, the footprint reaches into the creek.
	const glm::vec2 overhanging{10.0F, kCreekY + kCreekHalfWidthMeters + kBoxHalfExtent * 0.5F};
	ASSERT_TRUE(navigation->isPointClearToBuild(overhanging));
	hover(overhanging);
	EXPECT_FALSE(placement->ghostValid()) << "the footprint is tested, not only its centre";

	const glm::vec2 onTheBank{10.0F, kCreekY + 2.0F};
	hover(onTheBank);
	EXPECT_TRUE(placement->ghostValid());
	EXPECT_TRUE(placement->handleClick());
}

TEST_F(PlacementFordTest, FootprintClearToBuildRefusesTheCreek) {
	EXPECT_FALSE(placement->footprintClearToBuild(kBox, {10.0F, kCreekY}));
	EXPECT_FALSE(placement->footprintClearToBuild("Test_PlacementNoFootprint", {10.0F, kCreekY})) << "a def with no outline is its centre";
	EXPECT_TRUE(placement->footprintClearToBuild(kBox, {10.0F, kCreekY - 2.0F}));
}
