#include "GoalTaskRegistry.h"

#include <gtest/gtest.h>

#include <map>
#include <set>

namespace ecs::test {

	class GoalTaskRegistryTest : public ::testing::Test {
	  protected:
		void SetUp() override { GoalTaskRegistry::Get().clear(); }
		void TearDown() override { GoalTaskRegistry::Get().clear(); }

		static uint64_t make(
			TaskType type, GoalOwner owner, EntityID destination, std::optional<uint64_t> parent = std::nullopt, uint32_t target = 0) {
			GoalTask goal;
			goal.type = type;
			goal.owner = owner;
			goal.destinationEntity = destination;
			goal.parentGoalId = parent;
			goal.targetAmount = target;
			return GoalTaskRegistry::Get().createGoal(std::move(goal));
		}

		static std::set<uint64_t> ids(const std::vector<const GoalTask*>& goals) {
			std::set<uint64_t> out;
			for (const GoalTask* goal : goals) {
				out.insert(goal->id);
			}
			return out;
		}

		// Every lookup the registry offers must describe the same goal set getGoalsMatching walks.
		static void expectIndexesAgreeWithGoalSet() {
			auto&	   registry = GoalTaskRegistry::Get();
			const auto all = registry.getGoalsMatching([](const GoalTask&) { return true; });
			EXPECT_EQ(all.size(), registry.goalCount());

			std::map<GoalOwner, std::set<uint64_t>> byOwner;
			std::map<TaskType, std::set<uint64_t>>	byType;
			std::map<uint64_t, std::set<uint64_t>>	byParent;
			for (const GoalTask* goal : all) {
				EXPECT_EQ(registry.getGoal(goal->id), goal);
				byOwner[goal->owner].insert(goal->id);
				byType[goal->type].insert(goal->id);
				if (goal->parentGoalId.has_value()) {
					byParent[*goal->parentGoalId].insert(goal->id);
				} else {
					const GoalTask* found = registry.getGoalByDestination(goal->destinationEntity);
					ASSERT_NE(found, nullptr);
					EXPECT_EQ(found->id, goal->id);
				}
			}

			for (const auto& [owner, expected] : byOwner) {
				EXPECT_EQ(ids(registry.getGoalsByOwner(owner)), expected);
				EXPECT_EQ(registry.goalCount(owner), expected.size());
			}
			for (const auto& [type, expected] : byType) {
				EXPECT_EQ(ids(registry.getGoalsOfType(type)), expected);
				EXPECT_EQ(registry.goalCount(type), expected.size());
			}
			for (const auto& [parent, expected] : byParent) {
				EXPECT_EQ(ids(registry.getChildGoals(parent)), expected);
			}

			// Owners with no goals left answer empty rather than stale.
			for (GoalOwner owner : {GoalOwner::StorageGoalSystem,
									GoalOwner::CraftingGoalSystem,
									GoalOwner::BuildGoalSystem,
									GoalOwner::ConstructionGoalSystem}) {
				if (!byOwner.contains(owner)) {
					EXPECT_TRUE(registry.getGoalsByOwner(owner).empty());
					EXPECT_EQ(registry.goalCount(owner), 0U);
				}
			}
		}
	};

	// A second session in the same process must hand out the same chain ids as the first.
	TEST_F(GoalTaskRegistryTest, ChainIdsRestartAfterClear) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t first = registry.allocateChainId();
		const uint64_t second = registry.allocateChainId();
		EXPECT_NE(first, 0U);
		EXPECT_NE(first, second);

		registry.clear();
		EXPECT_EQ(registry.allocateChainId(), first);
		EXPECT_EQ(registry.allocateChainId(), second);
	}

	// Pinned: only a Haul child credits its parent Craft; a Harvest child's delivery stays on itself.
	TEST_F(GoalTaskRegistryTest, RecordDeliveryCreditsParentCraftOnlyForHaulChildren) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t craft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 10, std::nullopt, 4);
		registry.updateGoal(craft, [](GoalTask& goal) { goal.status = GoalStatus::Blocked; });
		const uint64_t harvest = make(TaskType::Harvest, GoalOwner::CraftingGoalSystem, 10, craft, 4);
		const uint64_t haul = make(TaskType::Haul, GoalOwner::CraftingGoalSystem, 10, craft, 4);

		registry.recordDelivery(harvest, 2);
		EXPECT_EQ(registry.getGoal(harvest)->deliveredAmount, 2U);
		EXPECT_EQ(registry.getGoal(craft)->deliveredAmount, 0U);

		registry.recordDelivery(haul, 3);
		EXPECT_EQ(registry.getGoal(haul)->deliveredAmount, 3U);
		EXPECT_EQ(registry.getGoal(craft)->deliveredAmount, 3U);
		EXPECT_EQ(registry.getGoal(craft)->status, GoalStatus::Blocked);

		registry.recordDelivery(haul, 1);
		EXPECT_EQ(registry.getGoal(craft)->deliveredAmount, 4U);
		EXPECT_EQ(registry.getGoal(craft)->status, GoalStatus::Available) << "the craft leaves Blocked once materials are met";
	}

	TEST_F(GoalTaskRegistryTest, RecordDeliveryOfZeroChangesNothing) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t craft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 10, std::nullopt, 4);
		const uint64_t haul = make(TaskType::Haul, GoalOwner::CraftingGoalSystem, 10, craft, 4);
		registry.recordDelivery(haul, 0);

		EXPECT_EQ(registry.getGoal(haul)->deliveredAmount, 0U);
		EXPECT_EQ(registry.getGoal(craft)->deliveredAmount, 0U);
	}

	// Pinned: removal cascades through grandchildren and leaves unrelated goals and their indexes alone.
	TEST_F(GoalTaskRegistryTest, RemoveGoalWithChildrenRemovesDescendantsOnly) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t craft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 10);
		const uint64_t haul = make(TaskType::Haul, GoalOwner::CraftingGoalSystem, 10, craft);
		const uint64_t grandchild = make(TaskType::Harvest, GoalOwner::CraftingGoalSystem, 10, haul);
		const uint64_t otherCraft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 20);
		const uint64_t otherChild = make(TaskType::Haul, GoalOwner::CraftingGoalSystem, 20, otherCraft);

		registry.removeGoalWithChildren(craft);

		EXPECT_EQ(registry.getGoal(craft), nullptr);
		EXPECT_EQ(registry.getGoal(haul), nullptr);
		EXPECT_EQ(registry.getGoal(grandchild), nullptr);
		EXPECT_NE(registry.getGoal(otherCraft), nullptr);
		EXPECT_NE(registry.getGoal(otherChild), nullptr);
		EXPECT_EQ(registry.getGoalByDestination(10), nullptr);
		EXPECT_EQ(registry.goalCount(), 2U);
		expectIndexesAgreeWithGoalSet();
	}

	TEST_F(GoalTaskRegistryTest, IndexesAgreeWithGoalSetThroughCreateStatusChangeAndRemoval) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t storage = make(TaskType::Haul, GoalOwner::StorageGoalSystem, 1);
		const uint64_t craft = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 2);
		const uint64_t harvest = make(TaskType::Harvest, GoalOwner::CraftingGoalSystem, 2, craft);
		make(TaskType::Haul, GoalOwner::CraftingGoalSystem, 2, craft);
		make(TaskType::Build, GoalOwner::ConstructionGoalSystem, 3);
		expectIndexesAgreeWithGoalSet();

		registry.updateGoal(craft, [](GoalTask& goal) { goal.status = GoalStatus::Blocked; });
		registry.updateGoal(harvest, [](GoalTask& goal) { goal.status = GoalStatus::NoSource; });
		expectIndexesAgreeWithGoalSet();

		registry.removeGoal(harvest);
		expectIndexesAgreeWithGoalSet();
		registry.removeGoalByDestination(1);
		EXPECT_EQ(registry.getGoal(storage), nullptr);
		expectIndexesAgreeWithGoalSet();
		registry.removeGoalWithChildren(craft);
		expectIndexesAgreeWithGoalSet();
		EXPECT_EQ(registry.goalCount(), 1U);
	}

	// Pinned: a second top-level goal for the same destination replaces the first in place, keeping
	// the id and moving the owner and type index entries with it.
	TEST_F(GoalTaskRegistryTest, CreatingASecondTopLevelGoalForADestinationReplacesItKeepingTheId) {
		auto& registry = GoalTaskRegistry::Get();

		const uint64_t first = make(TaskType::Haul, GoalOwner::StorageGoalSystem, 5);
		const uint64_t second = make(TaskType::Craft, GoalOwner::CraftingGoalSystem, 5);

		EXPECT_EQ(second, first);
		EXPECT_EQ(registry.goalCount(), 1U);
		EXPECT_EQ(registry.getGoal(first)->type, TaskType::Craft);
		EXPECT_EQ(registry.goalCount(GoalOwner::StorageGoalSystem), 0U);
		EXPECT_EQ(registry.goalCount(GoalOwner::CraftingGoalSystem), 1U);
		expectIndexesAgreeWithGoalSet();
	}

} // namespace ecs::test
