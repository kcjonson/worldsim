#include "scenes/game/world/placement/PlacementSystem.h"

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
