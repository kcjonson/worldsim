#include "ConstructionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/StructureBlueprint.h"
#include "../components/Structure.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>

using namespace ecs;

namespace {
	StructureBlueprint makeBlueprint(StructureBlueprint::BuildPhase phase) {
		StructureBlueprint bp;
		bp.phase	= phase;
		bp.required = {{"Wood", 100}};
		bp.workTotal = 50.0F;
		return bp;
	}

	size_t countOfType(const std::vector<const GoalTask*>& goals, TaskType type) {
		return static_cast<size_t>(std::count_if(goals.begin(), goals.end(), [type](const GoalTask* g) {
			return g->type == type;
		}));
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

// ============================================================================
// constructionHarvestDemand: bound chopping by what is already carried so the
// colonist delivers its load instead of topping up forever (the haul-loop stall).
// ============================================================================

TEST(ConstructionSystemTests, HarvestDemandIsSiteShortfallWhenCarryingNothing) {
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/20, /*carried=*/0), 20U);
}

TEST(ConstructionSystemTests, HarvestDemandShrinksByWhatIsCarried) {
	// Carrying some Wood toward the site: only chop the difference.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/20, /*carried=*/8), 12U);
}

TEST(ConstructionSystemTests, HarvestDemandIsZeroWhenCarryingEnough) {
	// Carrying exactly enough or more: stop chopping, go deliver.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/20, /*carried=*/20), 0U);
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/20, /*carried=*/35), 0U);
}

// ============================================================================
// Goal-graph emission: drive the live update() path against a real
// GoalTaskRegistry and assert the umbrella + per-material children coexist.
//
// This is the regression the pure-helper tests above cannot catch: before the
// umbrella refactor, each material's parent-less Harvest and Haul goals (plus a
// second material's) collided on the registry's single-top-level-goal-per-
// destination guard and clobbered each other. With the umbrella, every phase
// goal is a child and the whole set coexists, and survives repeated refreshes.
// ============================================================================

namespace {
	class ConstructionGoalEmissionTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			GoalTaskRegistry::Get().clear();
			auto& assets = engine::assets::AssetRegistry::Get();
			assets.clearDefinitions();

			// Two distinct materials so the multi-material collision is exercised. Both must have
			// non-zero defNameIds or emitMaterialGoals skips them.
			for (const char* name : {"Wood", "Stone"}) {
				engine::assets::AssetDefinition def;
				def.defName = name;
				def.label	= name;
				assets.registerTestDefinition(std::move(def));
			}

			world = std::make_unique<World>();
			construction = &world->registerSystem<ConstructionSystem>();
			// No ConstructionWorld/placement wired: isFootprintClear() returns true, so the
			// blueprint advances straight to AwaitingMaterials and emits material goals.
		}

		void TearDown() override {
			world.reset();
			GoalTaskRegistry::Get().clear();
			engine::assets::AssetRegistry::Get().clearDefinitions();
		}

		/// A two-material foundation blueprint that is clear of blockers but has no materials yet.
		EntityID createTwoMaterialFoundation(glm::vec2 pos = {5.0F, 7.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{pos});
			world->addComponent<Structure>(entity, Structure{StructureKind::Foundation, /*graphId=*/0});
			StructureBlueprint bp;
			bp.phase	 = StructureBlueprint::BuildPhase::Clearing;
			bp.required	 = {{"Wood", 30}, {"Stone", 20}};
			bp.workTotal = 100.0F;
			world->addComponent<StructureBlueprint>(entity, bp);
			return entity;
		}

		/// Run one ConstructionSystem cycle (it throttles to every 30 frames).
		void refresh() {
			for (int i = 0; i < 30; ++i) {
				world->update(0.016F);
			}
		}

		std::unique_ptr<World> world;
		ConstructionSystem*	   construction = nullptr;
	};
} // namespace

TEST_F(ConstructionGoalEmissionTest, TwoMaterialsEmitUmbrellaPlusAllChildrenAndSurviveSecondRefresh) {
	auto foundation = createTwoMaterialFoundation();
	auto& registry	= GoalTaskRegistry::Get();

	refresh();

	// The blueprint advanced to AwaitingMaterials (footprint clear, materials outstanding).
	const auto* bp = world->getComponent<StructureBlueprint>(foundation);
	ASSERT_NE(bp, nullptr);
	EXPECT_EQ(bp->phase, StructureBlueprint::BuildPhase::AwaitingMaterials);

	// One umbrella (the top-level Build goal owning the destination slot), Blocked while awaiting.
	const auto* umbrella = registry.getGoalByDestination(foundation);
	ASSERT_NE(umbrella, nullptr);
	EXPECT_EQ(umbrella->type, TaskType::Build);
	EXPECT_FALSE(umbrella->parentGoalId.has_value());
	EXPECT_EQ(umbrella->status, GoalStatus::Blocked);

	// Two Harvest + two Haul children, all parented to the umbrella, all coexisting.
	auto children = registry.getChildGoals(umbrella->id);
	EXPECT_EQ(countOfType(children, TaskType::Harvest), 2U);
	EXPECT_EQ(countOfType(children, TaskType::Haul), 2U);
	for (const auto* child : children) {
		ASSERT_TRUE(child->parentGoalId.has_value());
		EXPECT_EQ(child->parentGoalId.value(), umbrella->id);
		EXPECT_EQ(child->destinationEntity, foundation);
	}

	// Whole owned set: 1 umbrella + 4 children.
	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 5U);

	const uint64_t umbrellaId = umbrella->id;

	// A SECOND refresh must NOT clobber any of them (the original collision bug). The umbrella id
	// is stable, and both materials still have a coexisting Harvest + Haul child.
	refresh();

	const auto* umbrella2 = registry.getGoalByDestination(foundation);
	ASSERT_NE(umbrella2, nullptr);
	EXPECT_EQ(umbrella2->id, umbrellaId) << "umbrella id must stay stable across refreshes";

	auto children2 = registry.getChildGoals(umbrellaId);
	EXPECT_EQ(countOfType(children2, TaskType::Harvest), 2U) << "both Harvest children must survive";
	EXPECT_EQ(countOfType(children2, TaskType::Haul), 2U) << "both Haul children must survive";
	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 5U);
}

TEST_F(ConstructionGoalEmissionTest, GoalsAreCleanedUpWhenBlueprintCompletes) {
	auto foundation = createTwoMaterialFoundation();
	auto& registry	= GoalTaskRegistry::Get();

	refresh();
	ASSERT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 5U);

	// Flip the blueprint to Complete: the next refresh leaves it in the stale set and the cleanup
	// pass drops the umbrella and every child via removeGoalWithChildren.
	world->getComponent<StructureBlueprint>(foundation)->phase = StructureBlueprint::BuildPhase::Complete;
	refresh();

	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 0U);
	EXPECT_EQ(registry.getGoalByDestination(foundation), nullptr);
}
