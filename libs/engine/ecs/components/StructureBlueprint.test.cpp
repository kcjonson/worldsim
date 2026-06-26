#include "StructureBlueprint.h"

#include <gtest/gtest.h>

using namespace ecs;

// ============================================================================
// materialsComplete
// ============================================================================

TEST(StructureBlueprintTests, MaterialsCompleteWhenRequiredIsEmpty) {
	StructureBlueprint bp;
	EXPECT_TRUE(bp.materialsComplete());
}

TEST(StructureBlueprintTests, MaterialsCompleteUnderDelivery) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 10}};
	bp.delivered = {{"Wood", 5}};
	EXPECT_FALSE(bp.materialsComplete());
}

TEST(StructureBlueprintTests, MaterialsCompleteExactDelivery) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 10}};
	bp.delivered = {{"Wood", 10}};
	EXPECT_TRUE(bp.materialsComplete());
}

TEST(StructureBlueprintTests, MaterialsCompleteOverDelivery) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 10}};
	bp.delivered = {{"Wood", 15}};
	EXPECT_TRUE(bp.materialsComplete());
}

TEST(StructureBlueprintTests, MaterialsCompleteMultiMaterialAllMet) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 5}, {"Stone", 3}};
	bp.delivered = {{"Wood", 5}, {"Stone", 4}};
	EXPECT_TRUE(bp.materialsComplete());
}

TEST(StructureBlueprintTests, MaterialsCompleteMultiMaterialOneMissing) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 5}, {"Stone", 3}};
	bp.delivered = {{"Wood", 5}, {"Stone", 2}};
	EXPECT_FALSE(bp.materialsComplete());
}

TEST(StructureBlueprintTests, MaterialsCompleteDeliveredHasExtraItems) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 5}};
	bp.delivered = {{"Wood", 5}, {"Stone", 99}}; // Stone wasn't required
	EXPECT_TRUE(bp.materialsComplete());
}

TEST(StructureBlueprintTests, MaterialsCompleteNoDeliveries) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 5}};
	EXPECT_FALSE(bp.materialsComplete());
}

// ============================================================================
// remaining()
// ============================================================================

TEST(StructureBlueprintTests, RemainingForUnknownDefName) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 5}};
	EXPECT_EQ(bp.remaining("Stone"), 0U);
}

TEST(StructureBlueprintTests, RemainingWhenNothingDelivered) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 10}};
	EXPECT_EQ(bp.remaining("Wood"), 10U);
}

TEST(StructureBlueprintTests, RemainingPartialDelivery) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 10}};
	bp.delivered = {{"Wood", 4}};
	EXPECT_EQ(bp.remaining("Wood"), 6U);
}

TEST(StructureBlueprintTests, RemainingExactDelivery) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 10}};
	bp.delivered = {{"Wood", 10}};
	EXPECT_EQ(bp.remaining("Wood"), 0U);
}

TEST(StructureBlueprintTests, RemainingOverDelivery) {
	StructureBlueprint bp;
	bp.required = {{"Wood", 10}};
	bp.delivered = {{"Wood", 20}};
	EXPECT_EQ(bp.remaining("Wood"), 0U);
}

// ============================================================================
// progress()
// ============================================================================

TEST(StructureBlueprintTests, ProgressWhenWorkTotalIsZero) {
	StructureBlueprint bp;
	EXPECT_FLOAT_EQ(bp.progress(), 0.0F);
}

TEST(StructureBlueprintTests, ProgressZeroWorkDone) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = 0.0F;
	EXPECT_FLOAT_EQ(bp.progress(), 0.0F);
}

TEST(StructureBlueprintTests, ProgressHalfway) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = 50.0F;
	EXPECT_FLOAT_EQ(bp.progress(), 0.5F);
}

TEST(StructureBlueprintTests, ProgressComplete) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = 100.0F;
	EXPECT_FLOAT_EQ(bp.progress(), 1.0F);
}

TEST(StructureBlueprintTests, ProgressClampedAboveOne) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = 150.0F; // over-work (shouldn't happen but must not crash)
	EXPECT_FLOAT_EQ(bp.progress(), 1.0F);
}

TEST(StructureBlueprintTests, ProgressClampedBelowZero) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = -10.0F; // corrupted state; must not go below 0
	EXPECT_FLOAT_EQ(bp.progress(), 0.0F);
}

// ============================================================================
// displayProgress(): Build shows raw progress; Deconstruct shows the complement
// because its workDone counts DOWN from workTotal toward 0.
// ============================================================================

TEST(StructureBlueprintTests, DisplayProgressBuildMatchesRawProgress) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = 25.0F;
	EXPECT_FLOAT_EQ(bp.displayProgress(false), 0.25F);
}

// A deconstruct starts at workDone == workTotal (raw progress 1.0) and the meter must read 0%,
// rising toward 100% as workDone drains to 0.
TEST(StructureBlueprintTests, DisplayProgressDeconstructStartsEmpty) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = 100.0F; // demolition just began
	EXPECT_FLOAT_EQ(bp.displayProgress(true), 0.0F);
}

TEST(StructureBlueprintTests, DisplayProgressDeconstructHalfway) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = 75.0F; // a quarter torn down
	EXPECT_FLOAT_EQ(bp.displayProgress(true), 0.25F);
}

TEST(StructureBlueprintTests, DisplayProgressDeconstructNearlyDoneReadsFull) {
	StructureBlueprint bp;
	bp.workTotal = 100.0F;
	bp.workDone = 0.0F; // fully torn down
	EXPECT_FLOAT_EQ(bp.displayProgress(true), 1.0F);
}

// ============================================================================
// Default state
// ============================================================================

TEST(StructureBlueprintTests, DefaultPhaseIsClearing) {
	StructureBlueprint bp;
	EXPECT_EQ(bp.phase, StructureBlueprint::BuildPhase::Clearing);
}

TEST(StructureBlueprintTests, DefaultDemolishingIsFalse) {
	StructureBlueprint bp;
	EXPECT_FALSE(bp.demolishing);
}
