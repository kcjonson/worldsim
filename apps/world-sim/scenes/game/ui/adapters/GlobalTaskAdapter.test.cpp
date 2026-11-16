#include "GlobalTaskAdapter.h"

#include <ecs/ClaimIndex.h>
#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/StructureBlueprint.h>
#include <ecs/components/Task.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

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

	namespace {
		GlobalTaskDisplayData row(uint64_t id, uint8_t priority, float distance) {
			GlobalTaskDisplayData data;
			data.id = id;
			data.taskTypePriority = priority;
			data.distanceValue = distance;
			return data;
		}

		std::vector<uint64_t> idsAfterSorting(std::vector<GlobalTaskDisplayData> rows) {
			sortTasksForDisplay(rows);
			std::vector<uint64_t> ids;
			for (const auto& data : rows) {
				ids.push_back(data.id);
			}
			return ids;
		}
	} // namespace

	TEST(SortTasksForDisplayTest, TypePriorityComesBeforeDistance) {
		const auto ids = idsAfterSorting({row(1, 2, 1.0F), row(2, 1, 50.0F)});
		EXPECT_EQ(ids, (std::vector<uint64_t>{2, 1}));
	}

	TEST(SortTasksForDisplayTest, RowsReadingTheSameWholeMetresTieOnId) {
		// 10.0 m and 10.9 m both read "10m"; 11.0 m reads "11m".
		const auto ids = idsAfterSorting({row(3, 1, 11.0F), row(2, 1, 10.9F), row(1, 1, 10.0F)});
		EXPECT_EQ(ids, (std::vector<uint64_t>{1, 2, 3}));
	}

	TEST(SortTasksForDisplayTest, AChainSpacedUnderAMetreSortsTheSameFromEveryInputOrder) {
		const std::array<GlobalTaskDisplayData, 4> chain = {row(4, 1, 10.9F), row(3, 1, 10.5F), row(2, 1, 10.1F), row(1, 1, 9.7F)};
		std::array<size_t, 4>					   order{0, 1, 2, 3};
		const std::vector<uint64_t>				   expected{1, 2, 3, 4};
		do {
			std::vector<GlobalTaskDisplayData> rows;
			for (const size_t index : order) {
				rows.push_back(chain[index]);
			}
			EXPECT_EQ(idsAfterSorting(rows), expected);
		} while (std::next_permutation(order.begin(), order.end()));
	}

} // namespace world_sim::adapters
