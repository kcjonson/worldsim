#include "GlobalTaskAdapter.h"

#include <ecs/ClaimIndex.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>

#include <gtest/gtest.h>

namespace world_sim::adapters {

	// The panel's and the Known work tab's rows for a structure being torn down.
	class GlobalTaskAdapterTest : public ::testing::Test {
	  protected:
		void SetUp() override { ecs::GoalTaskRegistry::Get().clear(); }
		void TearDown() override { ecs::GoalTaskRegistry::Get().clear(); }

		/// A structure with `workDone` of `workTotal` left to tear down, and its Deconstruct goal.
		ecs::EntityID structure(float workDone) {
			const ecs::EntityID entity = world.createEntity();
			ecs::StructureBlueprint blueprint;
			blueprint.workTotal = 100.0F;
			blueprint.workDone = workDone;
			world.addComponent<ecs::StructureBlueprint>(entity, blueprint);
			ecs::GoalTask goal;
			goal.type = ecs::TaskType::Deconstruct;
			goal.owner = ecs::GoalOwner::ConstructionGoalSystem;
			goal.destinationEntity = entity;
			ecs::GoalTaskRegistry::Get().createGoal(std::move(goal));
			return entity;
		}

		ecs::EntityID tearingDown(ecs::EntityID target, const char* name) {
			const ecs::EntityID entity = world.createEntity();
			world.addComponent<ecs::Colonist>(entity, ecs::Colonist{.name = name});
			ecs::Task task;
			task.type = ecs::TaskType::Deconstruct;
			task.buildBlueprintEntityId = static_cast<uint64_t>(target);
			world.addComponent<ecs::Task>(entity, task);
			return entity;
		}

		std::vector<GlobalTaskDisplayData> panel() { return getGlobalTasks(world, claims, {0.0F, 0.0F}); }

		ecs::World	   world;
		ecs::ClaimIndex claims;
	};

	TEST_F(GlobalTaskAdapterTest, ATeardownWithAWorkerIsOneRowReadingDeconstructing) {
		const ecs::EntityID target = structure(25.0F);
		const ecs::EntityID worker = tearingDown(target, "Mara");

		const auto rows = panel();
		ASSERT_EQ(rows.size(), 1U);
		EXPECT_EQ(rows[0].description, "Deconstruct structure");
		EXPECT_EQ(rows[0].status, "Deconstructing");
		EXPECT_EQ(rows[0].statusDetail, "Mara (75%)");

		const auto known = getKnownTasks(world, claims, worker, {0.0F, 0.0F});
		ASSERT_EQ(known.size(), 1U) << "the tab lists it for the colonist tearing it down";
		EXPECT_EQ(known[0].status, "Deconstructing");
	}

	TEST_F(GlobalTaskAdapterTest, ATeardownWithNoWorkLeftOrNoBlueprintIsNotListed) {
		const ecs::EntityID done = structure(0.0F);
		tearingDown(done, "Mara");
		EXPECT_TRUE(panel().empty()) << "the goal outlives the last tick until the next construction pass";

		const ecs::EntityID gone = structure(40.0F);
		world.removeComponent<ecs::StructureBlueprint>(gone);
		EXPECT_TRUE(panel().empty()) << "its blueprint is removed";
	}

} // namespace world_sim::adapters
