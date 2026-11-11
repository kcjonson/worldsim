// A regrowing source through the pieces the game wires: the placement executor that holds its regrowth,
// takeWorldSource that cuts it, worldSourceRegrowingUntil that answers whether it is ready, and the
// TimeSystem clock both read.

#include "WorldSource.h"

#include "LoosePileIndex.h"
#include "World.h"
#include "components/ResourceStack.h"
#include "systems/TimeSystem.h"

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>
#include <world/chunk/ChunkCoordinate.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

namespace ecs::test {

	namespace {
		constexpr const char* kBush = "Flora_WorldSourceTestBush";
		constexpr float		  kRegrowthSeconds = 60.0F;

		class WorldSourceTest : public ::testing::Test {
		  protected:
			void SetUp() override {
				auto& registry = assets::AssetRegistry::Get();
				registry.clearDefinitions();
				assets::AssetDefinition bush;
				bush.defName = kBush;
				bush.label = "World Source Test Bush";
				assets::HarvestableCapability cap;
				cap.yieldDefName = "Berry";
				cap.amountMin = 3;
				cap.amountMax = 3;
				cap.destructive = false;
				cap.regrowthTime = kRegrowthSeconds;
				bush.capabilities.harvestable = cap;
				registry.registerTestDefinition(std::move(bush));

				assets::AsyncChunkPlacementResult chunk;
				chunk.coord = world::worldToChunk({bushAt.x, bushAt.y});
				chunk.spatialIndex.insert({kBush, bushAt});
				executor.storeChunkResult(std::move(chunk));

				time = &world.registerSystem<TimeSystem>();
			}

			void TearDown() override { assets::AssetRegistry::Get().clearDefinitions(); }

			ActionSystem::SourceState cut() {
				return takeWorldSource(executor, world, pendingRemoval, kBush, bushAt, ActionSystem::SourceTake::Regrow, kRegrowthSeconds);
			}

			bool ready() {
				return !worldSourceRegrowingUntil(executor, assets::AssetRegistry::Get().getDefNameId(kBush), bushAt, time->simulationSeconds()).has_value();
			}

			void run(int frames) {
				for (int i = 0; i < frames; ++i) {
					world.update(0.1F);
				}
			}

			const glm::vec2			  bushAt{10.0F, 10.0F};
			assets::PlacementExecutor executor{assets::AssetRegistry::Get()};
			World					  world;
			TimeSystem*				  time = nullptr;
			std::vector<EntityID>	  pendingRemoval;
		};
	} // namespace

	// A cut bush reads regrowing and pays nothing until its regrowth time has passed on the game clock,
	// whatever the speed, and not at all while paused. Then it reads ready and pays again.
	TEST_F(WorldSourceTest, CutBushRegrowsOnTheGameClock) {
		ASSERT_TRUE(ready());
		EXPECT_EQ(cut(), ActionSystem::SourceState::Ready) << "the first cut pays";
		EXPECT_FALSE(ready());
		EXPECT_EQ(cut(), ActionSystem::SourceState::OnCooldown) << "a second cut pays nothing";

		run(300); // 30 s at 1x
		time->setSpeed(GameSpeed::VeryFast);
		run(29); // 29 s more at 10x
		EXPECT_FALSE(ready()) << "59 game seconds in";

		time->pause();
		run(1000);
		EXPECT_FALSE(ready()) << "no regrowth while paused";
		time->resume();

		run(2);
		EXPECT_TRUE(ready()) << "61 game seconds in";
		EXPECT_EQ(cut(), ActionSystem::SourceState::Ready) << "it pays again";
		EXPECT_FALSE(ready()) << "and that cut starts a fresh regrowth";
	}

	// Unloading and reloading the bush's chunk changes neither state.
	TEST_F(WorldSourceTest, ChunkReloadKeepsRegrowthState) {
		const auto reload = [&] {
			const auto coord = world::worldToChunk({bushAt.x, bushAt.y});
			executor.unloadChunk(coord);
			assets::AsyncChunkPlacementResult chunk;
			chunk.coord = coord;
			chunk.spatialIndex.insert({kBush, bushAt});
			executor.storeChunkResult(std::move(chunk));
		};

		reload();
		EXPECT_TRUE(ready()) << "a ready bush is still ready";
		ASSERT_EQ(cut(), ActionSystem::SourceState::Ready);
		reload();
		EXPECT_FALSE(ready()) << "a regrowing bush is still regrowing";
		EXPECT_EQ(cut(), ActionSystem::SourceState::OnCooldown);
	}

#ifdef DEVELOPMENT_BUILD
	// A world with no TimeSystem has no clock to stamp a regrowth on, so cutting a bush there aborts
	// instead of starting a regrowth that would never end. Taking a source with nothing regrowing reads
	// no clock, so worlds that never cut a regrowing source need no TimeSystem.
	// Two live piles filed under one cell: a lookup names the lowest entity id, whatever order the
	// standard library's buckets hold them in.
	TEST(LoosePileIndexTest, TwoPilesInOneCellResolveToTheLowestEntity) {
		World				   world;
		LoosePileIndex		   index;
		const glm::vec2		   at{4.0F, 2.0F};
		const EntityID		   first = world.createEntity();
		const EntityID		   second = world.createEntity();
		for (EntityID pile : {first, second}) {
			world.addComponent<ResourceStack>(pile, ResourceStack{.quantity = 3});
		}
		index.add(second, 7, at);
		index.add(first, 7, at);
		EXPECT_EQ(index.find(world, 7, at), std::min(first, second));
		world.getComponent<ResourceStack>(std::min(first, second))->quantity = 0;
		EXPECT_EQ(index.find(world, 7, at), std::max(first, second)) << "a drained pile doesn't hide the live one";
	}

	TEST(WorldSourceNoClockTest, CuttingWithoutAClockAborts) {
		GTEST_FLAG_SET(death_test_style, "threadsafe");
		auto& registry = assets::AssetRegistry::Get();
		registry.clearDefinitions();
		assets::AssetDefinition bush;
		bush.defName = kBush;
		bush.capabilities.harvestable = assets::HarvestableCapability{};
		bush.capabilities.harvestable->yieldDefName = "Berry";
		registry.registerTestDefinition(std::move(bush));
		const glm::vec2			  at{10.0F, 10.0F};
		assets::PlacementExecutor executor{registry};
		assets::AsyncChunkPlacementResult chunk;
		chunk.coord = world::worldToChunk({at.x, at.y});
		chunk.spatialIndex.insert({kBush, at});
		executor.storeChunkResult(std::move(chunk));
		World				  world;
		std::vector<EntityID> pendingRemoval;

		EXPECT_EQ(takeWorldSource(executor, world, pendingRemoval, kBush, at, ActionSystem::SourceTake::Keep, 0.0F), ActionSystem::SourceState::Ready)
			<< "nothing regrows yet, so no clock is read";
		EXPECT_DEATH(
			(void)takeWorldSource(executor, world, pendingRemoval, kBush, at, ActionSystem::SourceTake::Regrow, kRegrowthSeconds),
			""
		);
		registry.clearDefinitions();
	}
#endif

} // namespace ecs::test
