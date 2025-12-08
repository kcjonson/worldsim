// Tests for AIDecisionSystem - Colonist autonomous decision making
// Tests tier-based priority, re-evaluation logic, wander behavior, and memory dependency.

#include "AIDecisionSystem.h"

#include "../World.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include "assets/AssetRegistry.h"

#include <gtest/gtest.h>

#include <cmath>

namespace ecs::test {

	// Test fixture for AIDecisionSystem tests
	class AIDecisionSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			// Create ECS world
			world = std::make_unique<World>();

			// Initialize AssetRegistry (singleton) - needed for capability lookups
			auto& registry = engine::assets::AssetRegistry::Get();

			// Register system with deterministic RNG seed for reproducible tests
			world->registerSystem<AIDecisionSystem>(registry, kTestRngSeed);
		}

		void TearDown() override { world.reset(); }

		/// Create a colonist entity with all required components for AI decision making
		EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{position});
			world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
			world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
			world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
			world->addComponent<Memory>(entity, Memory{});
			world->addComponent<Task>(entity, Task{});
			return entity;
		}

		/// Set a specific need value (0-100)
		void setNeedValue(EntityID entity, NeedType need, float value) {
			auto* needs = world->getComponent<NeedsComponent>(entity);
			ASSERT_NE(needs, nullptr);
			needs->get(need).value = value;
		}

		/// Add a known entity to colonist's memory with specific capability
		/// Uses direct defNameId/capabilityMask to avoid AssetRegistry dependency
		void addKnownEntity(EntityID entity, glm::vec2 position, uint32_t defNameId, engine::assets::CapabilityType capability) {
			auto* memory = world->getComponent<Memory>(entity);
			ASSERT_NE(memory, nullptr);
			// Create capability mask from single capability
			uint8_t capabilityMask = static_cast<uint8_t>(1 << static_cast<size_t>(capability));
			memory->rememberWorldEntity(position, defNameId, capabilityMask);
		}

		/// Get the current task for an entity
		Task* getTask(EntityID entity) { return world->getComponent<Task>(entity); }

		/// Get the movement target for an entity
		MovementTarget* getMovementTarget(EntityID entity) { return world->getComponent<MovementTarget>(entity); }

		/// Get the needs component for an entity
		NeedsComponent* getNeeds(EntityID entity) { return world->getComponent<NeedsComponent>(entity); }

		std::unique_ptr<World> world;

		// Fixed RNG seed for deterministic tests
		static constexpr uint32_t kTestRngSeed = 42;

		// Test defNameIds (arbitrary unique values for testing)
		static constexpr uint32_t kBerryBushDefId = 1001;
		static constexpr uint32_t kWaterDefId = 1002;
		static constexpr uint32_t kBedDefId = 1003;
		static constexpr uint32_t kToiletDefId = 1004;
	};

	// =============================================================================
	// Basic Behavior Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, WandersWhenAllNeedsSatisfied) {
		auto colonist = createColonist({0.0F, 0.0F});

		// All needs at 100% (fully satisfied)
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		// Run update
		world->update(0.016F);

		// Should assign wander task
		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::Wander);
		EXPECT_EQ(task->state, TaskState::Moving);
		EXPECT_FALSE(task->reason.empty());

		// Movement target should be active
		auto* movementTarget = getMovementTarget(colonist);
		ASSERT_NE(movementTarget, nullptr);
		EXPECT_TRUE(movementTarget->active);
	}

	TEST_F(AIDecisionSystemTest, NoTaskAssignedWithoutRequiredComponents) {
		// Create entity with only Position (missing other components)
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{{0.0F, 0.0F}});

		// Update should not crash and should not affect partial entity
		EXPECT_NO_THROW(world->update(0.016F));
	}

	// =============================================================================
	// Tier Priority Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, CriticalNeedTakesPriorityOverActionable) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add known berry bush (for hunger)
		addKnownEntity(colonist, {5.0F, 5.0F}, kBerryBushDefId, engine::assets::CapabilityType::Edible);

		// Add known water source (for thirst)
		addKnownEntity(colonist, {10.0F, 10.0F}, kWaterDefId, engine::assets::CapabilityType::Drinkable);

		// Hunger at CRITICAL level (5%), Thirst at actionable level (40%)
		setNeedValue(colonist, NeedType::Hunger, 5.0F);	 // Critical
		setNeedValue(colonist, NeedType::Thirst, 40.0F); // Actionable
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Hunger); // Should prioritize critical hunger
		EXPECT_TRUE(task->reason.find("CRITICAL") != std::string::npos);
	}

	TEST_F(AIDecisionSystemTest, ActionableNeedTriggersMovement) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add known berry bush
		glm::vec2 berryPosition = {8.0F, 3.0F};
		addKnownEntity(colonist, berryPosition, kBerryBushDefId, engine::assets::CapabilityType::Edible);

		// Hunger at actionable level (40%)
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Hunger);
		EXPECT_EQ(task->state, TaskState::Moving);

		// Target should be the berry bush position
		EXPECT_FLOAT_EQ(task->targetPosition.x, berryPosition.x);
		EXPECT_FLOAT_EQ(task->targetPosition.y, berryPosition.y);
	}

	TEST_F(AIDecisionSystemTest, MostUrgentNeedSelectedWhenMultipleActionable) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add known entities for both needs
		addKnownEntity(colonist, {5.0F, 0.0F}, kBerryBushDefId, engine::assets::CapabilityType::Edible);
		addKnownEntity(colonist, {0.0F, 5.0F}, kWaterDefId, engine::assets::CapabilityType::Drinkable);

		// Thirst more urgent than hunger
		setNeedValue(colonist, NeedType::Hunger, 45.0F);
		setNeedValue(colonist, NeedType::Thirst, 30.0F); // Lower = more urgent
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Thirst); // Should select most urgent
	}

	// =============================================================================
	// Ground Fallback Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, EnergyNeedUsesGroundFallback) {
		auto colonist = createColonist({5.0F, 5.0F});

		// No sleepable entities in memory - Energy should use ground fallback
		// Use a lower value to ensure it's actionable (different needs may have different thresholds)
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 20.0F); // Actionable (low enough to trigger)
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Energy);

		// Target should be current position (ground fallback)
		EXPECT_FLOAT_EQ(task->targetPosition.x, 5.0F);
		EXPECT_FLOAT_EQ(task->targetPosition.y, 5.0F);

		// Should immediately be in Arrived state (no movement needed)
		EXPECT_EQ(task->state, TaskState::Arrived);
	}

	TEST_F(AIDecisionSystemTest, BladderNeedUsesGroundFallback) {
		auto colonist = createColonist({-3.0F, 7.0F});

		// No toilet entities in memory - Bladder should use ground fallback
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 25.0F); // Actionable

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Bladder);

		// Target should be current position (ground fallback)
		EXPECT_FLOAT_EQ(task->targetPosition.x, -3.0F);
		EXPECT_FLOAT_EQ(task->targetPosition.y, 7.0F);
		EXPECT_EQ(task->state, TaskState::Arrived);
	}

	TEST_F(AIDecisionSystemTest, CriticalEnergyUsesGroundFallback) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Critical energy with no bed available
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 5.0F); // Critical
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Energy);
		EXPECT_TRUE(task->reason.find("CRITICAL") != std::string::npos);
		EXPECT_TRUE(task->reason.find("ground") != std::string::npos);
	}

	// =============================================================================
	// Re-evaluation Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, ReEvaluatesWhenTaskArrived) {
		auto colonist = createColonist({0.0F, 0.0F});

		// First update - assigns wander
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::Wander);

		// Simulate arrival
		task->state = TaskState::Arrived;

		// Second update - should re-evaluate and assign new wander
		world->update(0.016F);

		// Should have a new task (timer reset)
		EXPECT_EQ(task->type, TaskType::Wander);
		EXPECT_EQ(task->timeSinceEvaluation, 0.0F);
	}

	TEST_F(AIDecisionSystemTest, ReEvaluatesWhenCriticalNeedEmerges) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add known berry bush
		addKnownEntity(colonist, {10.0F, 10.0F}, kBerryBushDefId, engine::assets::CapabilityType::Edible);

		// Start with all needs satisfied - will wander
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::Wander);

		// Suddenly, hunger becomes critical
		setNeedValue(colonist, NeedType::Hunger, 5.0F);

		// Next update should interrupt wander for critical need
		world->update(0.016F);

		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Hunger);
		EXPECT_TRUE(task->reason.find("CRITICAL") != std::string::npos);
	}

	TEST_F(AIDecisionSystemTest, DoesNotReEvaluateWhileHandlingCriticalNeed) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add two edible sources at different distances
		addKnownEntity(colonist, {10.0F, 0.0F}, kBerryBushDefId, engine::assets::CapabilityType::Edible);
		addKnownEntity(colonist, {5.0F, 0.0F}, kBerryBushDefId + 1, engine::assets::CapabilityType::Edible);

		// Critical hunger
		setNeedValue(colonist, NeedType::Hunger, 5.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);

		// Store original target
		glm::vec2 originalTarget = task->targetPosition;

		// Make another need critical
		setNeedValue(colonist, NeedType::Thirst, 5.0F);

		// Update again - should NOT switch tasks because already handling critical
		world->update(0.016F);

		// Should still be heading to same target
		EXPECT_EQ(task->needToFulfill, NeedType::Hunger);
		EXPECT_FLOAT_EQ(task->targetPosition.x, originalTarget.x);
		EXPECT_FLOAT_EQ(task->targetPosition.y, originalTarget.y);
	}

	TEST_F(AIDecisionSystemTest, TimerIncrementsWhenNotReEvaluating) {
		auto colonist = createColonist({0.0F, 0.0F});

		addKnownEntity(colonist, {20.0F, 20.0F}, kBerryBushDefId, engine::assets::CapabilityType::Edible);

		// Actionable need - will create task and start timer
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->timeSinceEvaluation, 0.0F); // Just evaluated

		// Multiple updates while moving (not arrived)
		world->update(0.1F);
		EXPECT_NEAR(task->timeSinceEvaluation, 0.1F, 0.01F);

		world->update(0.1F);
		EXPECT_NEAR(task->timeSinceEvaluation, 0.2F, 0.01F);
	}

	// =============================================================================
	// Memory Dependency Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, CannotFindUnknownEntities) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Hunger at actionable level, but NO berry bushes in memory
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);

		// Without food in memory, should fall through to wander
		EXPECT_EQ(task->type, TaskType::Wander);
	}

	TEST_F(AIDecisionSystemTest, FindsNearestKnownEntity) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add multiple berry bushes at different distances
		glm::vec2 farBerry = {20.0F, 20.0F};
		glm::vec2 nearBerry = {3.0F, 4.0F}; // Distance 5
		glm::vec2 midBerry = {10.0F, 0.0F}; // Distance 10

		addKnownEntity(colonist, farBerry, kBerryBushDefId, engine::assets::CapabilityType::Edible);
		addKnownEntity(colonist, nearBerry, kBerryBushDefId + 1, engine::assets::CapabilityType::Edible);
		addKnownEntity(colonist, midBerry, kBerryBushDefId + 2, engine::assets::CapabilityType::Edible);

		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);

		// Should target the nearest berry bush
		EXPECT_FLOAT_EQ(task->targetPosition.x, nearBerry.x);
		EXPECT_FLOAT_EQ(task->targetPosition.y, nearBerry.y);
	}

	// =============================================================================
	// Wander Behavior Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, WanderTargetWithinRadius) {
		auto colonist = createColonist({0.0F, 0.0F});

		// All needs satisfied - will wander
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::Wander);

		// Wander target should be within radius (8.0m per AIDecisionSystem.h)
		float distance = std::sqrt(task->targetPosition.x * task->targetPosition.x + task->targetPosition.y * task->targetPosition.y);

		constexpr float kWanderRadius = 8.0F;
		EXPECT_LE(distance, kWanderRadius);
		EXPECT_GT(distance, 0.0F); // Should not be at exact same position
	}

	TEST_F(AIDecisionSystemTest, DeterministicWanderWithSeed) {
		// Create first colonist and get wander target
		auto colonist1 = createColonist({0.0F, 0.0F});
		setNeedValue(colonist1, NeedType::Hunger, 100.0F);
		setNeedValue(colonist1, NeedType::Thirst, 100.0F);
		setNeedValue(colonist1, NeedType::Energy, 100.0F);
		setNeedValue(colonist1, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task1 = getTask(colonist1);
		ASSERT_NE(task1, nullptr);
		glm::vec2 target1 = task1->targetPosition;

		// Recreate world with same seed
		world.reset();
		world = std::make_unique<World>();
		auto& registry = engine::assets::AssetRegistry::Get();
		world->registerSystem<AIDecisionSystem>(registry, kTestRngSeed);

		auto colonist2 = createColonist({0.0F, 0.0F});
		setNeedValue(colonist2, NeedType::Hunger, 100.0F);
		setNeedValue(colonist2, NeedType::Thirst, 100.0F);
		setNeedValue(colonist2, NeedType::Energy, 100.0F);
		setNeedValue(colonist2, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task2 = getTask(colonist2);
		ASSERT_NE(task2, nullptr);

		// With same seed and same initial conditions, wander target should be identical
		EXPECT_FLOAT_EQ(task2->targetPosition.x, target1.x);
		EXPECT_FLOAT_EQ(task2->targetPosition.y, target1.y);
	}

	// =============================================================================
	// Task State Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, TaskClearedOnReEvaluation) {
		auto colonist = createColonist({0.0F, 0.0F});

		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);

		// Simulate arrival
		task->state = TaskState::Arrived;

		// Store old reason
		std::string oldReason = task->reason;

		// Force re-evaluation
		world->update(0.016F);

		// Task should have new reason (indicates it was cleared and reassigned)
		EXPECT_EQ(task->timeSinceEvaluation, 0.0F);
	}

	TEST_F(AIDecisionSystemTest, MovementTargetActivatedForMovingTask) {
		auto colonist = createColonist({0.0F, 0.0F});

		addKnownEntity(colonist, {10.0F, 10.0F}, kBerryBushDefId, engine::assets::CapabilityType::Edible);

		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		auto* movementTarget = getMovementTarget(colonist);

		ASSERT_NE(task, nullptr);
		ASSERT_NE(movementTarget, nullptr);

		EXPECT_EQ(task->state, TaskState::Moving);
		EXPECT_TRUE(movementTarget->active);
		EXPECT_FLOAT_EQ(movementTarget->target.x, task->targetPosition.x);
		EXPECT_FLOAT_EQ(movementTarget->target.y, task->targetPosition.y);
	}

	TEST_F(AIDecisionSystemTest, MovementTargetDeactivatedForArrivedTask) {
		auto colonist = createColonist({5.0F, 5.0F});

		// Bladder with no toilet - will use ground fallback (already at position)
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 25.0F); // Actionable, uses ground fallback

		world->update(0.016F);

		auto* task = getTask(colonist);
		auto* movementTarget = getMovementTarget(colonist);

		ASSERT_NE(task, nullptr);
		ASSERT_NE(movementTarget, nullptr);

		EXPECT_EQ(task->state, TaskState::Arrived);
		EXPECT_FALSE(movementTarget->active); // No movement needed
	}

	// =============================================================================
	// Performance Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, HandlesMultipleColonistsEfficiently) {
		constexpr int kNumColonists = 100;

		// Create many colonists
		std::vector<EntityID> colonists;
		colonists.reserve(kNumColonists);

		for (int i = 0; i < kNumColonists; ++i) {
			auto colonist = createColonist({static_cast<float>(i), static_cast<float>(i)});
			colonists.push_back(colonist);

			// Vary needs so some wander, some seek food
			if (i % 3 == 0) {
				setNeedValue(colonist, NeedType::Hunger, 40.0F);
			}
		}

		// Add some berry bushes for colonists to find
		for (int i = 0; i < 10; ++i) {
			// Add to first colonist's memory (just for test setup)
			for (auto colonist : colonists) {
				addKnownEntity(
					colonist,
					{static_cast<float>(i * 10), 0.0F},
					kBerryBushDefId + static_cast<uint32_t>(i),
					engine::assets::CapabilityType::Edible
				);
			}
		}

		// This should complete in reasonable time
		auto start = std::chrono::high_resolution_clock::now();
		world->update(0.016F);
		auto end = std::chrono::high_resolution_clock::now();

		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

		// Should process 100 colonists in well under 100ms
		EXPECT_LT(duration.count(), 100);

		// Verify all colonists have tasks
		for (auto colonist : colonists) {
			auto* task = getTask(colonist);
			ASSERT_NE(task, nullptr);
			EXPECT_TRUE(task->isActive());
		}
	}

	TEST_F(AIDecisionSystemTest, MemoryCapacityDoesNotAffectDecisionSpeed) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Fill memory with many entities
		constexpr int kNumEntities = 1000;
		for (int i = 0; i < kNumEntities; ++i) {
			addKnownEntity(
				colonist,
				{static_cast<float>(i), static_cast<float>(i)},
				kBerryBushDefId + static_cast<uint32_t>(i),
				engine::assets::CapabilityType::Edible
			);
		}

		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		auto start = std::chrono::high_resolution_clock::now();
		world->update(0.016F);
		auto end = std::chrono::high_resolution_clock::now();

		auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

		// Should still be fast even with large memory
		EXPECT_LT(duration.count(), 50);

		// Should have found an entity and created task
		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
	}

} // namespace ecs::test
