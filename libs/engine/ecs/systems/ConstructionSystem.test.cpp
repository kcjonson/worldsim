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

#include <construction/ConstructionWorld.h>

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

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
			// non-zero defNameIds or emitMaterialGoals skips them. Wood carries the real 2.5 kg
			// mass so the per-trip harvest bound (carryCapacityKg / mass) is the game's 14.
			for (const char* name : {"Wood", "Stone"}) {
				engine::assets::AssetDefinition def;
				def.defName = name;
				def.label = name;
				engine::assets::ItemProperties item;
				item.massKg = std::string(name) == "Wood" ? 2.5F : 1.0F;
				def.itemProperties = item;
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

		/// An opening blueprint (door/window) per the F1b entity contract: starts at
		/// AwaitingMaterials (no clear phase), single-material manifest, graphId = openingId.
		EntityID createOpeningBlueprint(uint64_t openingId, glm::vec2 pos = {5.0F, 7.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{pos});
			world->addComponent<Structure>(entity, Structure{StructureKind::Opening, openingId});
			world->addComponent<Inventory>(entity, Inventory{});
			StructureBlueprint bp;
			bp.phase = StructureBlueprint::BuildPhase::AwaitingMaterials;
			bp.required = {{"Wood", 10}};
			bp.workTotal = 20.0F;
			world->addComponent<StructureBlueprint>(entity, bp);
			return entity;
		}

		/// A BUILT structure (phase Complete, work fully done, manifest delivered) flagged for
		/// demolition. graphId 0 + no ConstructionWorld means the cascade gate is ungated, so its
		/// Deconstruct goal goes Available immediately. kind selects the structural role.
		EntityID createBuiltDemolishing(StructureKind kind, glm::vec2 pos = {5.0F, 7.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{pos});
			world->addComponent<Structure>(entity, Structure{kind, /*graphId=*/0});
			StructureBlueprint bp;
			bp.phase = StructureBlueprint::BuildPhase::Complete;
			bp.required = {{"Wood", 30}};
			bp.delivered = {{"Wood", 30}};
			bp.workTotal = 100.0F;
			bp.workDone = 100.0F;
			bp.demolishing = true;
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

// ============================================================================
// Deconstruct (work-driven demolish): a demolishing blueprint emits a top-level
// Deconstruct goal instead of nothing. With no ConstructionWorld wired the cascade
// gate is ungated, so a built structure's Deconstruct goal goes Available at once. A
// Build umbrella parked on the entity from before the demolish order is replaced.
// ============================================================================

TEST_F(ConstructionGoalEmissionTest, DemolishingBuiltStructureEmitsAvailableDeconstructGoal) {
	auto  foundation = createBuiltDemolishing(StructureKind::Foundation);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* goal = registry.getGoalByDestination(foundation);
	ASSERT_NE(goal, nullptr);
	EXPECT_EQ(goal->type, TaskType::Deconstruct);
	EXPECT_EQ(goal->status, GoalStatus::Available) << "no dependents (ungated) -> Deconstruct goal Available";
	EXPECT_FALSE(goal->parentGoalId.has_value()) << "the Deconstruct goal is top-level, not a child";
	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 1U) << "one Deconstruct goal, no children";
}

TEST_F(ConstructionGoalEmissionTest, NoWorkDemolishingStructureIsRemovedImmediatelyViaCallback) {
	// A demolishing blueprint with workDone <= 0 (an unbuilt blueprint, nothing to undo) can't be
	// work-deconstructed (startBuildAction rejects it). It must fire the deconstructed callback
	// immediately and emit NO Deconstruct goal.
	auto entity = world->createEntity();
	world->addComponent<Position>(entity, Position{{1.0F, 2.0F}});
	world->addComponent<Structure>(entity, Structure{StructureKind::Foundation, /*graphId=*/0});
	StructureBlueprint bp;
	bp.phase = StructureBlueprint::BuildPhase::AwaitingMaterials;
	bp.required = {{"Wood", 30}};
	bp.workTotal = 100.0F;
	bp.workDone = 0.0F; // no work invested
	bp.demolishing = true;
	world->addComponent<StructureBlueprint>(entity, bp);

	EntityID removed = kInvalidEntity;
	construction->setStructureDeconstructedCallback([&](EntityID e) { removed = e; });

	refresh();

	EXPECT_EQ(removed, entity) << "a no-work demolishing blueprint must fire the deconstructed callback";
	EXPECT_EQ(GoalTaskRegistry::Get().getGoalByDestination(entity), nullptr) << "no Deconstruct goal for a no-work structure";
}

TEST_F(ConstructionGoalEmissionTest, DemolishOrderReplacesAnExistingBuildUmbrella) {
	// A structure mid-build (Build umbrella + children) is then marked for demolition: the next
	// refresh must drop the Build tree and leave a single Deconstruct goal on the destination.
	auto  foundation = createTwoMaterialFoundation();
	auto& registry = GoalTaskRegistry::Get();
	refresh();
	ASSERT_EQ(registry.getGoalByDestination(foundation)->type, TaskType::Build);
	ASSERT_GT(registry.goalCount(GoalOwner::ConstructionGoalSystem), 1U);

	// Mark it demolishing with work invested so it takes the worked path (not the no-work edge).
	auto* bp = world->getComponent<StructureBlueprint>(foundation);
	bp->workDone = 10.0F;
	bp->demolishing = true;
	refresh();

	const auto* goal = registry.getGoalByDestination(foundation);
	ASSERT_NE(goal, nullptr);
	EXPECT_EQ(goal->type, TaskType::Deconstruct) << "Build umbrella replaced by a Deconstruct goal";
	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 1U) << "Build children gone, only the Deconstruct goal";
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
	// worth, NOT 313. One trip is the carry weight (35 kg) divided by Wood's mass (2.5 kg) =
	// 14. Asking for 313 is what kept it Available after the load filled.
	auto foundation = createLargeWoodFoundation(/*woodNeeded=*/313);
	createColonistCarryingWood(/*woodCarried=*/0);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* umbrella = registry.getGoalByDestination(foundation);
	ASSERT_NE(umbrella, nullptr);
	const auto* harvest = findChild(umbrella->id, TaskType::Harvest);
	const auto* haul = findChild(umbrella->id, TaskType::Haul);
	ASSERT_NE(harvest, nullptr) << "empty-handed colonist + outstanding manifest must have a Harvest goal";
	ASSERT_NE(haul, nullptr);
	EXPECT_EQ(harvest->targetAmount, 14U) << "Harvest demand must cap at one carry trip (35 kg / 2.5 kg), not the full 313";
	EXPECT_EQ(haul->targetAmount, 313U) << "Haul targets the whole site shortfall";
}

TEST_F(ConstructionGoalEmissionTest, FullStackRetiresHarvestSoHaulWinsForLargeManifest) {
	// 313 Wood needed, a colonist carrying far more than one trip (99 >> the 14-unit trip):
	// the Harvest goal must retire (demand 0) while the Haul stays open. Before the trip
	// bound the Harvest stayed Available (313 - carried > 0) and the colonist hoarded forever.
	auto foundation = createLargeWoodFoundation(/*woodNeeded=*/313);
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

// ============================================================================
// Openings (doors/windows): same gated lifecycle as walls but one level up. An
// opening waits on its host wall SEGMENT being Built, has no clear phase, and then
// hauls + builds through the shared loop. The null-world case (this fixture wires
// no ConstructionWorld) proves the headless ungate: isOpeningHostSegmentBuilt
// returns true, so the opening immediately hauls its material.
// ============================================================================

TEST_F(ConstructionGoalEmissionTest, OpeningWithNoWorldIsUngatedAndHaulsImmediately) {
	// No ConstructionWorld wired -> isOpeningHostSegmentBuilt is true (headless ungate),
	// so the opening flows straight into AwaitingMaterials and emits haul goals.
	auto  opening = createOpeningBlueprint(/*openingId=*/1);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* umbrella = registry.getGoalByDestination(opening);
	ASSERT_NE(umbrella, nullptr);
	EXPECT_EQ(umbrella->type, TaskType::Build);
	EXPECT_EQ(umbrella->status, GoalStatus::Blocked) << "umbrella Blocked while materials outstanding";

	auto children = registry.getChildGoals(umbrella->id);
	EXPECT_EQ(countOfType(children, TaskType::Harvest), 1U);
	EXPECT_EQ(countOfType(children, TaskType::Haul), 1U);
}

TEST_F(ConstructionGoalEmissionTest, OpeningNeverEmitsClearGoals) {
	// An opening sits on a built wall: it has no clear phase, so even on first sight it
	// emits no Harvest CLEAR goal. (Its only Harvest is the per-material delivery chain,
	// which carries a chainId; a clear goal would not.)
	auto  opening = createOpeningBlueprint(/*openingId=*/1);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* umbrella = registry.getGoalByDestination(opening);
	ASSERT_NE(umbrella, nullptr);
	for (const auto* child : registry.getChildGoals(umbrella->id)) {
		if (child->type == TaskType::Harvest) {
			EXPECT_TRUE(child->chainId.has_value()) << "an opening's only Harvest is a material chain, never a clear goal";
		}
	}
}

// ============================================================================
// Opening host-segment gate, driven through a real ConstructionWorld. The gate
// holds the opening at a Blocked umbrella with NO haul/build goals while its host
// segment is Blueprint, and releases it (haul, then build) once the segment is Built.
// ============================================================================

namespace {
	namespace cw = engine::construction;

	class OpeningHostGateTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			GoalTaskRegistry::Get().clear();
			auto& assets = engine::assets::AssetRegistry::Get();
			assets.clearDefinitions();
			engine::assets::AssetDefinition def;
			def.defName = "Wood";
			def.label = "Wood";
			assets.registerTestDefinition(std::move(def));

			world = std::make_unique<World>();
			construction = &world->registerSystem<ConstructionSystem>();
			construction->setConstructionWorld(&topology);
		}

		void TearDown() override {
			world.reset();
			GoalTaskRegistry::Get().clear();
			engine::assets::AssetRegistry::Get().clearDefinitions();
		}

		/// Commit a host foundation + wall segment, attach an opening at t=0.5, and return
		/// the opening id. The segment starts Blueprint.
		cw::OpeningId makeOpeningOnSegment() {
			// CCW integer-mm box (1000 mm == 1 m), the proven host-foundation shape.
			geometry::Ring	 ring{{-10000, -10000}, {100000, -10000}, {100000, 100000}, {-10000, 100000}};
			cw::CommitResult host = topology.commitFoundation(std::move(ring), "wood");
			EXPECT_TRUE(host.ok());
			cw::SegmentCommitResult seg = topology.commitSegment({0, 0}, {4000, 0}, "wood", "thin", host.id);
			EXPECT_TRUE(seg.ok());
			hostFoundation = host.id;
			hostSegment = seg.id;
			return topology.addOpening(seg.id, 0.5F, "door", "wood");
		}

		EntityID createOpeningBlueprint(uint64_t openingId, glm::vec2 pos = {1.0F, 1.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{pos});
			world->addComponent<Structure>(entity, Structure{StructureKind::Opening, openingId});
			world->addComponent<Inventory>(entity, Inventory{});
			StructureBlueprint bp;
			bp.phase = StructureBlueprint::BuildPhase::AwaitingMaterials;
			bp.required = {{"Wood", 10}};
			bp.workTotal = 20.0F;
			world->addComponent<StructureBlueprint>(entity, bp);
			return entity;
		}

		/// A BUILT structure (full work invested) flagged for demolition, linked to a real
		/// topology node by graphId, so the cascade gate queries the live ConstructionWorld.
		EntityID createBuiltDemolishing(StructureKind kind, uint64_t graphId, glm::vec2 pos = {1.0F, 1.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{pos});
			world->addComponent<Structure>(entity, Structure{kind, graphId});
			StructureBlueprint bp;
			bp.phase = StructureBlueprint::BuildPhase::Complete;
			bp.required = {{"Wood", 10}};
			bp.delivered = {{"Wood", 10}};
			bp.workTotal = 20.0F;
			bp.workDone = 20.0F;
			bp.demolishing = true;
			world->addComponent<StructureBlueprint>(entity, bp);
			return entity;
		}

		void refresh() {
			for (int i = 0; i < 30; ++i) {
				world->update(0.016F);
			}
		}

		cw::ConstructionWorld  topology;
		cw::FoundationId	   hostFoundation = cw::kInvalidFoundation;
		cw::SegmentId		   hostSegment = cw::kInvalidSegment;
		std::unique_ptr<World> world;
		ConstructionSystem*	   construction = nullptr;
	};
} // namespace

TEST_F(OpeningHostGateTest, BlueprintSegmentGatesOpeningAtBlockedUmbrellaWithNoGoals) {
	const cw::OpeningId opening = makeOpeningOnSegment();
	ASSERT_NE(opening, cw::kInvalidOpening);
	auto  blueprint = createOpeningBlueprint(opening);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	// Host segment is still Blueprint: the opening holds a Blocked umbrella, NO children,
	// and stays AwaitingMaterials.
	const auto* bp = world->getComponent<StructureBlueprint>(blueprint);
	ASSERT_NE(bp, nullptr);
	EXPECT_EQ(bp->phase, StructureBlueprint::BuildPhase::AwaitingMaterials);

	const auto* umbrella = registry.getGoalByDestination(blueprint);
	ASSERT_NE(umbrella, nullptr);
	EXPECT_EQ(umbrella->type, TaskType::Build);
	EXPECT_EQ(umbrella->status, GoalStatus::Blocked);
	EXPECT_TRUE(registry.getChildGoals(umbrella->id).empty()) << "gated opening must emit no haul/build child goals";
}

TEST_F(OpeningHostGateTest, BuiltSegmentReleasesOpeningToHaulThenBuild) {
	const cw::OpeningId opening = makeOpeningOnSegment();
	ASSERT_NE(opening, cw::kInvalidOpening);
	auto  blueprint = createOpeningBlueprint(opening);
	auto& registry = GoalTaskRegistry::Get();

	// Flip the host segment to Built: the gate opens.
	ASSERT_TRUE(topology.setSegmentState(hostSegment, cw::FoundationState::Built));

	refresh();

	// Now hauling: a Harvest + Haul child per material under the umbrella.
	const auto* umbrella = registry.getGoalByDestination(blueprint);
	ASSERT_NE(umbrella, nullptr);
	auto children = registry.getChildGoals(umbrella->id);
	EXPECT_EQ(countOfType(children, TaskType::Harvest), 1U);
	EXPECT_EQ(countOfType(children, TaskType::Haul), 1U);

	// Stage the materials on the blueprint's delivery inventory, then refresh: the opening
	// advances to UnderConstruction and the umbrella flips Available (the Build goal).
	world->getComponent<Inventory>(blueprint)->addItem("Wood", 10);
	refresh();

	const auto* bp = world->getComponent<StructureBlueprint>(blueprint);
	ASSERT_NE(bp, nullptr);
	EXPECT_EQ(bp->phase, StructureBlueprint::BuildPhase::UnderConstruction);

	const auto* umbrella2 = registry.getGoalByDestination(blueprint);
	ASSERT_NE(umbrella2, nullptr);
	EXPECT_EQ(umbrella2->status, GoalStatus::Available) << "materials staged -> umbrella Build goal Available";
}

TEST_F(OpeningHostGateTest, UnknownOpeningStaysGated) {
	// graphId points at no opening in the topology: the gate stays closed (Blocked, no goals).
	auto  blueprint = createOpeningBlueprint(/*openingId=*/9999);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* umbrella = registry.getGoalByDestination(blueprint);
	ASSERT_NE(umbrella, nullptr);
	EXPECT_EQ(umbrella->status, GoalStatus::Blocked);
	EXPECT_TRUE(registry.getChildGoals(umbrella->id).empty()) << "an opening with no topology node stays gated";
}

// ============================================================================
// Deconstruct cascade gate (driven through a real ConstructionWorld): a demolishing
// foundation stays Blocked while a wall is hosted on it; a demolishing wall stays
// Blocked while an opening sits on it; each goes Available once its dependent is gone.
// This is what orders a whole-building teardown: openings -> walls -> foundation.
// ============================================================================

TEST_F(OpeningHostGateTest, DemolishingFoundationGatedWhileWallStands) {
	const cw::OpeningId opening = makeOpeningOnSegment();
	ASSERT_NE(opening, cw::kInvalidOpening);
	auto  foundationBp = createBuiltDemolishing(StructureKind::Foundation, hostFoundation);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	// A wall is still hosted on the foundation: its Deconstruct goal is Blocked.
	const auto* goal = registry.getGoalByDestination(foundationBp);
	ASSERT_NE(goal, nullptr);
	EXPECT_EQ(goal->type, TaskType::Deconstruct);
	EXPECT_EQ(goal->status, GoalStatus::Blocked) << "foundation must wait for its walls";

	// Remove the wall (and its opening): the gate opens and the goal goes Available.
	topology.removeSegment(hostSegment);
	refresh();

	const auto* goal2 = registry.getGoalByDestination(foundationBp);
	ASSERT_NE(goal2, nullptr);
	EXPECT_EQ(goal2->status, GoalStatus::Available) << "no walls left -> foundation Deconstruct Available";
}

TEST_F(OpeningHostGateTest, DemolishingWallGatedWhileOpeningStands) {
	const cw::OpeningId opening = makeOpeningOnSegment();
	ASSERT_NE(opening, cw::kInvalidOpening);
	auto  wallBp = createBuiltDemolishing(StructureKind::Wall, hostSegment);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	// An opening still sits on the wall: its Deconstruct goal is Blocked.
	const auto* goal = registry.getGoalByDestination(wallBp);
	ASSERT_NE(goal, nullptr);
	EXPECT_EQ(goal->type, TaskType::Deconstruct);
	EXPECT_EQ(goal->status, GoalStatus::Blocked) << "wall must wait for its openings";

	// Remove the opening: the gate opens and the wall's goal goes Available.
	topology.removeOpening(opening);
	refresh();

	const auto* goal2 = registry.getGoalByDestination(wallBp);
	ASSERT_NE(goal2, nullptr);
	EXPECT_EQ(goal2->status, GoalStatus::Available) << "no openings left -> wall Deconstruct Available";
}
