#include "StoreOrDrop.h"

#include "World.h"
#include "components/Inventory.h"
#include "components/StorageConfiguration.h"
#include "components/Transform.h"
#include "components/WorkQueue.h"

#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

using namespace ecs;

namespace {

	class StoreOrDropTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			assets::AssetDefinition wood;
			wood.defName = "Wood";
			wood.label = "Wood";
			wood.handsRequired = 2;
			wood.category = assets::ItemCategory::RawMaterial;
			wood.itemProperties = assets::ItemProperties{};
			wood.itemProperties->stackSize = 40;
			assets::AssetRegistry::Get().registerTestDefinition(std::move(wood));
		}

		void TearDown() override { assets::AssetRegistry::Get().clearDefinitions(); }

		EntityID createBox(glm::vec2 position) {
			const EntityID box = world.createEntity();
			world.addComponent<Position>(box, Position{position});
			Inventory store;
			store.maxCapacity = 4;
			world.addComponent<Inventory>(box, std::move(store));
			world.addComponent<StorageConfiguration>(box, StorageConfiguration::createAcceptAll({assets::ItemCategory::RawMaterial}));
			return box;
		}

		World world;
	};

} // namespace

// A configured max below the physical room is what the box takes; a category it doesn't accept is 0.
TEST_F(StoreOrDropTest, StorageRoomForClampsToConfiguredMax) {
	StorageConfiguration config;
	config.addRule(StorageRule{.defName = "Wood", .category = assets::ItemCategory::RawMaterial, .maxAmount = 10});
	Inventory store;
	store.maxCapacity = 2;
	store.addItem("Wood", 3);
	ASSERT_EQ(store.addableCount("Wood"), 77U) << "37 left in the stack + one free slot of 40";

	EXPECT_EQ(storageRoomFor(config, store, "Wood", assets::ItemCategory::RawMaterial), 7U);
	EXPECT_EQ(storageRoomFor(config, store, "Wood", assets::ItemCategory::Food), 0U);
}

// A crafting station in reach only takes its recipe's bill, so store-first passes it for the box.
TEST_F(StoreOrDropTest, SkipsCraftingStationInReach) {
	const EntityID station = createBox({0.3F, 0.0F});
	world.addComponent<WorkQueue>(station, WorkQueue{});
	const EntityID box = createBox({1.2F, 0.0F});

	int		 dropCalls = 0;
	uint32_t stored = storeOrDrop(world, "Wood", 5, {0.0F, 0.0F}, [&](const std::string&, float, float, uint32_t) { ++dropCalls; });

	EXPECT_EQ(stored, 5U);
	EXPECT_EQ(world.getComponent<Inventory>(station)->getQuantity("Wood"), 0U);
	EXPECT_EQ(world.getComponent<Inventory>(box)->getQuantity("Wood"), 5U);
	EXPECT_EQ(dropCalls, 0);
}
