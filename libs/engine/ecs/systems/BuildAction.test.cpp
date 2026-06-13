// Tests for Build and Deconstruct construction actions in ActionSystem.
// Covers the pure work-rate helper, skill-scaled workDone advancement, clamping at the work
// bound, phase transition to Complete, progress() reflection, and the completion callbacks.

#include "ActionSystem.h"

#include "../World.h"
#include "../components/Action.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Skills.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <gtest/gtest.h>

namespace ecs::test {

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
			actionSystem = &world->registerSystem<ActionSystem>();
		}

		void TearDown() override { world.reset(); }

		/// Create a builder colonist with the components ActionSystem's view requires, plus a
		/// Construction skill at the given level.
		EntityID createBuilder(float constructionSkill, glm::vec2 position = {0.0F, 0.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{position});
			world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
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
		auto builder = createBuilder(0.0F);
		auto blueprint = createBlueprint(1000.0F);
		assignBuildTask(builder, blueprint);
		world->update(1.0F);

		float afterFirst = world->getComponent<StructureBlueprint>(blueprint)->workDone;

		// Simulate the colonist leaving and a fresh build session later: clear action/task,
		// re-assign, and tick again. workDone must accumulate, not reset.
		world->getComponent<Action>(builder)->clear();
		assignBuildTask(builder, blueprint);
		world->update(1.0F);

		auto* bp = world->getComponent<StructureBlueprint>(blueprint);
		EXPECT_GT(bp->workDone, afterFirst);
		EXPECT_FLOAT_EQ(bp->workDone, afterFirst * 2.0F);
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

} // namespace ecs::test
