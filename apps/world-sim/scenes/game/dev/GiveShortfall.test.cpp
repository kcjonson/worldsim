#include "scenes/game/dev/GiveShortfall.h"

#include <assets/AssetDefinition.h>
#include <ecs/components/Inventory.h>

#include <gtest/gtest.h>

// A tool in one hand is named as what keeps a two-hand material out.
TEST(GiveShortfallTest, HandsNameTheOtherItemTheyHold) {
	ecs::Inventory inventory;
	ASSERT_TRUE(inventory.pickUp("AxePrimitive"));
	EXPECT_EQ(world_sim::giveShortfallReason(inventory, "Wood", "hands"), "hands occupied by AxePrimitive");
	ASSERT_TRUE(inventory.pickUp("SmallStone"));
	EXPECT_EQ(world_sim::giveShortfallReason(inventory, "Wood", "hands"), "hands occupied by SmallStone and AxePrimitive");
}

// A two-hand item is in both hands as one stack, and is named once.
TEST(GiveShortfallTest, ATwoHandItemInTheWayIsNamedOnce) {
	ecs::Inventory inventory;
	ASSERT_TRUE(inventory.pickUp("Wood", 2));
	EXPECT_EQ(world_sim::giveShortfallReason(inventory, "SmallStone", "hands"), "hands occupied by Wood");
}

// Hands that hold only the material given are full of it.
TEST(GiveShortfallTest, HandsFullOfTheMaterialSaySo) {
	ecs::Inventory inventory;
	ASSERT_TRUE(inventory.pickUp("SmallStone"));
	ASSERT_TRUE(inventory.pickUp("SmallStone"));
	EXPECT_EQ(world_sim::giveShortfallReason(inventory, "SmallStone", "hands"), "the hands hold all the SmallStone they can");
}

// With nothing in the hands, an armful that lifted nothing was stopped by weight.
TEST(GiveShortfallTest, EmptyHandsThatTookNoArmfulBlameCarryWeight) {
	const ecs::Inventory inventory;
	EXPECT_EQ(world_sim::giveShortfallReason(inventory, "Wood", "hands"), "no Wood fits under his carry weight");
}

// An unknown name and a def that is not an item are given nowhere; an item is.
TEST(GiveShortfallTest, OnlyAnItemIsGiven) {
	EXPECT_EQ(world_sim::giveMaterialRefusal(nullptr, "Wod"), "unknown asset 'Wod'");
	assets::AssetDefinition definition;
	EXPECT_EQ(world_sim::giveMaterialRefusal(&definition, "BasicBox"), "'BasicBox' is not an item");
	definition.itemProperties = assets::ItemProperties{};
	EXPECT_TRUE(world_sim::giveMaterialRefusal(&definition, "Wood").empty());
}

TEST(GiveShortfallTest, BeltAndPackGiveTheirOwnReason) {
	const ecs::Inventory inventory;
	EXPECT_EQ(world_sim::giveShortfallReason(inventory, "AxePrimitive", "belt"), "the belt has no free slot");
	EXPECT_EQ(world_sim::giveShortfallReason(inventory, "Wood", "pack"), "no room for more Wood");
}
