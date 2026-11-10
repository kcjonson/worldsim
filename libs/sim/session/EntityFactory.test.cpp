#include "session/EntityFactory.h"

#include <ecs/World.h>
#include <ecs/components/Action.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/AnimationState.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/DecisionTrace.h>
#include <ecs/components/FacingDirection.h>
#include <ecs/components/Inventory.h>
#include <ecs/components/Knowledge.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Movement.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/ResourceStack.h>
#include <ecs/components/StorageConfiguration.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/components/WorkQueue.h>

#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>

#include <gtest/gtest.h>

namespace ecs::test {

	class EntityFactoryTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			auto& registry = assets::AssetRegistry::Get();

			assets::AssetDefinition wood;
			wood.defName = "Wood";
			wood.itemProperties = assets::ItemProperties{};
			wood.itemProperties->stackSize = 10;
			registry.registerTestDefinition(std::move(wood));

			assets::AssetDefinition box;
			box.defName = "BasicBox";
			box.capabilities.storage = assets::StorageCapability{};
			box.capabilities.storage->maxCapacity = 6;
			registry.registerTestDefinition(std::move(box));

			assets::AssetDefinition spot;
			spot.defName = "CraftingSpot";
			spot.capabilities.craftable = assets::CraftableCapability{};
			registry.registerTestDefinition(std::move(spot));
		}

		void TearDown() override { assets::AssetRegistry::Get().clearDefinitions(); }

		World					  world;
		assets::PlacementExecutor placement{assets::AssetRegistry::Get()};
		EntityFactory			  factory{world, placement};
	};

	// Every component the AI, movement, needs, and rendering read from a colonist.
	TEST_F(EntityFactoryTest, ColonistHasTheFullComponentSet) {
		const EntityID colonist = factory.createColonist({3.0F, 4.0F}, "Ada", EntityFactory::starterSkills());

		EXPECT_EQ(world.getComponent<Position>(colonist)->value, glm::vec2(3.0F, 4.0F));
		EXPECT_NE(world.getComponent<Rotation>(colonist), nullptr);
		EXPECT_NE(world.getComponent<Velocity>(colonist), nullptr);
		EXPECT_NE(world.getComponent<MovementTarget>(colonist), nullptr);
		EXPECT_NE(world.getComponent<FacingDirection>(colonist), nullptr);
		EXPECT_NE(world.getComponent<AnimationState>(colonist), nullptr);
		EXPECT_EQ(world.getComponent<Appearance>(colonist)->defName, "Colonist");
		EXPECT_EQ(world.getComponent<Colonist>(colonist)->name, "Ada");
		EXPECT_NE(world.getComponent<NeedsComponent>(colonist), nullptr);
		EXPECT_NE(world.getComponent<Knowledge>(colonist), nullptr);
		EXPECT_EQ(world.getComponent<Memory>(colonist)->owner, colonist);
		EXPECT_NE(world.getComponent<Task>(colonist), nullptr);
		EXPECT_NE(world.getComponent<DecisionTrace>(colonist), nullptr);
		EXPECT_NE(world.getComponent<Action>(colonist), nullptr);
		EXPECT_NE(world.getComponent<AgentRadius>(colonist), nullptr);
		EXPECT_EQ(world.getComponent<Skills>(colonist)->getLevel("Farming"), 3.0F);

		const auto* attributes = world.getComponent<Attributes>(colonist);
		ASSERT_NE(attributes, nullptr);
		EXPECT_GE(attributes->strength, 6.0F);
		EXPECT_LT(attributes->strength, 14.0F);
		EXPECT_EQ(world.getComponent<Inventory>(colonist)->carryCapacityKg, Attributes::carryCapacityKg(attributes->strength));
	}

	TEST_F(EntityFactoryTest, WorldEntityGetsWhatItsDefNeeds) {
		const EntityID box = factory.createWorldEntity("BasicBox", {1.0F, 1.0F});
		EXPECT_EQ(world.getComponent<Inventory>(box)->maxCapacity, 6U);
		EXPECT_NE(world.getComponent<StorageConfiguration>(box), nullptr);
		EXPECT_EQ(world.getComponent<WorkQueue>(box), nullptr);

		const EntityID spot = factory.createWorldEntity("CraftingSpot", {2.0F, 1.0F});
		EXPECT_NE(world.getComponent<WorkQueue>(spot), nullptr);
		EXPECT_NE(world.getComponent<Inventory>(spot), nullptr);
		EXPECT_EQ(world.getComponent<Appearance>(spot)->defName, "CraftingSpot");
	}

	TEST_F(EntityFactoryTest, DroppedPilesSplitAtTheStackSize) {
		factory.dropResourcePiles("Wood", {0.0F, 0.0F}, 25);
		uint32_t total = 0;
		int		 piles = 0;
		for (auto [entity, stack, appearance] : world.view<ResourceStack, Appearance>()) {
			EXPECT_EQ(appearance.defName, "Wood");
			EXPECT_LE(stack.quantity, 10U);
			total += stack.quantity;
			++piles;
		}
		EXPECT_EQ(total, 25U);
		EXPECT_EQ(piles, 3);
	}

	TEST_F(EntityFactoryTest, PackagedItemWaitsAtItsTargetWhenItHasOne) {
		const EntityID placed = factory.createPackaged("BasicBox", {0.0F, 0.0F}, glm::vec2{5.0F, 5.0F});
		EXPECT_EQ(world.getComponent<Position>(placed)->value, glm::vec2(5.0F, 5.0F));
		EXPECT_EQ(world.getComponent<Packaged>(placed)->targetPosition, glm::vec2(5.0F, 5.0F));

		const EntityID loose = factory.createPackaged("BasicBox", {1.0F, 2.0F}, std::nullopt);
		EXPECT_EQ(world.getComponent<Position>(loose)->value, glm::vec2(1.0F, 2.0F));
		EXPECT_FALSE(world.getComponent<Packaged>(loose)->targetPosition.has_value());
	}

	// Integration replaces a chunk's whole index when its placement lands, so an earlier insert
	// would vanish; it is refused instead.
	TEST_F(EntityFactoryTest, PlaceStaticIsRefusedBeforeTheChunksPlacementLands) {
		EXPECT_FALSE(factory.placeStatic("Wood", {3.0F, 3.0F}, 0.0F));
		EXPECT_EQ(placement.getChunkIndex({0, 0}), nullptr);
		EXPECT_EQ(placement.editEpoch(), 0U);
	}

	// A placed bush is filed in the index with a resource pool and depletes and is removed the way
	// a generated one does.
	TEST_F(EntityFactoryTest, PlacedStaticJoinsTheIndexWithItsResourcePool) {
		assets::AssetDefinition bush;
		bush.defName = "Test_StickBush";
		bush.placement.biomes.push_back(assets::BiomePlacement{.biomeName = "TemperateGrassland"});
		bush.capabilities.harvestable = assets::HarvestableCapability{};
		bush.capabilities.harvestable->yieldDefName = "Wood";
		bush.capabilities.harvestable->destructive = false;
		bush.capabilities.harvestable->totalResourceMin = 3;
		bush.capabilities.harvestable->totalResourceMax = 5;
		assets::AssetRegistry::Get().registerTestDefinition(std::move(bush));
		ASSERT_TRUE(assets::PlacementExecutor::isStaticDef(*assets::AssetRegistry::Get().getDefinition("Test_StickBush")));
		EXPECT_FALSE(assets::PlacementExecutor::isStaticDef(*assets::AssetRegistry::Get().getDefinition("CraftingSpot")));

		const world::ChunkCoordinate coord{0, 0};
		assets::AsyncChunkPlacementResult nothingPlaced;
		nothingPlaced.coord = coord;
		placement.storeChunkResult(std::move(nothingPlaced));

		const glm::vec2 rolled{4.0F, 4.0F};
		const glm::vec2 given{8.0F, 4.0F};
		ASSERT_TRUE(factory.placeStatic("Test_StickBush", rolled, 0.0F));
		ASSERT_TRUE(factory.placeStatic("Test_StickBush", given, 0.0F, 7U));
		EXPECT_EQ(placement.editEpoch(), 2U);
		EXPECT_EQ(placement.getChunkIndex(coord)->queryRadius(rolled, 0.1F).size(), 1U);
		int ecsEntities = 0;
		for ([[maybe_unused]] auto [entity, appearance] : world.view<Appearance>()) {
			++ecsEntities;
		}
		EXPECT_EQ(ecsEntities, 0) << "a static is not an ECS entity";

		const auto pool = placement.getResourceCount(coord, rolled, "Test_StickBush");
		ASSERT_TRUE(pool.has_value());
		EXPECT_GE(*pool, 3U);
		EXPECT_LE(*pool, 5U);
		EXPECT_EQ(placement.getResourceCount(coord, given, "Test_StickBush"), 7U);

		EXPECT_EQ(placement.decrementResourceCount(coord, given, "Test_StickBush", 10), 7U);
		EXPECT_FALSE(placement.getResourceCount(coord, given, "Test_StickBush").has_value()) << "depleted";
		EXPECT_TRUE(placement.removeEntity(coord, given, "Test_StickBush"));
		EXPECT_TRUE(placement.getChunkIndex(coord)->queryRadius(given, 0.1F).empty());
		EXPECT_EQ(placement.editEpoch(), 3U);
	}

} // namespace ecs::test
