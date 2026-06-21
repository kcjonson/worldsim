#include "InventoryMass.h"

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
