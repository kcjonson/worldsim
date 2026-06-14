#include "ConstructionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/Needs.h"
#include "../components/Structure.h"
#include "../components/StructureBlueprint.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>

#include <gtest/gtest.h>

#include <algorithm>

using namespace ecs;

namespace {
	StructureBlueprint makeBlueprint(StructureBlueprint::BuildPhase phase) {
		StructureBlueprint bp;
		bp.phase = phase;
		bp.required = {{"Wood", 100}};
		bp.workTotal = 50.0F;
		return bp;
	}

	size_t countOfType(const std::vector<const GoalTask*>& goals, TaskType type) {
		return static_cast<size_t>(std::count_if(goals.begin(), goals.end(), [type](const GoalTask* g) { return g->type == type; }));
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
// constructionHarvestDemand: bound chopping by what is already carried AND by one
// trip's carry capacity, so the colonist delivers its load instead of topping up
// forever (the haul-loop stall). The capacity bound is what lets a manifest larger
// than one stack ever build: `carried` caps at the stack size, so without it the
// demand would stay > 0 forever and the colonist would hoard at the cap.
// ============================================================================

TEST(ConstructionSystemTests, HarvestDemandIsSiteShortfallWhenCarryingNothing) {
	// Small site (fits in one trip): chop the whole shortfall.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/20, /*carried=*/0, /*carryCapacity=*/99), 20U);
}

TEST(ConstructionSystemTests, HarvestDemandShrinksByWhatIsCarried) {
	// Carrying some Wood toward a small site: only chop the difference.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/20, /*carried=*/8, /*carryCapacity=*/99), 12U);
}

TEST(ConstructionSystemTests, HarvestDemandIsZeroWhenCarryingEnough) {
	// Carrying exactly enough or more for a small site: stop chopping, go deliver.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/20, /*carried=*/20, /*carryCapacity=*/99), 0U);
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/20, /*carried=*/35, /*carryCapacity=*/99), 0U);
}

TEST(ConstructionSystemTests, HarvestDemandCapsAtOneTripForLargeManifest) {
	// The regression: a 313-Wood site with an empty-handed colonist must ask for ONE
	// trip's worth (99), not the whole 313. Asking for 313 keeps the Harvest goal
	// Available even when the colonist's stack is full, so it never delivers.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/313, /*carried=*/0, /*carryCapacity=*/99), 99U);
}

TEST(ConstructionSystemTests, HarvestDemandIsZeroWhenStackFullEvenIfSiteStillNeedsMore) {
	// Stack full (99) but the site still needs 313: demand is 0 so the colonist DELIVERS
	// its trip instead of hoarding at the cap. This is the exact stall the fix removes.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/313, /*carried=*/99, /*carryCapacity=*/99), 0U);
}

TEST(ConstructionSystemTests, HarvestDemandToppingUpToAFullTrip) {
	// Large site, carrying a partial load: top the trip up to one full stack, no more.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/313, /*carried=*/40, /*carryCapacity=*/99), 59U);
}

TEST(ConstructionSystemTests, HarvestDemandLastTripIsTheRemainder) {
	// Final trip: less than a stack still needed and hands empty -> chop just the remainder.
	EXPECT_EQ(constructionHarvestDemand(/*remaining=*/15, /*carried=*/0, /*carryCapacity=*/99), 15U);
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
				def.label = name;
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
			bp.phase = StructureBlueprint::BuildPhase::Clearing;
			bp.required = {{"Wood", 30}, {"Stone", 20}};
			bp.workTotal = 100.0F;
			world->addComponent<StructureBlueprint>(entity, bp);
			return entity;
		}

		/// A clear single-material foundation whose Wood manifest exceeds one carry stack
		/// (the bug case: 313 Wood with a 99 stack size).
		EntityID createLargeWoodFoundation(uint32_t woodNeeded, glm::vec2 pos = {5.0F, 7.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{pos});
			world->addComponent<Structure>(entity, Structure{StructureKind::Foundation, /*graphId=*/0});
			StructureBlueprint bp;
			bp.phase = StructureBlueprint::BuildPhase::Clearing;
			bp.required = {{"Wood", woodNeeded}};
			bp.workTotal = 100.0F;
			world->addComponent<StructureBlueprint>(entity, bp);
			return entity;
		}

		/// A colonist carrying `woodCarried` Wood in its backpack (capped at the stack size).
		/// NeedsComponent marks it a colonist so carriedAmount/colonistCarryCapacity see it.
		EntityID createColonistCarryingWood(uint32_t woodCarried) {
			auto entity = world->createEntity();
			world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
			auto inv = Inventory::createForColonist();
			inv.addItem("Wood", woodCarried); // clamps to maxStackSize
			world->addComponent<Inventory>(entity, std::move(inv));
			return entity;
		}

		static const GoalTask* findChild(uint64_t umbrellaId, TaskType type) {
			for (const auto* g : GoalTaskRegistry::Get().getChildGoals(umbrellaId)) {
				if (g->type == type) {
					return g;
				}
			}
			return nullptr;
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
	auto  foundation = createTwoMaterialFoundation();
	auto& registry = GoalTaskRegistry::Get();

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
	auto  foundation = createTwoMaterialFoundation();
	auto& registry = GoalTaskRegistry::Get();

	refresh();
	ASSERT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 5U);

	// Flip the blueprint to Complete: the next refresh leaves it in the stale set and the cleanup
	// pass drops the umbrella and every child via removeGoalWithChildren.
	world->getComponent<StructureBlueprint>(foundation)->phase = StructureBlueprint::BuildPhase::Complete;
	refresh();

	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 0U);
	EXPECT_EQ(registry.getGoalByDestination(foundation), nullptr);
}

// ============================================================================
// The haul-loop stall (this fix): a manifest larger than one carry stack must still
// emit a sensibly-sized Harvest goal and, once a colonist's stack is full, retire the
// Harvest so the Haul wins and the load gets delivered. These drive the live update()
// path with a real colonist so carriedAmount + colonistCarryCapacity feed the bound.
// ============================================================================

TEST_F(ConstructionGoalEmissionTest, LargeManifestHarvestGoalIsBoundedToOneTrip) {
	// 313 Wood needed, an empty-handed colonist: the Harvest goal must ask for one trip's
	// worth (99), NOT 313. Asking for 313 is what kept it Available after the stack filled.
	auto  foundation = createLargeWoodFoundation(/*woodNeeded=*/313);
	createColonistCarryingWood(/*woodCarried=*/0);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* umbrella = registry.getGoalByDestination(foundation);
	ASSERT_NE(umbrella, nullptr);
	const auto* harvest = findChild(umbrella->id, TaskType::Harvest);
	const auto* haul = findChild(umbrella->id, TaskType::Haul);
	ASSERT_NE(harvest, nullptr) << "empty-handed colonist + outstanding manifest must have a Harvest goal";
	ASSERT_NE(haul, nullptr);
	EXPECT_EQ(harvest->targetAmount, 99U) << "Harvest demand must cap at one carry stack, not the full 313";
	EXPECT_EQ(haul->targetAmount, 313U) << "Haul targets the whole site shortfall";
}

TEST_F(ConstructionGoalEmissionTest, FullStackRetiresHarvestSoHaulWinsForLargeManifest) {
	// 313 Wood needed, a colonist carrying a FULL stack (addItem clamps 200 -> 99): the
	// Harvest goal must retire (demand 0) while the Haul stays open. Before the fix the
	// Harvest stayed Available (313 - 99 = 214 > 0) and the colonist hoarded forever.
	auto  foundation = createLargeWoodFoundation(/*woodNeeded=*/313);
	createColonistCarryingWood(/*woodCarried=*/200); // clamps to the 99 stack cap
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* umbrella = registry.getGoalByDestination(foundation);
	ASSERT_NE(umbrella, nullptr);
	EXPECT_EQ(findChild(umbrella->id, TaskType::Harvest), nullptr)
		<< "a full-stacked colonist must NOT have a Harvest goal (it should deliver)";
	const auto* haul = findChild(umbrella->id, TaskType::Haul);
	ASSERT_NE(haul, nullptr) << "the Haul goal must remain so the carried load gets delivered";
	EXPECT_EQ(haul->targetAmount, 313U);
}
