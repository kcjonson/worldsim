// Tests for Build and Deconstruct construction actions in ActionSystem.
// Covers the pure work-rate helper, skill-scaled workDone advancement, clamping at the work
// bound, phase transition to Complete, progress() reflection, and the completion callbacks.

#include "ActionSystem.h"

#include "../ShippedConfig.test.h"
#include "../World.h"
#include "../components/Action.h"
#include "../components/AgentRadius.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/PlayerControlled.h"
#include "../components/Skills.h"
#include "../components/Structure.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <assets/ConstructionRegistry.h>
#include <construction/ConstructionWorld.h>
#include <construction/WallSides.h>

#include <gtest/gtest.h>

#include <filesystem>

namespace ecs::test {

	constexpr uint32_t kTestRngSeed = 42;

	// =============================================================================
	// constructionWorkRate (pure helper)
	// =============================================================================

	TEST(ConstructionWorkRateTest, UntrainedBuilderStillWorks) {
		// A level-0 colonist makes progress at the base rate (not zero).
		EXPECT_FLOAT_EQ(constructionWorkRate(0.0F), 10.0F);
	}

	TEST(ConstructionWorkRateTest, RateScalesWithSkill) {
		// Higher skill builds faster: base * (1 + 0.08 * level).
		EXPECT_FLOAT_EQ(constructionWorkRate(10.0F), 10.0F * 1.8F);
		EXPECT_FLOAT_EQ(constructionWorkRate(20.0F), 10.0F * 2.6F);
	}

	TEST(ConstructionWorkRateTest, MidSkillFasterThanUntrained) {
		EXPECT_GT(constructionWorkRate(10.0F), constructionWorkRate(0.0F));
		EXPECT_GT(constructionWorkRate(20.0F), constructionWorkRate(10.0F));
	}

	TEST(ConstructionWorkRateTest, SkillClampedToValidRange) {
		// Out-of-range skill is clamped, never producing a negative or runaway rate.
		EXPECT_FLOAT_EQ(constructionWorkRate(-5.0F), constructionWorkRate(0.0F));
		EXPECT_FLOAT_EQ(constructionWorkRate(99.0F), constructionWorkRate(20.0F));
	}

	// =============================================================================
	// Action factories
	// =============================================================================

	TEST(BuildActionFactoryTest, BuildActionCreation) {
		auto action = Action::Build(42, {3.0F, 4.0F}, 5.0F);

		EXPECT_EQ(action.type, ActionType::Build);
		EXPECT_EQ(action.state, ActionState::Starting);
		EXPECT_FALSE(action.interruptable);
		EXPECT_FLOAT_EQ(action.targetPosition.x, 3.0F);
		EXPECT_FLOAT_EQ(action.targetPosition.y, 4.0F);

		ASSERT_TRUE(action.hasProgressEffect());
		const auto& eff = action.progressEffect();
		EXPECT_EQ(eff.targetEntityId, 42U);
		EXPECT_FLOAT_EQ(eff.skillLevel, 5.0F);
		EXPECT_FALSE(eff.deconstruct);
	}

	TEST(BuildActionFactoryTest, DeconstructActionCreation) {
		auto action = Action::Deconstruct(7, {1.0F, 2.0F}, 12.0F);

		EXPECT_EQ(action.type, ActionType::Deconstruct);
		ASSERT_TRUE(action.hasProgressEffect());
		const auto& eff = action.progressEffect();
		EXPECT_EQ(eff.targetEntityId, 7U);
		EXPECT_FLOAT_EQ(eff.skillLevel, 12.0F);
		EXPECT_TRUE(eff.deconstruct);
	}

	// =============================================================================
	// Integration: Build/Deconstruct through ActionSystem
	// =============================================================================

	class BuildActionSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			world = std::make_unique<World>();
			actionSystem = &world->registerSystem<ActionSystem>(kTestRngSeed);
		}

		void TearDown() override { world.reset(); }

		/// Create a builder colonist with the components ActionSystem's view requires, plus a
		/// Construction skill at the given level.
		EntityID createBuilder(float constructionSkill, glm::vec2 position = {0.0F, 0.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{position});
			world->addComponent<NeedsComponent>(entity, ecs::testing::shippedNeeds());
			world->addComponent<Memory>(entity, Memory{});
			world->addComponent<Inventory>(entity, Inventory::createForColonist());
			world->addComponent<Task>(entity, Task{});
			world->addComponent<Action>(entity, Action{});
			Skills skills;
			skills.setLevel("Construction", constructionSkill);
			world->addComponent<Skills>(entity, skills);
			return entity;
		}

		/// Create a blueprint entity ready to build (UnderConstruction) with the given workTotal.
		EntityID createBlueprint(float workTotal, glm::vec2 position = {0.0F, 0.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{position});
			StructureBlueprint bp;
			bp.phase = StructureBlueprint::BuildPhase::UnderConstruction;
			bp.workTotal = workTotal;
			bp.workDone = 0.0F;
			world->addComponent<StructureBlueprint>(entity, bp);
			return entity;
		}

		/// Assign a Build task on the given blueprint to a builder, arrived at the work slot.
		void assignBuildTask(EntityID builder, EntityID blueprint) {
			auto* task = world->getComponent<Task>(builder);
			task->type = TaskType::Build;
			task->state = TaskState::Arrived;
			task->buildBlueprintEntityId = blueprint;
			task->targetPosition = {0.0F, 0.0F};
		}

		void assignDeconstructTask(EntityID builder, EntityID blueprint) {
			auto* task = world->getComponent<Task>(builder);
			task->type = TaskType::Deconstruct;
			task->state = TaskState::Arrived;
			task->buildBlueprintEntityId = blueprint;
			task->targetPosition = {0.0F, 0.0F};
		}

		std::unique_ptr<World> world;
		ActionSystem*		   actionSystem = nullptr;
	};

	TEST_F(BuildActionSystemTest, StartsBuildActionOnArrival) {
		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(100.0F);
		assignBuildTask(builder, blueprint);

		world->update(0.1F);

		auto* action = world->getComponent<Action>(builder);
		EXPECT_EQ(action->type, ActionType::Build);
		EXPECT_TRUE(action->hasProgressEffect());
	}

	TEST_F(BuildActionSystemTest, WorkAdvancesAtBaseRateForUntrained) {
		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(1000.0F);
		assignBuildTask(builder, blueprint);

		// One 1.0s tick at base rate 10/s advances workDone by 10.
		world->update(1.0F);

		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		EXPECT_FLOAT_EQ(bp->workDone, constructionWorkRate(0.0F) * 1.0F);
		EXPECT_FLOAT_EQ(bp->progress(), bp->workDone / 1000.0F);
	}

	TEST_F(BuildActionSystemTest, HigherSkillAdvancesFaster) {
		auto blueprintLow = createBlueprint(10000.0F);
		auto blueprintHigh = createBlueprint(10000.0F);
		auto novice = createBuilder(0.0F);
		auto master = createBuilder(20.0F);
		assignBuildTask(novice, blueprintLow);
		assignBuildTask(master, blueprintHigh);

		world->update(1.0F);

		auto* low = world->getComponent<StructureBlueprint>(blueprintLow);
		auto* high = world->getComponent<StructureBlueprint>(blueprintHigh);
		EXPECT_GT(high->workDone, low->workDone);
		EXPECT_FLOAT_EQ(low->workDone, constructionWorkRate(0.0F));
		EXPECT_FLOAT_EQ(high->workDone, constructionWorkRate(20.0F));
	}

	TEST_F(BuildActionSystemTest, WorkClampsAtWorkTotalAndCompletes) {
		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(15.0F); // base rate 10/s -> done after ~1.5s
		assignBuildTask(builder, blueprint);

		world->update(0.1F); // start + first tick
		world->update(5.0F); // overshoot the work bound

		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		EXPECT_FLOAT_EQ(bp->workDone, 15.0F); // clamped, not overshot
		EXPECT_FLOAT_EQ(bp->progress(), 1.0F);
		EXPECT_EQ(bp->phase, StructureBlueprint::BuildPhase::Complete);
	}

	TEST_F(BuildActionSystemTest, MultipleSessionsAccumulate) {
		std::vector<ActionEvent> heard;
		actionSystem->addActionObserver([&heard](const ActionEvent& event) { heard.push_back(event); });
		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(1000.0F);
		assignBuildTask(builder, blueprint);
		world->update(1.0F);
		ASSERT_EQ(heard.size(), 1U);
		EXPECT_EQ(heard[0].moment, ActionMoment::Started);
		EXPECT_EQ(heard[0].type, ActionType::Build);
		EXPECT_EQ(heard[0].aboutDefinition, 0U) << "a build is about no kind of thing";

		float afterFirst = world->getComponent<StructureBlueprint>(blueprint)->workDone;

		// Simulate the colonist leaving and a fresh build session later: clear action/task,
		// re-assign, and tick again. workDone must accumulate, not reset.
		world->getComponent<Action>(builder)->clear();
		assignBuildTask(builder, blueprint);
		world->update(1.0F);

		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		EXPECT_GT(bp->workDone, afterFirst);
		EXPECT_FLOAT_EQ(bp->workDone, afterFirst * 2.0F);
		EXPECT_EQ(heard.size(), 1U) << "taking up a blueprint with work on it is no new start";
	}

	TEST_F(BuildActionSystemTest, FiresStructureCompletedCallbackOnce) {
		int		 callbackCount = 0;
		EntityID notified = 0;
		actionSystem->setStructureCompletedCallback([&](EntityID e) {
			++callbackCount;
			notified = e;
		});

		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(10.0F);
		assignBuildTask(builder, blueprint);

		world->update(0.1F);
		world->update(2.0F); // complete

		EXPECT_EQ(callbackCount, 1);
		EXPECT_EQ(notified, blueprint);
	}

	TEST_F(BuildActionSystemTest, CompletionCallbackFiresOnceWithTwoConcurrentBuilders) {
		// Two builders working the same blueprint. When the structure flips Complete, exactly ONE
		// completion callback must fire: the redundant builder that arrives after the phase already
		// flipped Complete is treated as a no-op, not a second completion (no duplicate toast / world
		// version bump).
		int		 callbackCount = 0;
		EntityID notified = 0;
		actionSystem->setStructureCompletedCallback([&](EntityID e) {
			++callbackCount;
			notified = e;
		});

		auto blueprint = createBlueprint(10.0F); // base rate 10/s
		auto builderA = createBuilder(0.0F);
		auto builderB = createBuilder(0.0F);
		assignBuildTask(builderA, blueprint);
		assignBuildTask(builderB, blueprint);

		world->update(0.1F); // both start + first tick
		world->update(2.0F); // both overshoot the bound on the same update

		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		EXPECT_EQ(bp->phase, StructureBlueprint::BuildPhase::Complete);
		EXPECT_FLOAT_EQ(bp->workDone, 10.0F); // clamped, not double-overshot
		EXPECT_EQ(callbackCount, 1) << "redundant second builder must not re-fire completion";
		EXPECT_EQ(notified, blueprint);

		// A further tick with both still assigned (the redundant-builder path) must not fire again.
		assignBuildTask(builderA, blueprint);
		assignBuildTask(builderB, blueprint);
		world->update(1.0F);
		EXPECT_EQ(callbackCount, 1) << "no completion callback once already Complete";
	}

	TEST_F(BuildActionSystemTest, DoesNotStartBuildWhenNotUnderConstruction) {
		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(100.0F);
		world->getComponent<StructureBlueprint>(blueprint)->phase = StructureBlueprint::BuildPhase::AwaitingMaterials;
		assignBuildTask(builder, blueprint);

		world->update(0.5F);

		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		EXPECT_FLOAT_EQ(bp->workDone, 0.0F); // no progress on a not-ready blueprint
	}

	TEST_F(BuildActionSystemTest, BuilderMidActionStopsWhenSiteOrderedDemolished) {
		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(1000.0F);
		assignBuildTask(builder, blueprint);
		const Inventory inventoryBefore = *world->getComponent<Inventory>(builder);

		world->update(1.0F);
		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		const float workAtOrder = bp->workDone;
		ASSERT_GT(workAtOrder, 0.0F);
		ASSERT_EQ(world->getComponent<Action>(builder)->type, ActionType::Build);

		bp->demolishing = true;
		world->update(1.0F);

		EXPECT_FLOAT_EQ(bp->workDone, workAtOrder) << "no build work once the site is ordered demolished";
		EXPECT_FALSE(world->getComponent<Action>(builder)->isActive());

		world->update(1.0F);
		EXPECT_FLOAT_EQ(bp->workDone, workAtOrder);
		EXPECT_FALSE(world->getComponent<Task>(builder)->isActive()) << "the build task ends so the colonist decides again";
		EXPECT_EQ(world->getComponent<Inventory>(builder)->items.size(), inventoryBefore.items.size());
	}

	TEST_F(BuildActionSystemTest, DemolishOrderInCompletingTickPreventsCompletion) {
		int callbackCount = 0;
		actionSystem->setStructureCompletedCallback([&](EntityID) { ++callbackCount; });

		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(15.0F);
		assignBuildTask(builder, blueprint);
		world->update(1.0F); // 10 of 15

		// The order lands before the tick whose work would reach the bound.
		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		bp->demolishing = true;
		world->update(1.0F);

		EXPECT_FLOAT_EQ(bp->workDone, 10.0F);
		EXPECT_EQ(bp->phase, StructureBlueprint::BuildPhase::UnderConstruction);
		EXPECT_EQ(callbackCount, 0) << "a site ordered demolished never completes";
	}

	TEST_F(BuildActionSystemTest, WalkingBuilderDoesNotStartOnDemolishedSite) {
		auto builder = createBuilder(0.0F, {5.0F, 0.0F});
		auto blueprint = createBlueprint(1000.0F);
		assignBuildTask(builder, blueprint);
		world->getComponent<Task>(builder)->state = TaskState::Moving;
		world->update(0.1F);
		ASSERT_FALSE(world->getComponent<Action>(builder)->isActive()) << "still walking";

		world->getComponent<StructureBlueprint>(blueprint)->demolishing = true;
		world->getComponent<Position>(builder)->value = {0.0F, 0.0F};
		world->update(1.0F);

		EXPECT_FALSE(world->getComponent<Action>(builder)->isActive());
		EXPECT_FALSE(world->getComponent<Task>(builder)->isActive()) << "arriving ends the build task";
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(blueprint)->workDone, 0.0F);
	}

	TEST_F(BuildActionSystemTest, DeconstructCountsWorkDownAndCompletes) {
		int		 callbackCount = 0;
		EntityID notified = 0;
		actionSystem->setStructureDeconstructedCallback([&](EntityID e) {
			++callbackCount;
			notified = e;
		});

		auto  builder = createBuilder(0.0F);
		auto  blueprint = createBlueprint(20.0F);
		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		bp->phase = StructureBlueprint::BuildPhase::Complete;
		bp->workDone = 20.0F; // a fully built structure being torn down
		assignDeconstructTask(builder, blueprint);

		world->update(1.0F); // 10 units removed
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(blueprint)->workDone, 10.0F);

		world->update(5.0F); // finish teardown
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(blueprint)->workDone, 0.0F);
		EXPECT_EQ(callbackCount, 1);
		EXPECT_EQ(notified, blueprint);
	}

	// The first colonist to tear down a half-built site starts the teardown, though the build was never
	// finished; one taking the teardown up again does not, and the last of it finishes once.
	TEST_F(BuildActionSystemTest, FreshDeconstructOfHalfBuiltSiteStartsOnce) {
		std::vector<ActionEvent> heard;
		actionSystem->addActionObserver([&heard](const ActionEvent& event) { heard.push_back(event); });
		auto  builder = createBuilder(0.0F);
		auto  blueprint = createBlueprint(100.0F);
		auto* halfBuilt = world->getComponent<StructureBlueprint>(blueprint);
		halfBuilt->workDone = 50.0F;
		halfBuilt->demolishing = true;
		assignDeconstructTask(builder, blueprint);

		world->update(1.0F);
		ASSERT_EQ(heard.size(), 1U);
		EXPECT_EQ(heard[0].type, ActionType::Deconstruct);
		EXPECT_EQ(heard[0].moment, ActionMoment::Started);

		world->getComponent<Action>(builder)->clear();
		assignDeconstructTask(builder, blueprint);
		world->update(1.0F);
		EXPECT_EQ(heard.size(), 1U) << "resuming the teardown is no new start";

		world->update(10.0F);
		ASSERT_EQ(heard.size(), 2U);
		EXPECT_EQ(heard[1].moment, ActionMoment::Finished);
	}

	// A BUILT structure's salvage is dropped at tear-down COMPLETION, not at the demolish-order
	// tick. The drop itself is GameScene's job (dropResourcePiles lives there), but it reads the
	// blueprint's delivered[] inside the structure-deconstructed callback. This test pins the
	// engine-side contract that makes that possible: when the Deconstruct WORK completes, the
	// callback fires exactly once and delivered[] is still intact at that moment, so refundPercent%
	// of the manifest is computable THEN (the salvage timing the GameScene hook depends on). No
	// salvage is computed mid-teardown -- only once, at completion.
	TEST_F(BuildActionSystemTest, DeconstructSalvagesRefundPercentAtCompletion) {
		constexpr float kRefundPercent = 50.0F;

		// Model the GameScene salvage: read delivered[] in the callback and compute refundPercent%.
		std::vector<std::pair<std::string, uint32_t>> salvaged;
		int											callbackCount = 0;
		actionSystem->setStructureDeconstructedCallback([&](EntityID e) {
			++callbackCount;
			const auto* bp = world->getComponent<StructureBlueprint>(e);
			ASSERT_NE(bp, nullptr) << "the entity is still alive when the callback fires";
			for (const auto& [defName, qty] : bp->delivered) {
				const auto salvageQty =
					static_cast<uint32_t>(static_cast<float>(qty) * kRefundPercent / 100.0F);
				if (salvageQty > 0) {
					salvaged.emplace_back(defName, salvageQty);
				}
			}
		});

		auto  builder = createBuilder(0.0F);
		auto  blueprint = createBlueprint(20.0F);
		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		bp->phase = StructureBlueprint::BuildPhase::Complete;
		bp->workDone = 20.0F;				// a fully built structure being torn down
		bp->delivered = {{"Wood", 30}};		// built: delivered == required, the salvage source
		assignDeconstructTask(builder, blueprint);

		world->update(1.0F); // 10 of 20 removed: mid-teardown, NOT complete
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(blueprint)->workDone, 10.0F);
		EXPECT_EQ(callbackCount, 0) << "no salvage until the tear-down work completes";
		EXPECT_TRUE(salvaged.empty());

		world->update(5.0F); // finish teardown
		EXPECT_FLOAT_EQ(world->getComponent<StructureBlueprint>(blueprint)->workDone, 0.0F);

		// Completion fired the callback once; delivered[] was intact, so refundPercent 50 of 30
		// Wood = 15 salvaged THEN.
		EXPECT_EQ(callbackCount, 1) << "the salvage fires once, at tear-down completion";
		ASSERT_EQ(salvaged.size(), 1U) << "one salvaged material";
		EXPECT_EQ(salvaged.front().first, "Wood");
		EXPECT_EQ(salvaged.front().second, 15U) << "refundPercent 50 of 30 Wood = 15";
	}

	// A wall does not complete while a colonist the player controls stands in its band, nor while it would
	// shut in one of its builders or someone on the way out (building-construction: Walls / Lifecycle).
	class WallBandHoldTest : public BuildActionSystemTest {
	  protected:
		void SetUp() override {
			BuildActionSystemTest::SetUp();
			assets::ConstructionRegistry::Get().clear();
			std::filesystem::path folder = __FILE__; // libs/sim/ecs/systems/BuildAction.test.cpp
			folder = folder.parent_path().parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "construction";
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load(folder.string()));
			actionSystem->setConstructionWorld(&construction);
		}
		void TearDown() override {
			BuildActionSystemTest::TearDown();
			assets::ConstructionRegistry::Get().clear();
		}

		// A Standard Wood wall (0.2 m) from `from` to `to` in meters, built or a blueprint.
		construction::SegmentId wall(glm::vec2 from, glm::vec2 to, bool built) {
			const auto result = construction.commitSegment(
				{static_cast<int64_t>(from.x * 1000.0F), static_cast<int64_t>(from.y * 1000.0F)},
				{static_cast<int64_t>(to.x * 1000.0F), static_cast<int64_t>(to.y * 1000.0F)}, "Wood", "Standard",
				construction::kInvalidFoundation, construction.version()
			);
			EXPECT_TRUE(result.ok());
			if (built) {
				construction.setSegmentState(result.id, construction::FoundationState::Built);
			}
			return result.id;
		}

		// The wall blueprint entity of `segment`, its work almost done, worked by a builder at `builderAt`.
		EntityID wallSite(construction::SegmentId segment, glm::vec2 builderAt = {2.0F, -0.6F}) {
			const EntityID blueprint = createBlueprint(10.0F);
			world->getComponent<StructureBlueprint>(blueprint)->workDone = 9.0F;
			world->addComponent<Structure>(blueprint, Structure{.kind = StructureKind::Wall, .graphId = segment});
			builder = createBuilder(0.0F, builderAt);
			assignBuildTask(builder, blueprint);
			world->getComponent<Task>(builder)->targetPosition = builderAt;
			return blueprint;
		}

		// The three built walls of the square (0, 0) to (4, 4) other than its south side, the wall from
		// (0, 0) to (4, 0) that closes it.
		construction::SegmentId closingWallOfASquare() {
			wall({4.0F, 0.0F}, {4.0F, 4.0F}, true);
			wall({4.0F, 4.0F}, {0.0F, 4.0F}, true);
			wall({0.0F, 4.0F}, {0.0F, 0.0F}, true);
			return wall({0.0F, 0.0F}, {4.0F, 0.0F}, false);
		}

		EntityID colonistAt(glm::vec2 position, bool playerControlled) {
			const EntityID colonist = world->createEntity();
			world->addComponent<Position>(colonist, Position{position});
			world->addComponent<AgentRadius>(colonist, AgentRadius{});
			if (playerControlled) {
				world->addComponent<PlayerControlled>(colonist, PlayerControlled{});
			}
			return colonist;
		}

		[[nodiscard]] StructureBlueprint::BuildPhase phaseOf(EntityID blueprint) {
			return world->getComponent<StructureBlueprint>(blueprint)->phase;
		}

		construction::ConstructionWorld construction;
		EntityID						builder = kInvalidEntity;
	};

	// A player-controlled colonist on the wall's line: the work reaches its end and the wall waits for as
	// long as he stands there, saying so once, moving nobody, and leaving its builder free to go; it
	// completes once he steps off.
	TEST_F(WallBandHoldTest, AWallWaitsForAPlayerControlledColonistInItsBandAndCompletesWhenHeLeaves) {
		int					  completions = 0;
		std::vector<EntityID> heldBy;
		actionSystem->setStructureCompletedCallback([&](EntityID) { ++completions; });
		actionSystem->setWallHeldCallback([&](EntityID, EntityID colonist) { heldBy.push_back(colonist); });
		const EntityID blueprint = wallSite(wall({0.0F, 0.0F}, {4.0F, 0.0F}, false));
		const EntityID standing = colonistAt({2.0F, 0.35F}, true); // 0.35 m off: his disc reaches the 0.1 m half-thickness
		const EntityID crossing = colonistAt({1.0F, 0.1F}, false);

		for (int tick = 0; tick < 20; ++tick) {
			world->update(0.5F);
		}
		const auto* site = world->getComponent<StructureBlueprint>(blueprint);
		EXPECT_FLOAT_EQ(site->workDone, site->workTotal) << "the work is done";
		EXPECT_EQ(site->phase, StructureBlueprint::BuildPhase::UnderConstruction) << "the wall is not";
		EXPECT_EQ(completions, 0);
		EXPECT_EQ(heldBy, std::vector<EntityID>{standing}) << "the hold is told once, naming who holds it";
		EXPECT_EQ(world->getComponent<Position>(standing)->value, glm::vec2(2.0F, 0.35F)) << "a controlled colonist is the player's to move";
		EXPECT_EQ(world->getComponent<Position>(crossing)->value, glm::vec2(1.0F, 0.1F)) << "nobody is moved while the wall waits";
		EXPECT_TRUE(world->getComponent<Action>(builder)->interruptable) << "waiting is no work to commit to";

		world->getComponent<Position>(standing)->value = {2.0F, 0.45F};
		world->update(0.5F);
		EXPECT_EQ(site->phase, StructureBlueprint::BuildPhase::Complete);
		EXPECT_EQ(completions, 1);
		EXPECT_GT(world->getComponent<Position>(crossing)->value.y, 0.1F + 0.3F) << "the other colonist steps out, on his side, as it completes";
	}

	// Anyone else in the band steps out as the wall completes, to the outside of the room it closes.
	TEST_F(WallBandHoldTest, AColonistInTheBandOfAClosingWallStepsOutOfTheRoom) {
		const EntityID blueprint = wallSite(closingWallOfASquare());
		const EntityID inside = colonistAt({1.0F, 0.05F}, false); // on the room's side of the line
		const EntityID onLine = colonistAt({3.0F, 0.0F}, false);

		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::Complete);
		const auto band = construction::wallBand(construction, *construction.getSegment(world->getComponent<Structure>(blueprint)->graphId));
		for (const EntityID colonist : {inside, onLine}) {
			const glm::vec2 at = world->getComponent<Position>(colonist)->value;
			EXPECT_LT(at.y, 0.0F) << "outside the room, which lies at y > 0";
			EXPECT_FALSE(band->overlaps(at, AgentRadius{}.radiusMeters));
		}
		EXPECT_NEAR(world->getComponent<Position>(inside)->value.x, 1.0F, 1e-4F) << "level with where he stood";
	}

	// A wall that closes nothing: a colonist in its band keeps the side he is on, and one exactly on its
	// line steps out to one side, either way clear of the band.
	TEST_F(WallBandHoldTest, AColonistInTheBandOfAWallThatClosesNothingKeepsHisSide) {
		const EntityID blueprint = wallSite(wall({0.0F, 0.0F}, {4.0F, 0.0F}, false));
		const EntityID above = colonistAt({1.0F, 0.2F}, false);
		const EntityID below = colonistAt({2.0F, -0.1F}, false);
		const EntityID onLine = colonistAt({3.0F, 0.0F}, false);

		world->update(0.5F);
		ASSERT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::Complete);
		const auto band = construction::wallBand(construction, *construction.getSegment(world->getComponent<Structure>(blueprint)->graphId));
		EXPECT_GT(world->getComponent<Position>(above)->value.y, 0.0F);
		EXPECT_LT(world->getComponent<Position>(below)->value.y, 0.0F);
		for (const EntityID colonist : {above, below, onLine}) {
			EXPECT_FALSE(band->overlaps(world->getComponent<Position>(colonist)->value, AgentRadius{}.radiusMeters));
		}
	}

	// In a paused game no time passes: a wall whose hold has just ended does not complete, and nobody is
	// moved, until time runs again.
	TEST_F(WallBandHoldTest, NothingCompletesOrMovesOnATickInWhichNoTimePasses) {
		const EntityID blueprint = wallSite(wall({0.0F, 0.0F}, {4.0F, 0.0F}, false));
		const EntityID standing = colonistAt({2.0F, 0.35F}, true);
		const EntityID crossing = colonistAt({1.0F, 0.1F}, false);
		world->update(0.5F);
		world->update(0.5F);
		ASSERT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::UnderConstruction);

		world->getComponent<Position>(standing)->value = {2.0F, 2.0F};
		world->update(0.0F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::UnderConstruction);
		EXPECT_EQ(world->getComponent<Position>(crossing)->value, glm::vec2(1.0F, 0.1F));

		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::Complete);
	}

	// A builder the closing wall would shut in holds it: his work ends, so he decides again and goes to the
	// side it allows, and it completes once he works it from outside.
	TEST_F(WallBandHoldTest, AClosingWallWaitsForItsBuilderInsideTheRoom) {
		const EntityID blueprint = wallSite(closingWallOfASquare(), {2.0F, 0.6F});

		world->update(0.5F);
		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::UnderConstruction);
		EXPECT_FALSE(world->getComponent<Action>(builder)->isActive()) << "his work ends so he decides again";

		world->getComponent<Position>(builder)->value = {2.0F, -0.6F};
		world->getComponent<Task>(builder)->targetPosition = {2.0F, -0.6F};
		world->update(0.5F);
		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::Complete);
	}

	// Someone the closing wall would shut in who is on the way out (a task walking him outside) holds it
	// until he is out; someone staying inside (no task, or one whose point is inside) does not.
	TEST_F(WallBandHoldTest, AClosingWallWaitsForSomeoneOnTheWayOutButNotForSomeoneStaying) {
		const EntityID blueprint = wallSite(closingWallOfASquare());
		const EntityID idle = colonistAt({1.0F, 3.0F}, false);
		world->addComponent<Task>(idle, Task{});
		const EntityID eatingInside = colonistAt({3.0F, 3.0F}, false);
		Task eat;
		eat.type = TaskType::FulfillNeed;
		eat.state = TaskState::Arrived;
		eat.targetPosition = {3.0F, 3.0F};
		world->addComponent<Task>(eatingInside, eat);
		const EntityID leaving = colonistAt({2.0F, 1.0F}, false);
		Task walkOut;
		walkOut.type = TaskType::Wander;
		walkOut.state = TaskState::Moving;
		walkOut.targetPosition = {2.0F, -3.0F};
		world->addComponent<Task>(leaving, walkOut);

		world->update(0.5F);
		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::UnderConstruction) << "it waits for the one leaving";
		EXPECT_EQ(world->getComponent<Position>(leaving)->value, glm::vec2(2.0F, 1.0F)) << "nobody is moved while it waits";

		world->getComponent<Position>(leaving)->value = {2.0F, -1.0F};
		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::Complete) << "those staying inside are not waited for";
	}

	// Neither someone inside whose route out was found wanting, nor a colonist the player controls inside
	// (out of the band), is waited for.
	TEST_F(WallBandHoldTest, AClosingWallDoesNotWaitForAFailedRouteOrAControlledColonistInside) {
		const EntityID blueprint = wallSite(closingWallOfASquare());
		const EntityID stuck = colonistAt({1.0F, 2.0F}, false);
		Task walkOut;
		walkOut.type = TaskType::Wander;
		walkOut.state = TaskState::Moving;
		walkOut.navState = NavState::CantFindWayTo;
		walkOut.targetPosition = {2.0F, -3.0F};
		world->addComponent<Task>(stuck, walkOut);
		const EntityID controlled = colonistAt({3.0F, 2.0F}, true);
		walkOut.navState = NavState::Traveling;
		world->addComponent<Task>(controlled, walkOut);

		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::Complete);
	}

	// A builder of another wall, inside the room this one would close, holds it with his work left going:
	// that wall completes first and frees him.
	TEST_F(WallBandHoldTest, AClosingWallWaitsForABuilderOfAnotherWallInsideTheRoom) {
		const EntityID blueprint = wallSite(closingWallOfASquare());
		const EntityID otherWall = createBlueprint(1000.0F);
		world->addComponent<Structure>(otherWall, Structure{.kind = StructureKind::Wall, .graphId = wall({1.0F, 1.0F}, {1.0F, 3.0F}, false)});
		const EntityID otherBuilder = createBuilder(0.0F, {1.6F, 2.0F});
		assignBuildTask(otherBuilder, otherWall);
		world->getComponent<Task>(otherBuilder)->targetPosition = {1.6F, 2.0F};

		world->update(0.5F);
		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::UnderConstruction);
		EXPECT_TRUE(world->getComponent<Action>(otherBuilder)->isActive()) << "his own work goes on";

		world->getComponent<StructureBlueprint>(otherWall)->phase = StructureBlueprint::BuildPhase::Complete;
		world->getComponent<Task>(otherBuilder)->clear();
		world->update(0.5F);
		EXPECT_EQ(phaseOf(blueprint), StructureBlueprint::BuildPhase::Complete);
	}

} // namespace ecs::test
