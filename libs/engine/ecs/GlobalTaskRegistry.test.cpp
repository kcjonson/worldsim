#include "GlobalTaskRegistry.h"

#include <gtest/gtest.h>

namespace ecs {

class GlobalTaskRegistryTest : public ::testing::Test {
  protected:
	void SetUp() override {
		// Clear registry before each test
		GlobalTaskRegistry::Get().clear();
	}

	void TearDown() override {
		// Clean up after each test
		GlobalTaskRegistry::Get().clear();
	}
};

TEST_F(GlobalTaskRegistryTest, EmptyRegistryHasNoTasks) {
	auto& registry = GlobalTaskRegistry::Get();
	EXPECT_EQ(registry.taskCount(), 0);
}

TEST_F(GlobalTaskRegistryTest, DiscoveringEntityCreatesTask) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist = 1;
	uint64_t worldEntityKey = 12345;
	uint32_t defNameId = 100;
	glm::vec2 position{10.0F, 20.0F};

	uint64_t taskId = registry.onEntityDiscovered(colonist, worldEntityKey, defNameId, position, TaskType::Gather, 0.0F);

	EXPECT_GT(taskId, 0);
	EXPECT_EQ(registry.taskCount(), 1);

	const auto* task = registry.getTask(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_EQ(task->defNameId, defNameId);
	EXPECT_EQ(task->position, position);
	EXPECT_EQ(task->type, TaskType::Gather);
	EXPECT_TRUE(task->isKnownBy(colonist));
}

TEST_F(GlobalTaskRegistryTest, MultipleColonistsCanKnowSameTask) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist1 = 1;
	EntityID colonist2 = 2;
	uint64_t worldEntityKey = 12345;
	uint32_t defNameId = 100;
	glm::vec2 position{10.0F, 20.0F};

	uint64_t taskId1 = registry.onEntityDiscovered(colonist1, worldEntityKey, defNameId, position, TaskType::Gather, 0.0F);
	uint64_t taskId2 = registry.onEntityDiscovered(colonist2, worldEntityKey, defNameId, position, TaskType::Gather, 0.0F);

	// Same task
	EXPECT_EQ(taskId1, taskId2);
	EXPECT_EQ(registry.taskCount(), 1);

	const auto* task = registry.getTask(taskId1);
	ASSERT_NE(task, nullptr);
	EXPECT_TRUE(task->isKnownBy(colonist1));
	EXPECT_TRUE(task->isKnownBy(colonist2));
	EXPECT_EQ(task->knownBy.size(), 2);
}

TEST_F(GlobalTaskRegistryTest, ForgettingEntityRemovesFromKnownBy) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist1 = 1;
	EntityID colonist2 = 2;
	uint64_t worldEntityKey = 12345;

	registry.onEntityDiscovered(colonist1, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist2, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);

	// Colonist1 forgets
	registry.onEntityForgotten(colonist1, worldEntityKey);

	EXPECT_EQ(registry.taskCount(), 1); // Task still exists (colonist2 knows)

	auto tasks = registry.getTasksFor(colonist1);
	EXPECT_EQ(tasks.size(), 0);

	tasks = registry.getTasksFor(colonist2);
	EXPECT_EQ(tasks.size(), 1);
}

TEST_F(GlobalTaskRegistryTest, ForgettingLastKnowerRemovesTask) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist = 1;
	uint64_t worldEntityKey = 12345;

	registry.onEntityDiscovered(colonist, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);
	EXPECT_EQ(registry.taskCount(), 1);

	registry.onEntityForgotten(colonist, worldEntityKey);
	EXPECT_EQ(registry.taskCount(), 0);
}

TEST_F(GlobalTaskRegistryTest, ReservationWorks) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist1 = 1;
	EntityID colonist2 = 2;
	uint64_t worldEntityKey = 12345;

	uint64_t taskId = registry.onEntityDiscovered(colonist1, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist2, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);

	// Colonist1 reserves
	EXPECT_TRUE(registry.reserve(taskId, colonist1, 0.0F));

	const auto* task = registry.getTask(taskId);
	EXPECT_TRUE(task->isReserved());
	EXPECT_TRUE(task->isReservedBy(colonist1));

	// Colonist2 cannot reserve (already reserved)
	EXPECT_FALSE(registry.reserve(taskId, colonist2, 0.0F));

	// Release
	registry.release(taskId);
	EXPECT_FALSE(task->isReserved());

	// Now colonist2 can reserve
	EXPECT_TRUE(registry.reserve(taskId, colonist2, 1.0F));
}

TEST_F(GlobalTaskRegistryTest, OnlyKnowerCanReserve) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist1 = 1;
	EntityID colonist2 = 2; // Never discovers the entity
	uint64_t worldEntityKey = 12345;

	uint64_t taskId = registry.onEntityDiscovered(colonist1, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);

	// Colonist2 cannot reserve (doesn't know about it)
	EXPECT_FALSE(registry.reserve(taskId, colonist2, 0.0F));

	// Colonist1 can reserve
	EXPECT_TRUE(registry.reserve(taskId, colonist1, 0.0F));
}

TEST_F(GlobalTaskRegistryTest, RadiusQueryWorks) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist = 1;

	// Create tasks at various positions
	registry.onEntityDiscovered(colonist, 1, 100, glm::vec2{0.0F, 0.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist, 2, 101, glm::vec2{5.0F, 0.0F}, TaskType::Haul, 0.0F);
	registry.onEntityDiscovered(colonist, 3, 102, glm::vec2{15.0F, 0.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist, 4, 103, glm::vec2{100.0F, 0.0F}, TaskType::Haul, 0.0F);

	EXPECT_EQ(registry.taskCount(), 4);

	// Query within 10m of origin
	auto nearby = registry.getTasksInRadius(glm::vec2{0.0F, 0.0F}, 10.0F);
	EXPECT_EQ(nearby.size(), 2); // Tasks at 0,0 and 5,0

	// Query within 20m of origin
	nearby = registry.getTasksInRadius(glm::vec2{0.0F, 0.0F}, 20.0F);
	EXPECT_EQ(nearby.size(), 3); // Tasks at 0,0, 5,0, and 15,0
}

TEST_F(GlobalTaskRegistryTest, GetTasksByTypeWorks) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist = 1;

	registry.onEntityDiscovered(colonist, 1, 100, glm::vec2{0.0F, 0.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist, 2, 101, glm::vec2{5.0F, 0.0F}, TaskType::Haul, 0.0F);
	registry.onEntityDiscovered(colonist, 3, 102, glm::vec2{10.0F, 0.0F}, TaskType::Gather, 0.0F);

	EXPECT_EQ(registry.taskCount(TaskType::Gather), 2);
	EXPECT_EQ(registry.taskCount(TaskType::Haul), 1);

	auto gatherTasks = registry.getTasksFor(colonist, TaskType::Gather);
	EXPECT_EQ(gatherTasks.size(), 2);

	auto haulTasks = registry.getTasksFor(colonist, TaskType::Haul);
	EXPECT_EQ(haulTasks.size(), 1);
}

TEST_F(GlobalTaskRegistryTest, StaleReservationsReleased) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist = 1;
	uint64_t taskId = registry.onEntityDiscovered(colonist, 1, 100, glm::vec2{0.0F, 0.0F}, TaskType::Gather, 0.0F);

	// Reserve at time 0
	registry.reserve(taskId, colonist, 0.0F);

	const auto* task = registry.getTask(taskId);
	EXPECT_TRUE(task->isReserved());

	// Release stale at time 5 (timeout 10) - should NOT release
	registry.releaseStale(5.0F, 10.0F);
	EXPECT_TRUE(task->isReserved());

	// Release stale at time 15 (timeout 10) - should release
	registry.releaseStale(15.0F, 10.0F);
	EXPECT_FALSE(task->isReserved());
}

TEST_F(GlobalTaskRegistryTest, OnEntityDestroyedRemovesTask) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist1 = 1;
	EntityID colonist2 = 2;
	uint64_t worldEntityKey = 12345;

	// Both colonists discover the same entity
	uint64_t taskId = registry.onEntityDiscovered(colonist1, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist2, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);

	EXPECT_EQ(registry.taskCount(), 1);
	EXPECT_NE(registry.getTask(taskId), nullptr);

	// Entity is destroyed
	registry.onEntityDestroyed(worldEntityKey);

	// Task should be completely removed regardless of how many colonists knew about it
	EXPECT_EQ(registry.taskCount(), 0);
	EXPECT_EQ(registry.getTask(taskId), nullptr);

	// Both colonists should have no tasks
	EXPECT_EQ(registry.getTasksFor(colonist1).size(), 0);
	EXPECT_EQ(registry.getTasksFor(colonist2).size(), 0);
}

TEST_F(GlobalTaskRegistryTest, ForgettingReleasesReservation) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist1 = 1;
	EntityID colonist2 = 2;
	uint64_t worldEntityKey = 12345;

	// Both colonists discover the entity
	uint64_t taskId = registry.onEntityDiscovered(colonist1, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist2, worldEntityKey, 100, glm::vec2{10.0F, 20.0F}, TaskType::Gather, 0.0F);

	// Colonist1 reserves the task
	EXPECT_TRUE(registry.reserve(taskId, colonist1, 0.0F));

	const auto* task = registry.getTask(taskId);
	EXPECT_TRUE(task->isReservedBy(colonist1));

	// Colonist1 forgets the entity - reservation should be released
	registry.onEntityForgotten(colonist1, worldEntityKey);

	// Task should still exist (colonist2 knows)
	EXPECT_EQ(registry.taskCount(), 1);

	// But reservation should be released
	task = registry.getTask(taskId);
	ASSERT_NE(task, nullptr);
	EXPECT_FALSE(task->isReserved());

	// Colonist2 should now be able to reserve
	EXPECT_TRUE(registry.reserve(taskId, colonist2, 1.0F));
}

TEST_F(GlobalTaskRegistryTest, GetTasksMatchingWithFilter) {
	auto& registry = GlobalTaskRegistry::Get();

	EntityID colonist = 1;

	// Create tasks with different types and defNameIds
	registry.onEntityDiscovered(colonist, 1, 100, glm::vec2{0.0F, 0.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist, 2, 101, glm::vec2{5.0F, 0.0F}, TaskType::Haul, 0.0F);
	registry.onEntityDiscovered(colonist, 3, 100, glm::vec2{10.0F, 0.0F}, TaskType::Gather, 0.0F);
	registry.onEntityDiscovered(colonist, 4, 102, glm::vec2{15.0F, 0.0F}, TaskType::Haul, 0.0F);

	EXPECT_EQ(registry.taskCount(), 4);

	// Filter by defNameId
	auto matchingDefName = registry.getTasksMatching([](const GlobalTask& task) { return task.defNameId == 100; });
	EXPECT_EQ(matchingDefName.size(), 2);

	// Filter by type and position
	auto farHaulTasks = registry.getTasksMatching(
		[](const GlobalTask& task) { return task.type == TaskType::Haul && task.position.x > 10.0F; }
	);
	EXPECT_EQ(farHaulTasks.size(), 1);
	EXPECT_EQ(farHaulTasks[0]->defNameId, 102);

	// Filter that matches nothing
	auto noMatch = registry.getTasksMatching([](const GlobalTask& task) { return task.defNameId == 999; });
	EXPECT_EQ(noMatch.size(), 0);
}

} // namespace ecs
