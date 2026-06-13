#include "ConstructionSystem.h"

#include "../components/StructureBlueprint.h"

#include <gtest/gtest.h>

using namespace ecs;

namespace {
	StructureBlueprint makeBlueprint(StructureBlueprint::BuildPhase phase) {
		StructureBlueprint bp;
		bp.phase	= phase;
		bp.required = {{"Wood", 100}};
		bp.workTotal = 50.0F;
		return bp;
	}
} // namespace

// ============================================================================
// decideConstructionPhase: the pure phase-decision + goal-emission logic.
// One measured fact each: footprint clear?, materials complete?
// ============================================================================

TEST(ConstructionSystemTests, BlockedFootprintStaysClearingAndEmitsClearGoals) {
	auto bp = makeBlueprint(StructureBlueprint::BuildPhase::Clearing);
	auto decision = decideConstructionPhase(bp, /*footprintClear=*/false, /*materialsComplete=*/false);

	EXPECT_EQ(decision.nextPhase, StructureBlueprint::BuildPhase::Clearing);
	EXPECT_TRUE(decision.emitClearGoals);
	EXPECT_FALSE(decision.emitMaterialGoals);
	EXPECT_FALSE(decision.emitBuildGoal);
}

TEST(ConstructionSystemTests, ClearSiteAdvancesToAwaitingMaterialsAndEmitsMaterialGoals) {
	// A blueprint with a clear site but outstanding materials advances to AwaitingMaterials.
	auto bp = makeBlueprint(StructureBlueprint::BuildPhase::Clearing);
	auto decision = decideConstructionPhase(bp, /*footprintClear=*/true, /*materialsComplete=*/false);

	EXPECT_EQ(decision.nextPhase, StructureBlueprint::BuildPhase::AwaitingMaterials);
	EXPECT_FALSE(decision.emitClearGoals);
	EXPECT_TRUE(decision.emitMaterialGoals);
	EXPECT_FALSE(decision.emitBuildGoal);
}

TEST(ConstructionSystemTests, DeliveredEqualsRequiredAdvancesToUnderConstructionAndEmitsBuild) {
	// Clear site, materials complete -> UnderConstruction, emit a Build goal.
	auto bp = makeBlueprint(StructureBlueprint::BuildPhase::AwaitingMaterials);
	auto decision = decideConstructionPhase(bp, /*footprintClear=*/true, /*materialsComplete=*/true);

	EXPECT_EQ(decision.nextPhase, StructureBlueprint::BuildPhase::UnderConstruction);
	EXPECT_FALSE(decision.emitClearGoals);
	EXPECT_FALSE(decision.emitMaterialGoals);
	EXPECT_TRUE(decision.emitBuildGoal);
}

TEST(ConstructionSystemTests, CompletePhaseEmitsNothing) {
	auto bp = makeBlueprint(StructureBlueprint::BuildPhase::Complete);
	auto decision = decideConstructionPhase(bp, /*footprintClear=*/true, /*materialsComplete=*/true);

	EXPECT_EQ(decision.nextPhase, StructureBlueprint::BuildPhase::Complete);
	EXPECT_FALSE(decision.emitClearGoals);
	EXPECT_FALSE(decision.emitMaterialGoals);
	EXPECT_FALSE(decision.emitBuildGoal);
}

TEST(ConstructionSystemTests, DemolishingEmitsNothingAndHoldsPhase) {
	auto bp = makeBlueprint(StructureBlueprint::BuildPhase::UnderConstruction);
	bp.demolishing = true;
	auto decision = decideConstructionPhase(bp, /*footprintClear=*/true, /*materialsComplete=*/true);

	EXPECT_EQ(decision.nextPhase, StructureBlueprint::BuildPhase::UnderConstruction);
	EXPECT_FALSE(decision.emitClearGoals);
	EXPECT_FALSE(decision.emitMaterialGoals);
	EXPECT_FALSE(decision.emitBuildGoal);
}

TEST(ConstructionSystemTests, ClearingGateTakesPriorityOverMaterials) {
	// Even with materials complete, a blocked footprint keeps the blueprint Clearing:
	// the clear gate is checked first.
	auto bp = makeBlueprint(StructureBlueprint::BuildPhase::Clearing);
	auto decision = decideConstructionPhase(bp, /*footprintClear=*/false, /*materialsComplete=*/true);

	EXPECT_EQ(decision.nextPhase, StructureBlueprint::BuildPhase::Clearing);
	EXPECT_TRUE(decision.emitClearGoals);
	EXPECT_FALSE(decision.emitBuildGoal);
}
