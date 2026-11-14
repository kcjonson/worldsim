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
#include <predicates/Predicates.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

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

// An L's notch lies inside its bounding box but is not footprint; the ring's boundary is.
TEST(FootprintBoundsTests, AnLsNotchIsOutsideItsFootprint) {
	const FootprintBounds footprint{geometry::Ring{{0, 0}, {8000, 0}, {8000, 4000}, {4000, 4000}, {4000, 8000}, {0, 8000}}};
	EXPECT_EQ(footprint.min, glm::vec2(0.0F, 0.0F));
	EXPECT_EQ(footprint.max, glm::vec2(8.0F, 8.0F));
	EXPECT_TRUE(footprint.contains({2.0F, 6.0F}));
	EXPECT_TRUE(footprint.contains({6.0F, 2.0F}));
	EXPECT_TRUE(footprint.contains({6.0F, 4.0F})) << "on the inner edge";
	EXPECT_FALSE(footprint.contains({6.0F, 6.0F}));
	EXPECT_TRUE(footprint.contains(footprint.interiorPoint()));
	EXPECT_EQ(footprint.nearestOutside({6.0F, 6.0F}, 0.5F), glm::vec2(6.0F, 6.0F)) << "already outside";
}

// Out of a long diagonal strip, a point goes across the strip's near side, not to its bounding box's edge.
TEST(FootprintBoundsTests, NearestOutsideADiagonalStripCrossesItsNearSide) {
	const FootprintBounds footprint{geometry::Ring{{0, 0}, {2000, 0}, {22000, 20000}, {20000, 20000}}};
	const glm::vec2		  inside{11.0F, 10.0F};
	ASSERT_TRUE(footprint.contains(inside));
	const glm::vec2 out = footprint.nearestOutside(inside, 0.5F);
	EXPECT_FALSE(footprint.contains(out)) << "at (" << out.x << ", " << out.y << ")";
	EXPECT_LT(glm::distance(out, inside), 1.3F) << "the strip is two metres wide across x, about 1.4 across its edges";
}

// A notch narrower than the margin: past the nearest edge is back on the floor, so the next edge is taken.
TEST(FootprintBoundsTests, NearestOutsideSkipsAnEdgeWhoseSpotLandsBackInside) {
	// A U whose slot is 0.4 m wide, between x 4 and 4.4, from y 2 to the top.
	const FootprintBounds footprint{geometry::Ring{{0, 0}, {8400, 0}, {8400, 8000}, {4400, 8000}, {4400, 2000}, {4000, 2000}, {4000, 8000}, {0, 8000}}};
	const glm::vec2		  inside{3.9F, 5.0F};
	const glm::vec2		  out = footprint.nearestOutside(inside, 1.0F);
	EXPECT_FALSE(footprint.contains(out)) << "at (" << out.x << ", " << out.y << ")";
}

// Just inside an L's inner corner two edges tie as nearest; the drop leaves by the corner into the notch, not across the floor.
TEST(FootprintBoundsTests, NearestOutsideNearAReflexCornerStaysNearTheCorner) {
	const FootprintBounds footprint{geometry::Ring{{0, 0}, {8000, 0}, {8000, 4000}, {4000, 4000}, {4000, 8000}, {0, 8000}}};
	const glm::vec2		  inside{3.9F, 3.9F};
	ASSERT_TRUE(footprint.contains(inside));
	const glm::vec2 out = footprint.nearestOutside(inside, 0.5F);
	EXPECT_FALSE(footprint.contains(out)) << "at (" << out.x << ", " << out.y << ")";
	EXPECT_LT(glm::distance(out, glm::vec2(4.0F, 4.0F)), 0.75F) << "at (" << out.x << ", " << out.y << ")";
}

TEST(FootprintBoundsTests, NearestOutsideFromOtherSpotsOnTheFloor) {
	const FootprintBounds footprint{geometry::Ring{{0, 0}, {8000, 0}, {8000, 4000}, {4000, 4000}, {4000, 8000}, {0, 8000}}};
	const auto			  expectOutsideNear = [&](glm::vec2 from, float within, const char* what) {
		ASSERT_TRUE(footprint.contains(from)) << what;
		const glm::vec2 out = footprint.nearestOutside(from, 0.5F);
		EXPECT_FALSE(footprint.contains(out)) << what << " at (" << out.x << ", " << out.y << ")";
		EXPECT_LT(glm::distance(out, from), within) << what << " at (" << out.x << ", " << out.y << ")";
	};
	expectOutsideNear({0.2F, 4.0F}, 1.0F, "near the west edge");
	expectOutsideNear({6.0F, 4.0F}, 1.0F, "on the inner edge");
	expectOutsideNear({8.0F, 0.0F}, 1.0F, "on a convex vertex");
	expectOutsideNear({1.0F, 1.0F}, 1.6F, "near the outer corner");
}

TEST(FootprintBoundsTests, NearestOutsideIsTheSameForAClockwiseRing) {
	const FootprintBounds counterClockwise{geometry::Ring{{0, 0}, {8000, 0}, {8000, 4000}, {4000, 4000}, {4000, 8000}, {0, 8000}}};
	const FootprintBounds clockwise{geometry::Ring{{0, 8000}, {4000, 8000}, {4000, 4000}, {8000, 4000}, {8000, 0}, {0, 0}}};
	for (const glm::vec2 from : {glm::vec2{3.9F, 3.9F}, glm::vec2{1.0F, 1.0F}, glm::vec2{6.0F, 2.0F}}) {
		EXPECT_EQ(clockwise.nearestOutside(from, 0.5F), counterClockwise.nearestOutside(from, 0.5F)) << from.x << ", " << from.y;
	}
}

// Where the nearest spot is a reflex vertex and the position lies on the line of one of its edges, the drop still goes
// into the notch, not along that edge to the far side of the ring.
TEST(FootprintBoundsTests, NearestOutsideOnTheLineOfAReflexCornersEdgeGoesIntoTheNotch) {
	const FootprintBounds footprint{geometry::Ring{{0, 0}, {8000, 0}, {8000, 4000}, {4000, 4000}, {4000, 8000}, {0, 8000}}};
	for (const glm::vec2 from : {glm::vec2{4.0F, 3.999F}, glm::vec2{3.999F, 4.0F}, glm::vec2{4.0F, 4.0F}}) {
		ASSERT_TRUE(footprint.contains(from));
		const glm::vec2 out = footprint.nearestOutside(from, 0.5F);
		EXPECT_FALSE(footprint.contains(out)) << "from (" << from.x << ", " << from.y << ") at (" << out.x << ", " << out.y << ")";
		EXPECT_LT(glm::distance(out, glm::vec2(4.0F, 4.0F)), 0.75F) << "from (" << from.x << ", " << from.y << ") at (" << out.x << ", " << out.y << ")";
	}
	const glm::vec2 corner = footprint.nearestOutside({4.0F, 4.0F}, 0.5F);
	EXPECT_NEAR(corner.x, 4.0F + 0.5F * 0.70710678F, 0.002F);
	EXPECT_NEAR(corner.y, 4.0F + 0.5F * 0.70710678F, 0.002F);
}

namespace {
	// Positions in a ring: an eighth-metre grid (which lands on edges and on whole-metre vertices) and the
	// 5 by 5 millimetre neighbourhood of every vertex.
	std::vector<glm::vec2> positionsInside(const FootprintBounds& footprint) {
		std::vector<glm::vec2> positions;
		const auto			   keep = [&](glm::vec2 candidate) {
			  if (footprint.contains(candidate)) {
				  positions.push_back(candidate);
			  }
		};
		for (float y = footprint.min.y; y <= footprint.max.y; y += 0.125F) {
			for (float x = footprint.min.x; x <= footprint.max.x; x += 0.125F) {
				keep({x, y});
			}
		}
		for (const geometry::Vec2i64& vertex : footprint.ring) {
			for (std::int64_t offsetY = -2; offsetY <= 2; ++offsetY) {
				for (std::int64_t offsetX = -2; offsetX <= 2; ++offsetX) {
					const auto meters = geometry::dequantize({vertex.x + offsetX, vertex.y + offsetY});
					keep({meters.x, meters.y});
				}
			}
		}
		return positions;
	}

	double metersToBoundary(const FootprintBounds& footprint, glm::vec2 position) {
		const geometry::Vec2i64 point = geometry::quantize({position.x, position.y});
		double					nearestMm = std::numeric_limits<double>::max();
		for (std::size_t index = 0; index < footprint.ring.size(); ++index) {
			const auto nearest = geometry::closestOnSegment(point, footprint.ring[index], footprint.ring[(index + 1) % footprint.ring.size()]);
			nearestMm = std::min(nearestMm, nearest.distanceMm);
		}
		return nearestMm / static_cast<double>(geometry::kMillimetersPerMeter);
	}
} // namespace

// Wherever in a ring a thing lies, it is set down outside the ring, no farther than the boundary plus the margin.
TEST(FootprintBoundsTests, NearestOutsideIsOutsideAndNearForEveryPositionInEveryShape) {
	const std::vector<std::pair<const char*, geometry::Ring>> shapes{
		{"L", {{0, 0}, {8000, 0}, {8000, 4000}, {4000, 4000}, {4000, 8000}, {0, 8000}}},
		{"U", {{0, 0}, {9000, 0}, {9000, 8000}, {6000, 8000}, {6000, 3000}, {3000, 3000}, {3000, 8000}, {0, 8000}}},
		{"T", {{3000, 0}, {6000, 0}, {6000, 5000}, {9000, 5000}, {9000, 8000}, {0, 8000}, {0, 5000}, {3000, 5000}}},
		{"plus", {{3000, 0}, {6000, 0}, {6000, 3000}, {9000, 3000}, {9000, 6000}, {6000, 6000}, {6000, 9000}, {3000, 9000}, {3000, 6000}, {0, 6000}, {0, 3000}, {3000, 3000}}},
	};
	std::size_t checked = 0;
	for (const auto& [name, ring] : shapes) {
		const FootprintBounds footprint{ring};
		const auto			  positions = positionsInside(footprint);
		ASSERT_GT(positions.size(), 1000U) << name;
		for (const float margin : {0.1F, 0.5F, 1.0F}) {
			for (const glm::vec2 from : positions) {
				const glm::vec2 out = footprint.nearestOutside(from, margin);
				++checked;
				ASSERT_FALSE(footprint.contains(out)) << name << " margin " << margin << " from (" << from.x << ", " << from.y << ") at (" << out.x << ", " << out.y << ")";
				const double limit = metersToBoundary(footprint, from) + static_cast<double>(margin) + 0.01;
				ASSERT_LE(static_cast<double>(glm::distance(out, from)), limit)
					<< name << " margin " << margin << " from (" << from.x << ", " << from.y << ") at (" << out.x << ", " << out.y << ")";
			}
		}
	}
	EXPECT_GT(checked, 10000U);
}

// A slot narrower than the margin leaves no spot beside the nearest edge; every position still ends up outside.
TEST(FootprintBoundsTests, NearestOutsideIsOutsideForEveryPositionAroundASlotNarrowerThanTheMargin) {
	const FootprintBounds footprint{geometry::Ring{{0, 0}, {8400, 0}, {8400, 8000}, {4400, 8000}, {4400, 2000}, {4000, 2000}, {4000, 8000}, {0, 8000}}};
	for (const glm::vec2 from : positionsInside(footprint)) {
		const glm::vec2 out = footprint.nearestOutside(from, 1.0F);
		ASSERT_FALSE(footprint.contains(out)) << "from (" << from.x << ", " << from.y << ") at (" << out.x << ", " << out.y << ")";
	}
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
			auto& assets = assets::AssetRegistry::Get();
			assets.clearDefinitions();

			// Two distinct materials so the multi-material collision is exercised. Both must have
			// non-zero defNameIds or emitMaterialGoals skips them. Wood carries the real 2.5 kg
			// mass so the per-trip harvest bound (carryCapacityKg / mass) is the game's 14, and a
			// 40 stack size so a colonist's backpack holds more than one trip's worth.
			for (const char* name : {"Wood", "Stone"}) {
				assets::AssetDefinition def;
				def.defName = name;
				def.label = name;
				assets::ItemProperties item;
				item.massKg = std::string(name) == "Wood" ? 2.5F : 1.0F;
				item.stackSize = 40;
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
			assets::AssetRegistry::Get().clearDefinitions();
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

		/// A colonist carrying `woodCarried` Wood in its backpack (spilled across 40-unit stacks).
		/// NeedsComponent marks it a colonist like a real one.
		EntityID createColonistCarryingWood(uint32_t woodCarried) {
			auto entity = world->createEntity();
			world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
			auto inv = Inventory::createForColonist();
			inv.addItem("Wood", woodCarried); // 10 slots x 40 stack holds well over one trip's worth
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

	// Two Harvest children, both parented to the umbrella, coexisting; the umbrella accepts both
	// materials (carry-in from possession delivers them, so there are no Haul children).
	auto children = registry.getChildGoals(umbrella->id);
	EXPECT_EQ(countOfType(children, TaskType::Harvest), 2U);
	EXPECT_EQ(countOfType(children, TaskType::Haul), 0U);
	EXPECT_EQ(umbrella->acceptedDefNameIds.size(), 2U);
	for (const auto* child : children) {
		ASSERT_TRUE(child->parentGoalId.has_value());
		EXPECT_EQ(child->parentGoalId.value(), umbrella->id);
		EXPECT_EQ(child->destinationEntity, foundation);
	}

	// Whole owned set: 1 umbrella + 2 children.
	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 3U);

	const uint64_t umbrellaId = umbrella->id;

	// A SECOND refresh must NOT clobber any of them (the original collision bug). The umbrella id
	// is stable, and both materials still have their Harvest child.
	refresh();

	const auto* umbrella2 = registry.getGoalByDestination(foundation);
	ASSERT_NE(umbrella2, nullptr);
	EXPECT_EQ(umbrella2->id, umbrellaId) << "umbrella id must stay stable across refreshes";

	auto children2 = registry.getChildGoals(umbrellaId);
	EXPECT_EQ(countOfType(children2, TaskType::Harvest), 2U) << "both Harvest children must survive";
	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 3U);
}

// A foundation reshaped in place drops its goal tree; the next tick rebuilds it at the
// entity's new position and against its new manifest.
TEST_F(ConstructionGoalEmissionTest, ResetBlueprintGoalsRebuildsTheTreeFromTheNewFootprint) {
	auto  foundation = createTwoMaterialFoundation();
	auto  other = createLargeWoodFoundation(10, {40.0F, 40.0F});
	auto& registry = GoalTaskRegistry::Get();
	refresh();
	ASSERT_NE(registry.getGoalByDestination(foundation), nullptr);
	const std::size_t otherGoals = registry.getChildGoals(registry.getGoalByDestination(other)->id).size() + 1;

	construction->resetBlueprintGoals(foundation);
	EXPECT_EQ(registry.getGoalByDestination(foundation), nullptr);
	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), otherGoals) << "only the reset blueprint's goals go";

	// The resize moved the site and dropped Stone from the manifest.
	world->getComponent<Position>(foundation)->value = {12.0F, 3.0F};
	world->getComponent<StructureBlueprint>(foundation)->required = {{"Wood", 12}};
	refresh();

	const auto* umbrella = registry.getGoalByDestination(foundation);
	ASSERT_NE(umbrella, nullptr);
	EXPECT_EQ(umbrella->destinationPosition, glm::vec2(12.0F, 3.0F));
	auto children = registry.getChildGoals(umbrella->id);
	EXPECT_EQ(countOfType(children, TaskType::Harvest), 1U);
	for (const auto* child : children) {
		EXPECT_EQ(child->destinationPosition, glm::vec2(12.0F, 3.0F));
	}
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
	ASSERT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 3U);

	// Flip the blueprint to Complete: the next refresh leaves it in the stale set and the cleanup
	// pass drops the umbrella and every child via removeGoalWithChildren.
	world->getComponent<StructureBlueprint>(foundation)->phase = StructureBlueprint::BuildPhase::Complete;
	refresh();

	EXPECT_EQ(registry.goalCount(GoalOwner::ConstructionGoalSystem), 0U);
	EXPECT_EQ(registry.getGoalByDestination(foundation), nullptr);
}

// A manifest larger than one carry trip: the umbrella accepts the whole shortfall and its Harvest
// child stays open however much a colonist carries. What is carried is delivered by carry-in from
// possession, and how much more to cut is the root's remaining need net of in-flight, so the goal
// graph no longer guesses at carried stock (the old per-trip harvest demand and its stall).
TEST_F(ConstructionGoalEmissionTest, LargeManifestAcceptsTheWholeShortfallWhateverIsCarried) {
	auto foundation = createLargeWoodFoundation(/*woodNeeded=*/313);
	createColonistCarryingWood(/*woodCarried=*/200);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* umbrella = registry.getGoalByDestination(foundation);
	ASSERT_NE(umbrella, nullptr);
	ASSERT_NE(findChild(umbrella->id, TaskType::Harvest), nullptr);
	EXPECT_EQ(findChild(umbrella->id, TaskType::Haul), nullptr);
	EXPECT_EQ(registry.remainingNeed(*world, umbrella->id, assets::AssetRegistry::Get().getDefNameId("Wood"), NeedBasis::Stock), 313U);
}

// A LARGE single-material foundation (200 Wood, far more than one 40-stack) must record its
// whole delivery onto delivered[] and COMPLETE its material gate, reaching UnderConstruction.
// delivered[] is the authoritative tracker now (no on-site Inventory); recordDelivery is the
// single accumulation path a real Deposit drives.
TEST_F(ConstructionGoalEmissionTest, LargeFoundationAccumulatesWoodAndBuilds) {
	auto foundation = createLargeWoodFoundation(/*woodNeeded=*/200);

	// Record all 200 Wood directly onto the manifest, as a real haul deposit would (one trip's
	// worth at a time sums to the same total). The whole 200 is required, so it all records.
	StructureBlueprint* bp = world->getComponent<StructureBlueprint>(foundation);
	ASSERT_NE(bp, nullptr);
	const uint32_t recorded = bp->recordDelivery("Wood", 200);
	ASSERT_EQ(recorded, 200U) << "the whole 200 is required, so the whole delivery records";
	EXPECT_EQ(bp->remaining("Wood"), 0U) << "manifest satisfied";

	refresh();

	// Materials complete -> the blueprint advanced to UnderConstruction (the build gate opened).
	bp = world->getComponent<StructureBlueprint>(foundation);
	ASSERT_NE(bp, nullptr);
	EXPECT_TRUE(bp->materialsComplete()) << "200 delivered >= 200 required";
	EXPECT_EQ(bp->phase, StructureBlueprint::BuildPhase::UnderConstruction)
		<< "a large foundation must leave AwaitingMaterials once its wood is staged";
}

// Cancelling a partially-delivered blueprint (no build work done) dumps the materials already
// hauled to the site to the ground via the drop callback, then clears delivered[]. This is the
// no-work deconstruct branch: the materials are loose at the site, not baked into a built structure.
TEST_F(ConstructionGoalEmissionTest, CancelledPartiallyDeliveredSiteDumpsDeliveredToGround) {
	auto foundation = createLargeWoodFoundation(/*woodNeeded=*/200);

	// Stage a partial delivery (50 of 200) onto the manifest, as in-flight hauls would have, then
	// cancel: workDone is still 0, so this takes the no-work removal path that dumps + clears.
	auto* bp = world->getComponent<StructureBlueprint>(foundation);
	ASSERT_NE(bp, nullptr);
	ASSERT_EQ(bp->recordDelivery("Wood", 50), 50U);
	bp->demolishing = true;

	// Capture ground-pile drops the system requests, and accept the no-work removal so the branch
	// runs to completion (a null deconstructed callback would only warn).
	std::vector<std::tuple<std::string, float, float, uint32_t>> drops;
	construction->setDropResourceCallback([&](const std::string& defName, float x, float y, uint32_t qty, glm::vec2) {
		drops.emplace_back(defName, x, y, qty);
	});
	bool removed = false;
	construction->setStructureDeconstructedCallback([&](EntityID e) {
		if (e == foundation) {
			removed = true;
		}
	});

	refresh();

	// The 50 staged Wood was dumped at the site (one call per material; the GameScene helper splits
	// it into stacks), and the removal callback fired.
	ASSERT_EQ(drops.size(), 1U) << "one drop call for the single staged material";
	EXPECT_EQ(std::get<0>(drops.front()), "Wood");
	EXPECT_EQ(std::get<3>(drops.front()), 50U) << "the whole staged quantity is dumped";
	EXPECT_TRUE(removed) << "the no-work blueprint was removed";

	// delivered[] is cleared so the manifest never re-dumps on a later tick.
	bp = world->getComponent<StructureBlueprint>(foundation);
	ASSERT_NE(bp, nullptr);
	EXPECT_TRUE(bp->delivered.empty()) << "delivered[] cleared after the dump";
}

// A two-material site completes its material gate only once BOTH manifests are filled. Recording a
// partial Wood delivery leaves it incomplete; recording the Stone plus the rest of the Wood fills
// it, and the next refresh advances the blueprint out of AwaitingMaterials into UnderConstruction.
// recordDelivery is the single accumulation path a real deposit drives, one trip at a time.
TEST_F(ConstructionGoalEmissionTest, MultiMaterialSiteCompletesViaDeposits) {
	auto foundation = createTwoMaterialFoundation(); // requires Wood 30, Stone 20

	auto* bp = world->getComponent<StructureBlueprint>(foundation);
	ASSERT_NE(bp, nullptr);

	// Partial Wood only: the Stone manifest is untouched and Wood is short, so the gate is open.
	ASSERT_EQ(bp->recordDelivery("Wood", 18), 18U);
	EXPECT_FALSE(bp->materialsComplete()) << "Wood still short and no Stone delivered yet";
	EXPECT_GT(bp->remaining("Wood"), 0U);
	EXPECT_EQ(bp->remaining("Stone"), 20U);

	// Deliver all the Stone and the rest of the Wood: every manifest is now satisfied.
	ASSERT_EQ(bp->recordDelivery("Stone", 20), 20U);
	ASSERT_EQ(bp->recordDelivery("Wood", 12), 12U) << "12 finishes the 30 Wood (18 + 12)";
	EXPECT_TRUE(bp->materialsComplete()) << "both Wood (30/30) and Stone (20/20) delivered";

	refresh();

	// Materials complete + clear footprint -> the blueprint leaves AwaitingMaterials for
	// UnderConstruction (the build gate opens).
	bp = world->getComponent<StructureBlueprint>(foundation);
	ASSERT_NE(bp, nullptr);
	EXPECT_EQ(bp->phase, StructureBlueprint::BuildPhase::UnderConstruction)
		<< "a multi-material site advances to UnderConstruction once every manifest is filled";
}

// Demolishing a BUILT structure must NOT drop its salvage at the demolish-order tick: the structure
// is still standing then, and its tear-down is work a colonist drives down. The worked branch leaves
// delivered[] intact (no drop, no clear) so the deconstructed-COMPLETION hook (GameScene, fired when
// the work finishes and the structure is removed) can read it and dump refundPercent% then. This
// fixture has no ActionSystem/colonist to drive the work to completion, so the completion-side drop
// is proved at the BuildAction layer (DeconstructSalvagesRefundPercentAtCompletion); here we pin the
// engine half: no order-tick drop, delivered[] preserved for the hook.
TEST_F(ConstructionGoalEmissionTest, BuiltStructureDemolishDoesNotDropSalvageAtOrderTick) {
	auto foundation = createBuiltDemolishing(StructureKind::Foundation); // built, delivered Wood 30

	std::vector<std::tuple<std::string, float, float, uint32_t>> drops;
	construction->setDropResourceCallback([&](const std::string& defName, float x, float y, uint32_t qty, glm::vec2) {
		drops.emplace_back(defName, x, y, qty);
	});

	refresh();

	// No drop at the demolish-order tick (the salvage waits for tear-down completion), even across the
	// per-tick Deconstruct refreshes while the goal sits Available.
	EXPECT_TRUE(drops.empty()) << "a built structure salvages on tear-down completion, NOT at the order tick";

	// delivered[] is left intact so the completion hook can read the manifest and salvage from it.
	const auto* bp = world->getComponent<StructureBlueprint>(foundation);
	ASSERT_NE(bp, nullptr);
	ASSERT_EQ(bp->delivered.size(), 1U) << "delivered[] preserved for the completion hook";
	EXPECT_EQ(bp->delivered.front().first, "Wood");
	EXPECT_EQ(bp->delivered.front().second, 30U) << "the full manifest is still recorded";

	// A worked/built teardown still emits a Deconstruct goal for a colonist to work down.
	const auto* goal = GoalTaskRegistry::Get().getGoalByDestination(foundation);
	ASSERT_NE(goal, nullptr);
	EXPECT_EQ(goal->type, TaskType::Deconstruct) << "the built structure is torn down by work, salvage aside";
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
}

TEST_F(ConstructionGoalEmissionTest, OpeningNeverEmitsClearGoals) {
	// An opening sits on a built wall: it has no clear phase, so even on first sight it
	// emits no Harvest CLEAR goal. (Its only Harvest is the per-material one.)
	auto  opening = createOpeningBlueprint(/*openingId=*/1);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* umbrella = registry.getGoalByDestination(opening);
	ASSERT_NE(umbrella, nullptr);
	for (const auto* child : registry.getChildGoals(umbrella->id)) {
		if (child->type == TaskType::Harvest) {
			EXPECT_FALSE(child->clearFootprint.has_value()) << "an opening's only Harvest is a material one, never a clear goal";
		}
	}
}

// ============================================================================
// Opening host-segment gate, driven through a real ConstructionWorld. The gate
// holds the opening at a Blocked umbrella with NO haul/build goals while its host
// segment is Blueprint, and releases it (haul, then build) once the segment is Built.
// ============================================================================

namespace {
	namespace cw = construction;

	class OpeningHostGateTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			GoalTaskRegistry::Get().clear();
			auto& assets = assets::AssetRegistry::Get();
			assets.clearDefinitions();
			assets::AssetDefinition def;
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
			assets::AssetRegistry::Get().clearDefinitions();
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

	// Materials are due: the umbrella accepts them and a Harvest child per material is open.
	const auto* umbrella = registry.getGoalByDestination(blueprint);
	ASSERT_NE(umbrella, nullptr);
	auto children = registry.getChildGoals(umbrella->id);
	EXPECT_EQ(countOfType(children, TaskType::Harvest), 1U);
	EXPECT_FALSE(umbrella->acceptedDefNameIds.empty());

	// Record the materials onto the blueprint's manifest, then refresh: the opening advances
	// to UnderConstruction and the umbrella flips Available (the Build goal).
	world->getComponent<StructureBlueprint>(blueprint)->recordDelivery("Wood", 10);
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

TEST_F(OpeningHostGateTest, DemolishingFoundationGatedWhileExtensionPending) {
	geometry::Ring	 ring{{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}};
	cw::CommitResult target = topology.commitFoundation(std::move(ring), "wood");
	ASSERT_TRUE(target.ok());
	ASSERT_TRUE(topology.setState(target.id, cw::FoundationState::Built));
	cw::CommitResult extension = topology.commitExtension(target.id, {{4000, 0}, {7000, 0}, {7000, 4000}, {4000, 4000}});
	ASSERT_TRUE(extension.ok());

	auto  targetBp = createBuiltDemolishing(StructureKind::Foundation, target.id);
	auto& registry = GoalTaskRegistry::Get();

	refresh();

	const auto* goal = registry.getGoalByDestination(targetBp);
	ASSERT_NE(goal, nullptr);
	EXPECT_EQ(goal->type, TaskType::Deconstruct);
	EXPECT_EQ(goal->status, GoalStatus::Blocked) << "target must wait for its pending extension";

	topology.removeFoundation(extension.id);
	refresh();

	const auto* goal2 = registry.getGoalByDestination(targetBp);
	ASSERT_NE(goal2, nullptr);
	EXPECT_EQ(goal2->status, GoalStatus::Available) << "extension gone -> target Deconstruct Available";
}
