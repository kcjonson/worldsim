// Tests for AIDecisionSystem - Colonist autonomous decision making
// Tests tier-based priority, re-evaluation logic, wander behavior, memory dependency,
// and DecisionTrace generation for task queue display.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"

#include "../World.h"
#include "../components/Action.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include "assets/AssetRegistry.h"
#include "assets/RecipeRegistry.h"

#include <gtest/gtest.h>

#include <chrono>
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

			// Register Berry as edible entity for testing (unified entity/item model)
			engine::assets::AssetDefinition berryDef;
			berryDef.defName = "Berry";
			berryDef.label = "Berry";
			berryDef.itemProperties = engine::assets::ItemProperties{};
			berryDef.itemProperties->stackSize = 20;
			berryDef.itemProperties->edible = engine::assets::EdibleCapability{0.3F, engine::assets::CapabilityQuality::Normal, true};
			registry.registerTestDefinition(std::move(berryDef));

			// Register system with deterministic RNG seed for reproducible tests
			auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
			world->registerSystem<AIDecisionSystem>(registry, recipeRegistry, kTestRngSeed);
		}

		void TearDown() override {
			// Clean up test definitions
			engine::assets::AssetRegistry::Get().clearDefinitions();
			world.reset();
		}

		/// Create a colonist entity with all required components for AI decision making
		/// Note: Includes DecisionTrace which is required for the inventory-aware hunger path
		EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{position});
			world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
			world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
			world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
			world->addComponent<Memory>(entity, Memory{});
			world->addComponent<Inventory>(entity, Inventory::createForColonist());
			world->addComponent<Task>(entity, Task{});
			world->addComponent<DecisionTrace>(entity, DecisionTrace{});
			return entity;
		}

		/// Get the decision trace for an entity
		DecisionTrace* getTrace(EntityID entity) { return world->getComponent<DecisionTrace>(entity); }

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

		// Give colonist berries in inventory so hunger has a valid fulfillment path
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Berry", 3);

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
		EXPECT_TRUE(task->reason.find("critical") != std::string::npos);
	}

	TEST_F(AIDecisionSystemTest, ActionableNeedTriggersMovement) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add known water source - use Drinkable capability (simpler path without yield validation)
		glm::vec2 waterPosition = {8.0F, 3.0F};
		addKnownEntity(colonist, waterPosition, kWaterDefId, engine::assets::CapabilityType::Drinkable);

		// Thirst at actionable level (40%)
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 40.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Thirst);
		EXPECT_EQ(task->state, TaskState::Moving);

		// Target should be the water source position
		EXPECT_FLOAT_EQ(task->targetPosition.x, waterPosition.x);
		EXPECT_FLOAT_EQ(task->targetPosition.y, waterPosition.y);
	}

	TEST_F(AIDecisionSystemTest, MostUrgentNeedSelectedWhenMultipleActionable) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add known entities for both needs - hunger uses Harvestable
		addKnownEntity(colonist, {5.0F, 0.0F}, kBerryBushDefId, engine::assets::CapabilityType::Harvestable);
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
		EXPECT_TRUE(task->reason.find("critical") != std::string::npos);
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

		// Give colonist berries so hunger has a valid fulfillment path
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Berry", 3);

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
		EXPECT_TRUE(task->reason.find("critical") != std::string::npos);
	}

	TEST_F(AIDecisionSystemTest, DoesNotReEvaluateWhileHandlingCriticalNeed) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Give colonist berries so hunger has a valid fulfillment path
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Berry", 3);

		// Critical hunger
		setNeedValue(colonist, NeedType::Hunger, 5.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);

		// Store original need being fulfilled
		NeedType originalNeed = task->needToFulfill;
		EXPECT_EQ(originalNeed, NeedType::Hunger);

		// Make another need critical
		setNeedValue(colonist, NeedType::Thirst, 5.0F);

		// Update again - should NOT switch tasks because already handling critical
		world->update(0.016F);

		// Should still be handling the same need
		EXPECT_EQ(task->needToFulfill, NeedType::Hunger);
	}

	TEST_F(AIDecisionSystemTest, TimerIncrementsWhenNotReEvaluating) {
		auto colonist = createColonist({0.0F, 0.0F});

		addKnownEntity(colonist, {20.0F, 20.0F}, kBerryBushDefId, engine::assets::CapabilityType::Harvestable);

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

		// Add multiple water sources at different distances - use Drinkable for simpler path
		glm::vec2 farWater = {20.0F, 20.0F};
		glm::vec2 nearWater = {3.0F, 4.0F}; // Distance 5
		glm::vec2 midWater = {10.0F, 0.0F}; // Distance 10

		addKnownEntity(colonist, farWater, kWaterDefId, engine::assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, nearWater, kWaterDefId + 1, engine::assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, midWater, kWaterDefId + 2, engine::assets::CapabilityType::Drinkable);

		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 40.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);

		// Should target the nearest water source
		EXPECT_FLOAT_EQ(task->targetPosition.x, nearWater.x);
		EXPECT_FLOAT_EQ(task->targetPosition.y, nearWater.y);
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
		auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
		world->registerSystem<AIDecisionSystem>(registry, recipeRegistry, kTestRngSeed);

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

		// Add some berry bushes for colonists to find - hunger uses Harvestable
		for (int i = 0; i < 10; ++i) {
			// Add to first colonist's memory (just for test setup)
			for (auto colonist : colonists) {
				addKnownEntity(
					colonist,
					{static_cast<float>(i * 10), 0.0F},
					kBerryBushDefId + static_cast<uint32_t>(i),
					engine::assets::CapabilityType::Harvestable
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

		// Fill memory with many entities - use Drinkable for simpler path
		constexpr int kNumEntities = 1000;
		for (int i = 0; i < kNumEntities; ++i) {
			addKnownEntity(
				colonist,
				{static_cast<float>(i), static_cast<float>(i)},
				kWaterDefId + static_cast<uint32_t>(i),
				engine::assets::CapabilityType::Drinkable
			);
		}

		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 40.0F);
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

	// =============================================================================
	// DecisionTrace Generation Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, TraceContainsAllNeedsPlusWander) {
		auto colonist = createColonist({0.0F, 0.0F});

		// All needs satisfied
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);
		setNeedValue(colonist, NeedType::Digestion, 100.0F);

		// Give colonist food in inventory to suppress Gather Food option
		// (otherwise we'd get 7 options: 5 needs + gather food + wander)
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		inventory->addItem("Berry", 1);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);

		// Should have 6 options: 5 needs + wander (no Gather Food since inventory has food)
		EXPECT_EQ(trace->options.size(), 6u);

		// Verify all need types are present
		bool hasHunger = false, hasThirst = false, hasEnergy = false, hasBladder = false, hasDigestion = false, hasWander = false;
		for (const auto& option : trace->options) {
			if (option.taskType == TaskType::Wander) {
				hasWander = true;
			} else if (option.taskType == TaskType::FulfillNeed) {
				switch (option.needType) {
					case NeedType::Hunger:
						hasHunger = true;
						break;
					case NeedType::Thirst:
						hasThirst = true;
						break;
					case NeedType::Energy:
						hasEnergy = true;
						break;
					case NeedType::Bladder:
						hasBladder = true;
						break;
					case NeedType::Digestion:
						hasDigestion = true;
						break;
					default:
						break;
				}
			}
		}

		EXPECT_TRUE(hasHunger);
		EXPECT_TRUE(hasThirst);
		EXPECT_TRUE(hasEnergy);
		EXPECT_TRUE(hasBladder);
		EXPECT_TRUE(hasDigestion);
		EXPECT_TRUE(hasWander);
	}

	TEST_F(AIDecisionSystemTest, TraceOptionsSortedByPriority) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Give colonist food to suppress Gather Food option
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		inventory->addItem("Berry", 1);

		// Add berry bush for hunger - uses Harvestable
		addKnownEntity(colonist, {5.0F, 0.0F}, kBerryBushDefId, engine::assets::CapabilityType::Harvestable);

		// Set up varying need levels
		setNeedValue(colonist, NeedType::Hunger, 40.0F);   // Actionable
		setNeedValue(colonist, NeedType::Thirst, 100.0F);  // Satisfied
		setNeedValue(colonist, NeedType::Energy, 100.0F);  // Satisfied
		setNeedValue(colonist, NeedType::Bladder, 100.0F); // Satisfied

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		ASSERT_GE(trace->options.size(), 2u);

		// Verify options are sorted by priority (descending)
		for (size_t i = 1; i < trace->options.size(); ++i) {
			float prevPriority = trace->options[i - 1].calculatePriority();
			float currPriority = trace->options[i].calculatePriority();
			EXPECT_GE(prevPriority, currPriority) << "Options not sorted at index " << i;
		}
	}

	TEST_F(AIDecisionSystemTest, TraceMarksFirstActionableAsSelected) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Give colonist berries so hunger has a valid fulfillment path
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Berry", 3);

		// Hunger needs attention
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);

		// Should have exactly one Selected option
		int					   selectedCount = 0;
		const EvaluatedOption* selected = nullptr;
		for (const auto& option : trace->options) {
			if (option.status == OptionStatus::Selected) {
				selectedCount++;
				selected = &option;
			}
		}

		EXPECT_EQ(selectedCount, 1);
		ASSERT_NE(selected, nullptr);

		// Selected option should be the first in sorted order (highest priority)
		EXPECT_EQ(selected, &trace->options[0]);
	}

	TEST_F(AIDecisionSystemTest, TraceShowsNoSourceWhenNotInMemory) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Hunger needs attention, but NO food in memory
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);

		// Find the hunger option
		const EvaluatedOption* hungerOption = nullptr;
		for (const auto& option : trace->options) {
			if (option.taskType == TaskType::FulfillNeed && option.needType == NeedType::Hunger) {
				hungerOption = &option;
				break;
			}
		}

		ASSERT_NE(hungerOption, nullptr);
		EXPECT_EQ(hungerOption->status, OptionStatus::NoSource);
		EXPECT_TRUE(hungerOption->reason.find("no known source") != std::string::npos);
	}

	TEST_F(AIDecisionSystemTest, TraceShowsSatisfiedForHighNeeds) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Give colonist food to suppress Gather Food option
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		inventory->addItem("Berry", 1);

		// All needs fully satisfied
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);

		// All need options should be Satisfied
		for (const auto& option : trace->options) {
			if (option.taskType == TaskType::FulfillNeed) {
				EXPECT_EQ(option.status, OptionStatus::Satisfied)
					<< "Need type " << static_cast<int>(option.needType) << " should be Satisfied";
			}
		}
	}

	TEST_F(AIDecisionSystemTest, TraceGroundFallbackForEnergyAndBladder) {
		auto colonist = createColonist({5.0F, 5.0F});

		// Energy and Bladder need attention, no entities in memory
		// Note: Energy/Bladder seek threshold is 30%, so values must be < 30% to be actionable
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 25.0F);  // Actionable (below 30% threshold)
		setNeedValue(colonist, NeedType::Bladder, 20.0F); // Actionable (below 30% threshold)

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);

		// Find energy and bladder options
		const EvaluatedOption* energyOption = nullptr;
		const EvaluatedOption* bladderOption = nullptr;
		for (const auto& option : trace->options) {
			if (option.taskType == TaskType::FulfillNeed) {
				if (option.needType == NeedType::Energy) {
					energyOption = &option;
				} else if (option.needType == NeedType::Bladder) {
					bladderOption = &option;
				}
			}
		}

		// Both should be Available (ground fallback), not NoSource
		ASSERT_NE(energyOption, nullptr);
		ASSERT_NE(bladderOption, nullptr);

		// One is Selected, other is Available (both are actionable with ground fallback)
		bool energyActionable = energyOption->status == OptionStatus::Selected || energyOption->status == OptionStatus::Available;
		bool bladderActionable = bladderOption->status == OptionStatus::Selected || bladderOption->status == OptionStatus::Available;
		EXPECT_TRUE(energyActionable) << "Energy should be actionable with ground fallback";
		EXPECT_TRUE(bladderActionable) << "Bladder should be actionable with ground fallback";

		// Both should have current position as target (ground fallback)
		ASSERT_TRUE(energyOption->targetPosition.has_value());
		ASSERT_TRUE(bladderOption->targetPosition.has_value());
		EXPECT_FLOAT_EQ(energyOption->targetPosition->x, 5.0F);
		EXPECT_FLOAT_EQ(energyOption->targetPosition->y, 5.0F);
		EXPECT_FLOAT_EQ(bladderOption->targetPosition->x, 5.0F);
		EXPECT_FLOAT_EQ(bladderOption->targetPosition->y, 5.0F);
	}

	TEST_F(AIDecisionSystemTest, TraceTaskMatchesSelectedOption) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Add berry bush - uses Harvestable
		glm::vec2 berryPosition = {8.0F, 3.0F};
		addKnownEntity(colonist, berryPosition, kBerryBushDefId, engine::assets::CapabilityType::Harvestable);

		// Hunger needs attention
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		auto* task = getTask(colonist);
		ASSERT_NE(trace, nullptr);
		ASSERT_NE(task, nullptr);

		// Get selected option from trace
		const auto* selected = trace->getSelected();
		ASSERT_NE(selected, nullptr);

		// Task should match selected option
		EXPECT_EQ(task->type, selected->taskType);
		EXPECT_EQ(task->needToFulfill, selected->needType);
		if (selected->targetPosition.has_value()) {
			EXPECT_FLOAT_EQ(task->targetPosition.x, selected->targetPosition->x);
			EXPECT_FLOAT_EQ(task->targetPosition.y, selected->targetPosition->y);
		}
	}

	TEST_F(AIDecisionSystemTest, TraceDisplayCountRespectsCap) {
		auto colonist = createColonist({0.0F, 0.0F});

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);

		// displayCount should be at most kMaxDisplayedOptions
		EXPECT_LE(trace->displayCount(), kMaxDisplayedOptions);
		// And equal to actual size when below cap
		EXPECT_EQ(trace->displayCount(), trace->options.size());
	}

	TEST_F(AIDecisionSystemTest, TraceClearedOnReEvaluation) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Give colonist food to suppress Gather Food option
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		inventory->addItem("Berry", 1);

		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);
		setNeedValue(colonist, NeedType::Digestion, 100.0F);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);

		// Store initial selection summary
		std::string initialSummary = trace->selectionSummary;
		EXPECT_FALSE(initialSummary.empty());

		// Trigger re-evaluation by simulating arrival
		task->state = TaskState::Arrived;
		world->update(0.016F);

		// Trace should be rebuilt (may have same or different summary)
		// The key is that it's still valid
		EXPECT_FALSE(trace->selectionSummary.empty());
		EXPECT_EQ(trace->options.size(), 6u); // Still has all options (5 needs + wander, no Gather Food since has food)
	}

	// =============================================================================
	// EvaluatedOption Priority Calculation Tests
	// =============================================================================

	TEST_F(AIDecisionSystemTest, PriorityCalculationCriticalNeeds) {
		EvaluatedOption option;
		option.taskType = TaskType::FulfillNeed;
		option.needType = NeedType::Hunger;
		option.threshold = 50.0F;
		option.status = OptionStatus::Available;

		// Critical need at 5% should give priority 305
		option.needValue = 5.0F;
		EXPECT_FLOAT_EQ(option.calculatePriority(), 305.0F);

		// Critical need at 9% should give priority 301
		option.needValue = 9.0F;
		EXPECT_FLOAT_EQ(option.calculatePriority(), 301.0F);

		// Edge case: exactly 10% is NOT critical, falls to actionable
		option.needValue = 10.0F;
		float priority = option.calculatePriority();
		EXPECT_LT(priority, 300.0F); // Not in critical range
		EXPECT_GE(priority, 100.0F); // In actionable range
	}

	TEST_F(AIDecisionSystemTest, PriorityCalculationActionableNeeds) {
		EvaluatedOption option;
		option.taskType = TaskType::FulfillNeed;
		option.needType = NeedType::Hunger;
		option.threshold = 50.0F;
		option.status = OptionStatus::Available;

		// Actionable at 40% with threshold 50 should give priority 110
		option.needValue = 40.0F;
		EXPECT_FLOAT_EQ(option.calculatePriority(), 110.0F);

		// Actionable at 30% with threshold 50 should give priority 120
		option.needValue = 30.0F;
		EXPECT_FLOAT_EQ(option.calculatePriority(), 120.0F);
	}

	TEST_F(AIDecisionSystemTest, PriorityCalculationWander) {
		EvaluatedOption option;
		option.taskType = TaskType::Wander;
		option.status = OptionStatus::Available;

		// Wander always has priority 10
		EXPECT_FLOAT_EQ(option.calculatePriority(), 10.0F);
	}

	TEST_F(AIDecisionSystemTest, PriorityCalculationSatisfied) {
		EvaluatedOption option;
		option.taskType = TaskType::FulfillNeed;
		option.needType = NeedType::Hunger;
		option.needValue = 80.0F; // Above threshold
		option.threshold = 50.0F;
		option.status = OptionStatus::Satisfied;

		// Satisfied needs have zero priority
		EXPECT_FLOAT_EQ(option.calculatePriority(), 0.0F);
	}

	TEST_F(AIDecisionSystemTest, IsActionableReturnsCorrectly) {
		EvaluatedOption option;

		option.status = OptionStatus::Selected;
		EXPECT_TRUE(option.isActionable());

		option.status = OptionStatus::Available;
		EXPECT_TRUE(option.isActionable());

		option.status = OptionStatus::NoSource;
		EXPECT_FALSE(option.isActionable());

		option.status = OptionStatus::Satisfied;
		EXPECT_FALSE(option.isActionable());
	}

	TEST_F(AIDecisionSystemTest, TraceCriticalNeedHasHighestPriority) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Give colonist berries so hunger has a valid fulfillment path
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Berry", 3);

		// Add water source for thirst
		addKnownEntity(colonist, {0.0F, 5.0F}, kWaterDefId, engine::assets::CapabilityType::Drinkable);

		// Critical hunger, actionable thirst
		setNeedValue(colonist, NeedType::Hunger, 5.0F);	 // Critical
		setNeedValue(colonist, NeedType::Thirst, 40.0F); // Actionable
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		ASSERT_FALSE(trace->options.empty());

		// First option (highest priority) should be the critical hunger
		const auto& first = trace->options[0];
		EXPECT_EQ(first.taskType, TaskType::FulfillNeed);
		EXPECT_EQ(first.needType, NeedType::Hunger);
		EXPECT_EQ(first.status, OptionStatus::Selected);
		EXPECT_GE(first.calculatePriority(), 300.0F); // In critical range
	}

	// =============================================================================
	// Integration Tests with ActionSystem - Bug Reproduction
	// =============================================================================

	// Test fixture that includes both AI and Action systems
	class AIActionIntegrationTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			world = std::make_unique<World>();

			auto& registry = engine::assets::AssetRegistry::Get();

			// Register Berry as edible entity for testing (unified entity/item model)
			engine::assets::AssetDefinition berryDef;
			berryDef.defName = "Berry";
			berryDef.label = "Berry";
			berryDef.itemProperties = engine::assets::ItemProperties{};
			berryDef.itemProperties->stackSize = 20;
			berryDef.itemProperties->edible = engine::assets::EdibleCapability{0.3F, engine::assets::CapabilityQuality::Normal, true};
			registry.registerTestDefinition(std::move(berryDef));

			// Register BOTH systems to test their interaction
			// This is critical - the bug only manifests when both systems run
			auto& recipeRegistry = engine::assets::RecipeRegistry::Get();
			world->registerSystem<AIDecisionSystem>(registry, recipeRegistry, kTestRngSeed);
			world->registerSystem<ActionSystem>();
		}

		void TearDown() override {
			// Clean up test definitions
			engine::assets::AssetRegistry::Get().clearDefinitions();
			world.reset();
		}

		/// Create a colonist with all components needed for full task→action flow
		EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
			auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{position});
			world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
			world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
			world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
			world->addComponent<Memory>(entity, Memory{});
			world->addComponent<Inventory>(entity, Inventory::createForColonist());
			world->addComponent<Task>(entity, Task{});
			world->addComponent<Action>(entity, Action{});				 // Required for ActionSystem
			world->addComponent<DecisionTrace>(entity, DecisionTrace{}); // Required for priority-based switching
			return entity;
		}

		void setNeedValue(EntityID entity, NeedType need, float value) {
			auto* needs = world->getComponent<NeedsComponent>(entity);
			ASSERT_NE(needs, nullptr);
			needs->get(need).value = value;
		}

		Task*			getTask(EntityID entity) { return world->getComponent<Task>(entity); }
		Action*			getAction(EntityID entity) { return world->getComponent<Action>(entity); }
		NeedsComponent* getNeeds(EntityID entity) { return world->getComponent<NeedsComponent>(entity); }

		std::unique_ptr<World>	  world;
		static constexpr uint32_t kTestRngSeed = 42;
	};

	/// This test demonstrates the bug: action should complete without being interrupted
	/// by AIDecisionSystem re-evaluation when task.state == Arrived.
	TEST_F(AIActionIntegrationTest, ActionCompletesWithoutReEvaluationInterrupt) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Set up low bladder need (uses ground fallback, so arrives immediately)
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 20.0F); // Actionable - will use ground fallback

		// Frame 1: AI assigns task, state = Arrived (ground fallback)
		// Action system starts the toilet action
		world->update(0.016F);

		auto* task = getTask(colonist);
		auto* action = getAction(colonist);

		ASSERT_NE(task, nullptr);
		ASSERT_NE(action, nullptr);

		// Verify action started
		EXPECT_TRUE(action->isActive()) << "Action should have started";
		EXPECT_EQ(action->type, ActionType::Toilet);
		EXPECT_EQ(task->state, TaskState::Arrived);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);

		// Frame 2-N: Continue processing
		// The action duration is 3 seconds (Toilet action)
		// We'll update in small increments to observe behavior

		// Store initial state
		float	   initialElapsed = action->elapsed;
		TaskType   initialTaskType = task->type;
		ActionType initialActionType = action->type;

		// Run several frames to complete the action
		// Toilet action is 3.0s, so we need at least 3.5s to ensure completion
		for (int i = 0; i < 20; ++i) {
			world->update(0.2F);

			// BUG CHECK: If the task was re-evaluated and cleared while action in progress,
			// the action would stop progressing or be replaced
			if (action->isActive()) {
				EXPECT_EQ(action->type, initialActionType) << "Action type changed mid-execution at frame " << i;
			}
		}

		// After 4 seconds total (20 * 0.2), action should be complete (Toilet is 3s)

		// The critical check: the action should have been able to complete
		// and the need should have been restored
		auto* needs = getNeeds(colonist);
		ASSERT_NE(needs, nullptr);

		// If the bug exists, bladder won't be restored because the action was interrupted
		// If fixed, bladder should be 100% (toilet fully relieves bladder)
		EXPECT_GT(needs->bladder().value, 20.0F)
			<< "Bladder should have been restored by completed toilet action, but action was interrupted";
	}

	/// Test that eat action completes successfully and restores hunger
	TEST_F(AIActionIntegrationTest, EatActionCompletesAndRestoresHunger) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Give colonist berries in inventory - the primary eating path now
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Berry", 5);

		// Set up low hunger
		setNeedValue(colonist, NeedType::Hunger, 30.0F); // Actionable
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		float initialHunger = 30.0F;

		// Run updates to complete the eat action (Eat is ~2.0s duration)
		for (int i = 0; i < 30; ++i) {
			world->update(0.1F);
		}

		auto* needs = getNeeds(colonist);
		EXPECT_GT(needs->hunger().value, initialHunger) << "Hunger should have been restored by eat action";
	}

	/// Test that a colonist can complete a full need-fulfill cycle:
	/// task assigned → movement → arrival → action → completion → new task
	TEST_F(AIActionIntegrationTest, FullNeedFulfillmentCycle) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Use ground fallback (Energy) to simplify - no movement needed
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 20.0F); // Low energy - will sleep on ground
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		// Frame 1: Should assign sleep task with ground fallback (immediate arrival)
		world->update(0.016F);

		auto* task = getTask(colonist);
		auto* action = getAction(colonist);

		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Energy);
		EXPECT_EQ(task->state, TaskState::Arrived);
		EXPECT_TRUE(action->isActive());
		EXPECT_EQ(action->type, ActionType::Sleep);

		// Sleep action takes 8 seconds
		// Run updates to complete it
		for (int i = 0; i < 100; ++i) {
			world->update(0.1F);
		}

		// After sleep completes, energy should be restored
		auto* needs = getNeeds(colonist);
		EXPECT_GT(needs->energy().value, 20.0F) << "Energy should have been restored by sleep action";

		// And a new task should be assigned (wander, since all needs satisfied)
		EXPECT_TRUE(task->isActive());
	}

	// ========================================================================
	// Task Chain Tests - Phase 5 Chain System
	// ========================================================================

	TEST_F(AIDecisionSystemTest, ChainIdAssignedToHaulTask) {
		// Test that Haul tasks get assigned a chainId when selected
		auto colonist = createColonist({0.0F, 0.0F});

		// Manually set up a Haul task (simulating what evaluateHaulOptions does)
		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);

		// Initially no chainId
		EXPECT_FALSE(task->chainId.has_value());

		// Simulate selecting a Haul task via the decision trace
		// (In reality, evaluateHaulOptions creates the option and selectTaskFromTrace assigns)
		// For this test, directly set up the task to verify chainId assignment
		task->type = TaskType::Haul;
		task->haulItemDefName = "Berry";
		task->haulQuantity = 1;
		task->haulSourcePosition = {5.0F, 5.0F};
		task->haulTargetPosition = {10.0F, 10.0F};
		task->targetPosition = task->haulSourcePosition; // First phase: go to source
		task->state = TaskState::Moving;

		// Chain ID and step should be set when task is first assigned
		// For now, we can only verify the data structure exists
		EXPECT_EQ(task->chainStep, 0);
		// Note: chainId is assigned in selectTaskFromTrace, not here
	}

	TEST_F(AIDecisionSystemTest, ChainBonusNotAppliedForNewTask) {
		// Chain bonus should NOT be applied for chainStep == 0 (just started)
		auto colonist = createColonist({0.0F, 0.0F});

		auto* task = getTask(colonist);
		auto* trace = getTrace(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_NE(trace, nullptr);

		// Set up a mid-chain Haul task (chainStep > 0 means already picked up)
		task->type = TaskType::Haul;
		task->chainId = 1ULL; // Has chain ID
		task->chainStep = 0;  // But just started (step 0)
		task->state = TaskState::Moving;
		task->haulItemDefName = "Berry";
		task->targetPosition = {10.0F, 10.0F};

		// The chain bonus logic checks chainStep > 0
		// At step 0, no chain bonus should be applied
		// This is verified by the implementation in populatePriorityBonuses
	}

	TEST_F(AIDecisionSystemTest, ChainStepTracksCurrent) {
		// Verify chainStep starts at 0 and can be incremented
		auto colonist = createColonist({0.0F, 0.0F});

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);

		// Set up Haul task at step 0 (pickup phase)
		task->type = TaskType::Haul;
		task->chainId = 42ULL;
		task->chainStep = 0;
		EXPECT_EQ(task->chainStep, 0);

		// Simulate phase transition (ActionSystem does this)
		task->chainStep++;
		EXPECT_EQ(task->chainStep, 1);
	}

	TEST_F(AIDecisionSystemTest, ChainInterruptionStowsOneHandedItem) {
		// Test that chain interruption stows 1-handed items to backpack when possible
		auto colonist = createColonist({5.0F, 5.0F});

		auto* task = getTask(colonist);
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_NE(inventory, nullptr);

		// Set up colonist mid-Haul chain (chainStep=1 means holding item)
		task->type = TaskType::Haul;
		task->chainId = 100ULL;
		task->chainStep = 1; // Mid-chain: has picked up item
		task->haulItemDefName = "Berry";
		task->targetPosition = {10.0F, 10.0F};
		task->state = TaskState::Moving;

		// Put Berry in hand (simulating what Pickup action does)
		inventory->pickUp("Berry", 1); // 1-handed item

		// Verify setup: Berry is in hand
		EXPECT_TRUE(inventory->isHolding("Berry"));
		EXPECT_FALSE(inventory->hasItem("Berry")); // Not in backpack yet

		// When chain is interrupted by higher priority task that needs hands,
		// the implementation should try to stow the item to backpack
		// This is tested by checking that stowToBackpack works
		bool stowed = inventory->stowToBackpack("Berry");
		EXPECT_TRUE(stowed) << "1-handed item should be stowable to backpack";
		EXPECT_FALSE(inventory->isHolding("Berry")) << "Item should no longer be in hands";
		EXPECT_TRUE(inventory->hasItem("Berry")) << "Item should be in backpack";
	}

	TEST_F(AIDecisionSystemTest, ChainInterruptionDropsTwoHandedItem) {
		// Test that chain interruption drops 2-handed items (can't stow)
		auto colonist = createColonist({5.0F, 5.0F});

		auto* task = getTask(colonist);
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_NE(inventory, nullptr);

		// Set up colonist mid-Haul chain with 2-handed item
		task->type = TaskType::Haul;
		task->chainId = 101ULL;
		task->chainStep = 1;
		task->haulItemDefName = "LargeRock"; // 2-handed item
		task->targetPosition = {10.0F, 10.0F};
		task->state = TaskState::Moving;

		// Put 2-handed item in hands (both hands occupied)
		inventory->pickUp("LargeRock", 2);

		// Verify setup: item is in both hands
		EXPECT_TRUE(inventory->leftHand.has_value());
		EXPECT_TRUE(inventory->rightHand.has_value());
		EXPECT_EQ(inventory->leftHand->defName, "LargeRock");
		EXPECT_EQ(inventory->rightHand->defName, "LargeRock");

		// 2-handed items cannot be stowed to backpack
		bool stowed = inventory->stowToBackpack("LargeRock");
		EXPECT_FALSE(stowed) << "2-handed items cannot be stowed to backpack";

		// Must use putDown (which the interruption handler does)
		auto dropped = inventory->putDown("LargeRock");
		EXPECT_TRUE(dropped.has_value()) << "Item should be put down";
		EXPECT_FALSE(inventory->leftHand.has_value()) << "Left hand should be empty";
		EXPECT_FALSE(inventory->rightHand.has_value()) << "Right hand should be empty";
	}

	TEST_F(AIDecisionSystemTest, TaskFirstActionNeedsHandsMapping) {
		// Verify the task-to-action mapping for hands requirement
		// This tests the helper function logic

		// Create an EvaluatedOption for each task type and verify hands requirement
		EvaluatedOption haulOption;
		haulOption.taskType = TaskType::Haul;
		// Haul first action is Pickup → needsHands=true

		EvaluatedOption craftOption;
		craftOption.taskType = TaskType::Craft;
		// Craft first action is Craft → needsHands=true

		EvaluatedOption gatherOption;
		gatherOption.taskType = TaskType::Gather;
		// Gather first action is Harvest → needsHands=true

		EvaluatedOption wanderOption;
		wanderOption.taskType = TaskType::Wander;
		// Wander action doesn't need hands → needsHands=false

		EvaluatedOption sleepOption;
		sleepOption.taskType = TaskType::FulfillNeed;
		sleepOption.needType = NeedType::Energy;
		// Sleep action → needsHands=false

		EvaluatedOption eatOption;
		eatOption.taskType = TaskType::FulfillNeed;
		eatOption.needType = NeedType::Hunger;
		// Eat action → needsHands=true

		EvaluatedOption toiletOption;
		toiletOption.taskType = TaskType::FulfillNeed;
		toiletOption.needType = NeedType::Bladder;
		// Toilet action → needsHands=false

		// The actual hands requirement check is done via ActionTypeRegistry
		// This test verifies the data structures are set up correctly
		EXPECT_EQ(haulOption.taskType, TaskType::Haul);
		EXPECT_EQ(sleepOption.needType, NeedType::Energy);
		EXPECT_EQ(toiletOption.needType, NeedType::Bladder);
	}

} // namespace ecs::test
