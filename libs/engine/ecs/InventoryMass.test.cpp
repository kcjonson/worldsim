#include "InventoryMass.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

using namespace ecs;

// ============================================================================
// Pure mass arithmetic: the per-trip and fit calculations that drive the
// chop-haul-build loop's carry limits. Registry-backed wrappers are exercised
// end-to-end in the game; here we pin the math.
// ============================================================================

// massUnitsPerTrip: floor(capacity / mass), clamped to at least 1.
TEST(InventoryMassTests, PerTrip_WoodAt35Kg_IsFourteen) {
	// 35 kg / 2.5 kg per wood = 14.
	EXPECT_EQ(massUnitsPerTrip(35.0F, 2.5F), 14U);
}

TEST(InventoryMassTests, PerTrip_FloorsPartialUnit) {
	// 35 / 1.5 = 23.33 -> 23.
	EXPECT_EQ(massUnitsPerTrip(35.0F, 1.5F), 23U);
}

TEST(InventoryMassTests, PerTrip_HeavierThanCapStillMakesProgress) {
	// A single unit heavier than the whole cap must not stall demand at zero.
	EXPECT_EQ(massUnitsPerTrip(35.0F, 50.0F), 1U);
}

TEST(InventoryMassTests, PerTrip_MasslessOrZeroCapIsOne) {
	EXPECT_EQ(massUnitsPerTrip(35.0F, 0.0F), 1U);
	EXPECT_EQ(massUnitsPerTrip(0.0F, 2.5F), 1U);
}

// massUnitsThatFit: how many more units fit in the remaining headroom.
TEST(InventoryMassTests, Fit_EmptyColonistFitsAFullTrip) {
	// 35 kg free, 2.5 kg wood -> 14.
	EXPECT_EQ(massUnitsThatFit(35.0F, 2.5F, /*masslessCap=*/99U), 14U);
}

TEST(InventoryMassTests, Fit_PartiallyLoadedClampsToHeadroom) {
	// Carrying 30 kg already (5 kg free) -> only 2 more wood fit (5 / 2.5).
	EXPECT_EQ(massUnitsThatFit(5.0F, 2.5F, 99U), 2U);
}

TEST(InventoryMassTests, Fit_AtOrOverCapIsZero) {
	EXPECT_EQ(massUnitsThatFit(0.0F, 2.5F, 99U), 0U);
	EXPECT_EQ(massUnitsThatFit(-3.0F, 2.5F, 99U), 0U);
}

TEST(InventoryMassTests, Fit_MasslessItemIsCappedBySlotsNotWeight) {
	// A weightless item is never weight-limited; the slot cap stands in.
	EXPECT_EQ(massUnitsThatFit(5.0F, 0.0F, 99U), 99U);
}

// The interaction that produces multi-trip hauling: a 30-wood manifest at a
// 14-per-trip cap takes three trips (14 + 14 + 2).
TEST(InventoryMassTests, PerTrip_DrivesMultipleTripsForLargeManifest) {
	const uint32_t perTrip = massUnitsPerTrip(35.0F, 2.5F); // 14
	uint32_t	   remaining = 30;
	int			   trips = 0;
	while (remaining > 0) {
		const uint32_t thisTrip = std::min(perTrip, remaining);
		remaining -= thisTrip;
		++trips;
	}
	EXPECT_EQ(trips, 3);
}

// ============================================================================
// Hand-carried bulk materials: a two-hand armful occupies BOTH hands as one
// logical stack, so it must be counted once and the mirror kept in sync. These
// are registry-free (pure inventory ops); the weight-clamped addArmful is
// exercised end-to-end once Wood becomes two-hand.
// ============================================================================

TEST(InventoryMassTests, HandHeldQuantity_TwoHandMirrorCountsOnce) {
	Inventory inv;
	inv.leftHand = ItemStack{"Wood", 12};
	inv.rightHand = ItemStack{"Wood", 12}; // same stack mirrored across both hands
	EXPECT_EQ(handHeldQuantity(inv, "Wood"), 12U);
}

TEST(InventoryMassTests, HandHeldQuantity_DistinctOneHandItemsCountPerHand) {
	Inventory inv;
	inv.leftHand = ItemStack{"Knife", 1};
	inv.rightHand = ItemStack{"Torch", 1};
	EXPECT_EQ(handHeldQuantity(inv, "Knife"), 1U);
	EXPECT_EQ(handHeldQuantity(inv, "Torch"), 1U);
	EXPECT_EQ(handHeldQuantity(inv, "Wood"), 0U);
}

TEST(InventoryMassTests, AvailableQuantity_SumsBackpackAndHands) {
	Inventory inv;
	inv.addItem("Stone", 5);
	inv.leftHand = ItemStack{"Wood", 10};
	inv.rightHand = ItemStack{"Wood", 10};
	EXPECT_EQ(availableQuantity(inv, "Wood"), 10U); // hands, counted once
	EXPECT_EQ(availableQuantity(inv, "Stone"), 5U); // backpack
}

TEST(InventoryMassTests, RemoveFromHands_DecrementsMirrorInSync) {
	Inventory inv;
	inv.leftHand = ItemStack{"Wood", 10};
	inv.rightHand = ItemStack{"Wood", 10};
	EXPECT_EQ(removeFromHands(inv, "Wood", 4), 4U);
	ASSERT_TRUE(inv.leftHand.has_value());
	ASSERT_TRUE(inv.rightHand.has_value());
	EXPECT_EQ(inv.leftHand->quantity, 6U);
	EXPECT_EQ(inv.rightHand->quantity, 6U); // both hands stay equal
}

TEST(InventoryMassTests, RemoveFromHands_ClearsBothHandsWhenEmptied) {
	Inventory inv;
	inv.leftHand = ItemStack{"Wood", 8};
	inv.rightHand = ItemStack{"Wood", 8};
	EXPECT_EQ(removeFromHands(inv, "Wood", 99), 8U); // clamped to held
	EXPECT_FALSE(inv.leftHand.has_value());
	EXPECT_FALSE(inv.rightHand.has_value());
}

TEST(InventoryMassTests, RemoveFromHands_MissingItemRemovesNothing) {
	Inventory inv;
	inv.leftHand = ItemStack{"Wood", 5};
	inv.rightHand = ItemStack{"Wood", 5};
	EXPECT_EQ(removeFromHands(inv, "Stone", 3), 0U);
	ASSERT_TRUE(inv.leftHand.has_value());
	EXPECT_EQ(inv.leftHand->quantity, 5U); // untouched
}

// ============================================================================
// addArmful stack-size clamp: an armful is one stack, bounded by carry weight AND
// the item's stackSize (weight or count, whichever binds first). Registry-backed.
// ============================================================================

class ArmfulStackClampTest : public ::testing::Test {
  protected:
	void SetUp() override {
		auto& reg = engine::assets::AssetRegistry::Get();
		// Featherweight: stackSize 5 binds long before weight (0.01 kg -> 3500 fit at 35 kg).
		engine::assets::AssetDefinition feather;
		feather.defName = "Feather";
		feather.handsRequired = 2;
		feather.itemProperties = engine::assets::ItemProperties{};
		feather.itemProperties->stackSize = 5;
		feather.itemProperties->massKg = 0.01F;
		reg.registerTestDefinition(std::move(feather));
		// Wood: weight (14 at 35 kg / 2.5 kg) binds well below stackSize 40.
		engine::assets::AssetDefinition wood;
		wood.defName = "Wood";
		wood.handsRequired = 2;
		wood.itemProperties = engine::assets::ItemProperties{};
		wood.itemProperties->stackSize = 40;
		wood.itemProperties->massKg = 2.5F;
		reg.registerTestDefinition(std::move(wood));
	}
	void TearDown() override { engine::assets::AssetRegistry::Get().clearDefinitions(); }
};

TEST_F(ArmfulStackClampTest, CappedByStackSizeWhenWeightAllowsMore) {
	Inventory inv = Inventory::createForColonist(); // 35 kg cap
	auto&	  reg = engine::assets::AssetRegistry::Get();
	// Weight would allow ~3500 feathers; the stackSize of 5 is the binding cap.
	EXPECT_EQ(addArmful(inv, reg, "Feather", 100), 5U);
	ASSERT_TRUE(inv.leftHand.has_value());
	ASSERT_TRUE(inv.rightHand.has_value());
	EXPECT_EQ(inv.leftHand->quantity, 5U);
	EXPECT_EQ(inv.rightHand->quantity, 5U); // mirror in sync
}

TEST_F(ArmfulStackClampTest, CappedByWeightWhenStackAllowsMore) {
	Inventory inv = Inventory::createForColonist();
	auto&	  reg = engine::assets::AssetRegistry::Get();
	// stackSize 40 exceeds the 14 wood that 35 kg allows -> weight still binds (unchanged).
	EXPECT_EQ(addArmful(inv, reg, "Wood", 100), 14U);
}

TEST_F(ArmfulStackClampTest, GrowsOnlyUpToStackCap) {
	Inventory inv = Inventory::createForColonist();
	auto&	  reg = engine::assets::AssetRegistry::Get();
	EXPECT_EQ(addArmful(inv, reg, "Feather", 3), 3U); // seat 3
	EXPECT_EQ(addArmful(inv, reg, "Feather", 5), 2U); // grow toward the 5 cap: only 2 more fit
	EXPECT_EQ(handHeldQuantity(inv, "Feather"), 5U);
}
