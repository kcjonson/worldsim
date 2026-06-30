// Tests for AIDecisionSystem - Colonist autonomous decision making
// Tests tier-based priority, re-evaluation logic, wander behavior, memory dependency,
// and DecisionTrace generation for task queue display.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Skills.h"
#include "../components/StorageConfiguration.h"
#include "../components/Structure.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include "assets/ActionTypeRegistry.h"
#include "assets/AssetRegistry.h"
#include "assets/PriorityConfig.h"
#include "assets/RecipeRegistry.h"

#include <gtest/gtest.h>

#include <glm/vec2.hpp>

#include <chrono>
#include <cmath>
#include <optional>

namespace ecs::test {

	// Test fixture for AIDecisionSystem tests
	class AIDecisionSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			// Create ECS world
			world = std::make_unique<World>();

			// Goals are global (singleton registry); clear so a Deconstruct goal from one test
			// can't leak into another.
			GoalTaskRegistry::Get().clear();

			// PriorityConfig is a singleton too. These tests rely on its DEFAULT arbitration tiers
			// (installed in the constructor) because they never load priority-tuning.xml. An earlier
			// suite that loads a partial XML now (post the loadFromFile-clears-taskTiers fix) leaves
			// the singleton's tier table empty, so getTaskTier would return the unassigned sentinel
			// here. Reset to defaults so classifyTier sees the real per-task tiers regardless of order.
			engine::assets::PriorityConfig::Get().clear();

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
			GoalTaskRegistry::Get().clear();
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
			uint16_t capabilityMask = static_cast<uint16_t>(1U << static_cast<size_t>(capability));
			memory->rememberWorldEntity(position, defNameId, capabilityMask);
		}

		/// Get the current task for an entity
		Task* getTask(EntityID entity) { return world->getComponent<Task>(entity); }

		/// Drive the private chain-interruption handler directly. This fixture is a friend of
		/// AIDecisionSystem (derived fixtures are not), so the seam lives here.
		void callHandleChainInterruption(
			EntityID entity, const Task& task, Inventory& inventory, const Position& position,
			TaskType newTaskType, NeedType newNeedType
		) {
			world->getSystem<AIDecisionSystem>().handleChainInterruption(
				entity, task, inventory, position, newTaskType, newNeedType
			);
		}

		/// Read the AI system's colony-origin fallback target. The fixture is a friend of
		/// AIDecisionSystem (derived TEST_F classes are not), so the seam lives here.
		std::optional<glm::vec2> getColonyOrigin() {
			return world->getSystem<AIDecisionSystem>().m_colonyOrigin;
		}

		/// Get the movement target for an entity
		MovementTarget* getMovementTarget(EntityID entity) { return world->getComponent<MovementTarget>(entity); }

		/// Get the needs component for an entity
		NeedsComponent* getNeeds(EntityID entity) { return world->getComponent<NeedsComponent>(entity); }

		/// Build a storage box entity: StorageConfiguration (one rule for `item` at `priority` with the
		/// given min/max) + Inventory (createForStorage, pre-filled with `initialQty`) + Position +
		/// Appearance (defName `boxDef`, a registered Storage def so colonyKnowsStorageEntity can key it).
		EntityID makeStorageBox(
			glm::vec2 pos, const std::string& boxDef, const std::string& item, ecs::StoragePriority priority,
			uint32_t initialQty = 0, uint32_t minAmount = 0, uint32_t maxAmount = 0
		) {
			auto box = world->createEntity();
			world->addComponent<Position>(box, Position{pos});
			world->addComponent<Appearance>(box, Appearance{boxDef, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});

			auto inv = Inventory::createForStorage();
			if (initialQty > 0) {
				inv.addItem(item, initialQty);
			}
			world->addComponent<Inventory>(box, std::move(inv));

			ecs::StorageConfiguration config;
			config.addRule(ecs::StorageRule{
				.defName = item,
				.category = engine::assets::ItemCategory::RawMaterial,
				.priority = priority,
				.minAmount = minAmount,
				.maxAmount = maxAmount,
			});
			world->addComponent<ecs::StorageConfiguration>(box, std::move(config));
			return box;
		}

		/// Create the ordinary umbrella Haul goal (StorageGoalSystem-owned, NO chainId) that a storage
		/// box raises when it wants items. This is the goal whose evaluation runs the pull scan.
		uint64_t makeUmbrellaGoal(EntityID destBox, glm::vec2 destPos, uint32_t itemDefNameId) {
			GoalTask haul;
			haul.type = TaskType::Haul;
			haul.owner = GoalOwner::StorageGoalSystem;
			haul.destinationEntity = destBox;
			haul.destinationPosition = destPos;
			haul.acceptedDefNameIds = {itemDefNameId};
			haul.targetAmount = 10;
			haul.status = GoalStatus::Available;
			return GoalTaskRegistry::Get().createGoal(std::move(haul));
		}

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

		// Verify options are sorted by the (tier, score) key: each option is >= the next, i.e. the
		// next is never strictly higher priority than the previous.
		for (size_t i = 1; i < trace->options.size(); ++i) {
			const auto& prev = trace->options[i - 1];
			const auto& curr = trace->options[i];
			EXPECT_FALSE(EvaluatedOption::higherPriority(curr, prev)) << "Options not sorted at index " << i;
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
	// (tier, score) arbitration key tests
	//
	// These pin the KEY semantics on bare EvaluatedOptions: tier is set directly (the system's
	// classifyTier() is .cpp-private). Classification itself is exercised end-to-end by the
	// "...ClassifiesTier..." integration tests further down, which build a real DecisionTrace through
	// world->update() and assert the tier the system stamped on the resulting option.
	// The tier numbers mirror the spec ladder: 2 critical need, 4 active work order, 5 actionable
	// need, 6 opportunistic work, 7 idle. score = distanceFactor + skill + age + hysteresis.
	// =============================================================================

	namespace {
		// Mirror PriorityConfig's default DistanceFactor curve so these unit tests can build scores
		// the same way the system does: factor(d) = maxFactor * max(0, 1 - d/maxDistance).
		constexpr float kDistMaxFactor = 300.0F;
		constexpr float kDistMaxDistance = 60.0F;
		[[nodiscard]] float distanceFactorAt(float d) {
			if (d <= 0.0F) {
				return kDistMaxFactor;
			}
			if (d >= kDistMaxDistance) {
				return 0.0F;
			}
			return kDistMaxFactor * (1.0F - d / kDistMaxDistance);
		}
	} // namespace

	// Tier dominance: a tier-4 active-work-order option, even FAR away (low score), beats a tier-5
	// actionable-need option that is RIGHT HERE (max score). This is the categorical guarantee the
	// floors used to fake -- now it falls out of the key, no floor constant needed.
	TEST_F(AIDecisionSystemTest, TierDominanceWorkOrderBeatsNearerNeed) {
		EvaluatedOption farWorkOrder;
		farWorkOrder.taskType = TaskType::Build;
		farWorkOrder.status = OptionStatus::Available;
		farWorkOrder.tier = 4;					// active work order
		farWorkOrder.distanceFactor = distanceFactorAt(59.0F); // ~5: nearly out of range
		farWorkOrder.skillBonus = 0;
		farWorkOrder.score = farWorkOrder.computeScore();

		EvaluatedOption nearNeed;
		nearNeed.taskType = TaskType::FulfillNeed;
		nearNeed.needType = NeedType::Hunger;
		nearNeed.status = OptionStatus::Available;
		nearNeed.tier = 5;								   // actionable need
		nearNeed.distanceFactor = distanceFactorAt(0.0F); // 300: right here
		nearNeed.taskAgeBonus = 100;					   // pile on every within-tier term
		nearNeed.score = nearNeed.computeScore();

		EXPECT_LT(farWorkOrder.score, nearNeed.score) << "sanity: the work order has the LOWER score";
		EXPECT_TRUE(EvaluatedOption::higherPriority(farWorkOrder, nearNeed))
			<< "tier 4 must beat tier 5 regardless of score (no floor needed)";
		EXPECT_FALSE(EvaluatedOption::higherPriority(nearNeed, farWorkOrder));
	}

	// Opportunistic work can't preempt a work order: a tier-6 haul with a huge skill bonus cannot
	// outrank any tier-4 option, even a far/zero-skill one.
	TEST_F(AIDecisionSystemTest, OpportunisticCannotPreemptWorkOrder) {
		EvaluatedOption opportunisticHaul;
		opportunisticHaul.taskType = TaskType::Haul;
		opportunisticHaul.status = OptionStatus::Available;
		opportunisticHaul.tier = 6;								  // opportunistic
		opportunisticHaul.distanceFactor = distanceFactorAt(0.0F); // right here
		opportunisticHaul.skillBonus = 100;						  // max skill
		opportunisticHaul.taskAgeBonus = 100;					  // max age
		opportunisticHaul.score = opportunisticHaul.computeScore();

		EvaluatedOption farWorkOrder;
		farWorkOrder.taskType = TaskType::Craft;
		farWorkOrder.status = OptionStatus::Available;
		farWorkOrder.tier = 4;
		farWorkOrder.distanceFactor = distanceFactorAt(55.0F); // far
		farWorkOrder.score = farWorkOrder.computeScore();

		EXPECT_TRUE(EvaluatedOption::higherPriority(farWorkOrder, opportunisticHaul))
			<< "tier 4 work order outranks tier 6 opportunistic haul regardless of score";
		EXPECT_FALSE(EvaluatedOption::higherPriority(opportunisticHaul, farWorkOrder));
	}

	// Within a tier, the nearest source wins even when a farther one has the max skill bonus. This
	// is the "chops the adjacent tree, not the one 70 m away" guarantee: the distance factor
	// dominates skill across the working range.
	TEST_F(AIDecisionSystemTest, WithinTierNearestDominatesSkill) {
		EvaluatedOption nearHarvest;
		nearHarvest.taskType = TaskType::Harvest;
		nearHarvest.status = OptionStatus::Available;
		nearHarvest.tier = 6;
		nearHarvest.distanceToTarget = 3.0F;
		nearHarvest.distanceFactor = distanceFactorAt(3.0F);
		nearHarvest.skillBonus = 0; // unskilled
		nearHarvest.score = nearHarvest.computeScore();

		EvaluatedOption farSkilledHarvest;
		farSkilledHarvest.taskType = TaskType::Harvest;
		farSkilledHarvest.status = OptionStatus::Available;
		farSkilledHarvest.tier = 6;
		farSkilledHarvest.distanceToTarget = 70.0F; // out of range -> factor 0
		farSkilledHarvest.distanceFactor = distanceFactorAt(70.0F);
		farSkilledHarvest.skillBonus = 100; // max skill
		farSkilledHarvest.taskAgeBonus = 100;
		farSkilledHarvest.score = farSkilledHarvest.computeScore();

		EXPECT_TRUE(EvaluatedOption::higherPriority(nearHarvest, farSkilledHarvest))
			<< "nearest same-tier source wins even against max skill+age on a far one";
	}

	// Hysteresis: the in-progress option is NOT dropped for a same-tier challenger with a tiny score
	// advantage, but a clearly-closer same-tier target DOES overcome the hysteresis margin. The
	// in-progress option carries +hysteresisBonus (= taskSwitchThreshold, 50).
	TEST_F(AIDecisionSystemTest, HysteresisResistsThrashButYieldsToClearlyCloser) {
		constexpr int16_t kHysteresis = 50; // = priority-tuning InProgress bonus / taskSwitchThreshold

		// In-progress harvest at 30 m, with the stickiness margin applied. The distance curve is
		// 5 score/m (300 over 60 m), so the 50 margin equals ~10 m of distance: a challenger must be
		// at least ~10 m closer than the in-progress target to overcome it.
		EvaluatedOption inProgress;
		inProgress.taskType = TaskType::Harvest;
		inProgress.status = OptionStatus::Available;
		inProgress.tier = 6;
		inProgress.distanceFactor = distanceFactorAt(30.0F); // 150
		inProgress.hysteresisBonus = kHysteresis;
		inProgress.score = inProgress.computeScore(); // 200

		// A marginally closer challenger (29 m, ~5 score gain) with NO stickiness: the gain is far
		// below the 50 margin, so it must NOT win.
		EvaluatedOption tinyBetter;
		tinyBetter.taskType = TaskType::Harvest;
		tinyBetter.status = OptionStatus::Available;
		tinyBetter.tier = 6;
		tinyBetter.distanceFactor = distanceFactorAt(29.0F); // 155
		tinyBetter.tiebreakId = 1;							  // distinct id; must still lose on score
		tinyBetter.score = tinyBetter.computeScore();

		EXPECT_TRUE(EvaluatedOption::higherPriority(inProgress, tinyBetter))
			<< "a tiny same-tier improvement must not overcome the hysteresis margin";

		// A clearly closer challenger (right here, 0 m, factor 300): 30 m closer, a gain (150) well
		// past the 50 margin, so the colonist DOES switch within a re-eval.
		EvaluatedOption clearlyCloser;
		clearlyCloser.taskType = TaskType::Harvest;
		clearlyCloser.status = OptionStatus::Available;
		clearlyCloser.tier = 6;
		clearlyCloser.distanceFactor = distanceFactorAt(0.0F); // 300
		clearlyCloser.tiebreakId = 2;
		clearlyCloser.score = clearlyCloser.computeScore();

		EXPECT_GT(clearlyCloser.score, inProgress.score)
			<< "sanity: the closer target's score beats the in-progress (hysteresis-boosted) score";
		EXPECT_TRUE(EvaluatedOption::higherPriority(clearlyCloser, inProgress))
			<< "a clearly-closer same-tier target overcomes hysteresis";
	}

	// Tiebreak determinism: equal (tier, score) resolves on tiebreakId ascending (the multiplayer
	// desync guard), never on container order.
	TEST_F(AIDecisionSystemTest, EqualKeyBreaksTiesOnTiebreakId) {
		EvaluatedOption a;
		a.taskType = TaskType::Harvest;
		a.tier = 6;
		a.distanceFactor = distanceFactorAt(5.0F);
		a.score = a.computeScore();
		a.tiebreakId = 10;

		EvaluatedOption b = a; // identical tier+score
		b.tiebreakId = 20;

		EXPECT_TRUE(EvaluatedOption::higherPriority(a, b)) << "lower tiebreakId wins an exact tie";
		EXPECT_FALSE(EvaluatedOption::higherPriority(b, a));
	}

	// Within the idle tier (7), a reachable gather-food option must outrank wander. Wander never sets
	// distanceToTarget, so before the fix its distance factor computed to the MAX (~300) and it beat
	// gather-food; the fix pins wander's within-tier score to 0 (the idle floor). A gather-food
	// sentinel (needValue>=100, threshold==0) with a nearby reachable source carries a positive
	// distance factor plus any Farming skill, so it wins the (tier, score) key.
	TEST_F(AIDecisionSystemTest, GatherFoodRanksAboveWanderInIdleTier) {
		// Wander after the fix: tier 7, score 0 (no distance/skill/age/hysteresis).
		EvaluatedOption wander;
		wander.taskType = TaskType::Wander;
		wander.status = OptionStatus::Available;
		wander.tier = 7;
		wander.score = 0.0F; // the idle floor

		// Gather-food sentinel: tier 7, a nearby (5 m) reachable food source plus modest Farming skill.
		EvaluatedOption gatherFood;
		gatherFood.taskType = TaskType::FulfillNeed;
		gatherFood.needType = NeedType::Hunger;
		gatherFood.needValue = 100.0F; // sentinel marker (not a real hunger need)
		gatherFood.threshold = 0.0F;   // sentinel marker
		gatherFood.status = OptionStatus::Available;
		gatherFood.tier = 7;
		gatherFood.distanceToTarget = 5.0F;
		gatherFood.distanceFactor = distanceFactorAt(5.0F); // ~275
		gatherFood.skillBonus = 20;							// some Farming skill
		gatherFood.score = gatherFood.computeScore();

		EXPECT_GT(gatherFood.score, wander.score) << "reachable gather-food scores above the wander floor";
		EXPECT_TRUE(EvaluatedOption::higherPriority(gatherFood, wander))
			<< "gather-food wins the (tier, score) key over wander within tier 7";
		EXPECT_FALSE(EvaluatedOption::higherPriority(wander, gatherFood));
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

		// First option (highest priority) should be the critical hunger, classified at tier 2.
		const auto& first = trace->options[0];
		EXPECT_EQ(first.taskType, TaskType::FulfillNeed);
		EXPECT_EQ(first.needType, NeedType::Hunger);
		EXPECT_EQ(first.status, OptionStatus::Selected);
		EXPECT_EQ(first.tier, 2) << "a critical need (<10%) is tier 2";
	}

	// =============================================================================
	// classifyTier() integration tests
	//
	// These build a REAL DecisionTrace through world->update() (the only path that calls the
	// .cpp-private classifyTier on every option) and assert the tier the system stamped onto the
	// resulting option. They cover each runtime classification rule from the spec Model table:
	// work-order Haul/Harvest -> 4, stocking Haul/Harvest -> 6, gather-food sentinel -> 7 (above
	// wander), critical need -> 2, actionable need -> 5.
	// =============================================================================

	namespace {
		// Find the first option matching a predicate in a trace (nullptr if none).
		template <typename Pred>
		[[nodiscard]] const EvaluatedOption* findOption(const DecisionTrace& trace, Pred pred) {
			for (const auto& option : trace.options) {
				if (pred(option)) {
					return &option;
				}
			}
			return nullptr;
		}

		// Register a tool-less harvestable whose yield is a light, one-hand carryable RawMaterial, so
		// a non-overweight colonist that remembers it produces an Available Harvest option (both the
		// tool gate and the carry gate pass). Returns {harvestableId, yieldId}.
		struct HarvestableIds {
			uint32_t harvestableId;
			uint32_t yieldId;
		};
		[[nodiscard]] HarvestableIds registerHarvestableYielding(
			engine::assets::AssetRegistry& registry, const std::string& harvestableDef, const std::string& yieldDef
		) {
			engine::assets::AssetDefinition yield;
			yield.defName = yieldDef;
			yield.label = yieldDef;
			yield.handsRequired = 1;
			yield.category = engine::assets::ItemCategory::RawMaterial;
			yield.capabilities.carryable = engine::assets::CarryableCapability{1};
			yield.itemProperties = engine::assets::ItemProperties{};
			yield.itemProperties->stackSize = 100;
			yield.itemProperties->massKg = 0.1F; // light: carry weight never binds the gate
			registry.registerTestDefinition(std::move(yield));

			engine::assets::AssetDefinition harvestable;
			harvestable.defName = harvestableDef;
			harvestable.label = harvestableDef;
			harvestable.capabilities.harvestable = engine::assets::HarvestableCapability{};
			harvestable.capabilities.harvestable->yieldDefName = yieldDef;
			harvestable.capabilities.harvestable->requiredToolType = ""; // tool-less: gate passes
			registry.registerTestDefinition(std::move(harvestable));

			return {registry.getDefNameId(harvestableDef), registry.getDefNameId(yieldDef)};
		}

		void satisfyAllNeeds(NeedsComponent& needs) {
			needs.get(NeedType::Hunger).value = 100.0F;
			needs.get(NeedType::Thirst).value = 100.0F;
			needs.get(NeedType::Energy).value = 100.0F;
			needs.get(NeedType::Bladder).value = 100.0F;
			needs.get(NeedType::Digestion).value = 100.0F;
		}
	} // namespace

	// A Harvest that provisions an active work order (its parent goal is a Craft) classifies at
	// tier 4. This is the chain-becomes-tier-lock: servesActiveWorkOrder -> tier 4, no score bonus.
	TEST_F(AIDecisionSystemTest, WorkOrderHarvestClassifiesTier4) {
		auto& registry = engine::assets::AssetRegistry::Get();
		auto [bushId, stickId] = registerHarvestableYielding(registry, "Flora_WorkBush", "WorkStick");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, {2.0F, 0.0F}, bushId, engine::assets::CapabilityType::Harvestable);

		const auto station = world->createEntity();
		world->addComponent<Position>(station, Position{{4.0F, 0.0F}});

		// Parent Craft goal + its child Harvest goal (the provisioning step).
		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.destinationPosition = {4.0F, 0.0F};
		craft.targetAmount = 2;
		craft.status = GoalStatus::Blocked;
		const uint64_t craftId = GoalTaskRegistry::Get().createGoal(std::move(craft));

		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::CraftingGoalSystem;
		harvest.destinationEntity = station;
		harvest.destinationPosition = {4.0F, 0.0F};
		harvest.yieldDefNameId = stickId;
		harvest.targetAmount = 2;
		harvest.parentGoalId = craftId;
		harvest.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(harvest));

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* harvestOption = findOption(*trace, [](const EvaluatedOption& o) { return o.taskType == TaskType::Harvest; });
		ASSERT_NE(harvestOption, nullptr) << "a work-order harvest option should be present";
		EXPECT_TRUE(harvestOption->servesActiveWorkOrder) << "harvest feeding a Craft serves the work order";
		EXPECT_EQ(harvestOption->tier, 4) << "a work-order harvest classifies at tier 4";
	}

	// A stocking Harvest (owned by StorageGoalSystem, no Craft/Build parent) is opportunistic work:
	// it classifies at tier 6, strictly below work orders.
	TEST_F(AIDecisionSystemTest, StockingHarvestClassifiesTier6) {
		auto& registry = engine::assets::AssetRegistry::Get();
		auto [bushId, stickId] = registerHarvestableYielding(registry, "Flora_StockBush", "StockStick");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, {2.0F, 0.0F}, bushId, engine::assets::CapabilityType::Harvestable);

		auto storage = world->createEntity();
		world->addComponent<Position>(storage, Position{{4.0F, 0.0F}});

		// A stocking harvest: StorageGoalSystem chops to fill a box toward its minimum. No Craft/Build
		// parent, so servesActiveWorkOrder is false and it stays at the opportunistic base tier.
		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::StorageGoalSystem;
		harvest.destinationEntity = storage;
		harvest.destinationPosition = {4.0F, 0.0F};
		harvest.yieldDefNameId = stickId;
		harvest.targetAmount = 5;
		harvest.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(harvest));

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* harvestOption = findOption(*trace, [](const EvaluatedOption& o) { return o.taskType == TaskType::Harvest; });
		ASSERT_NE(harvestOption, nullptr) << "a stocking harvest option should be present";
		EXPECT_FALSE(harvestOption->servesActiveWorkOrder) << "stocking is not a work order";
		EXPECT_TRUE(harvestOption->servesStorageStocking);
		EXPECT_EQ(harvestOption->tier, 6) << "a stocking harvest classifies at tier 6 (opportunistic)";
	}

	// =============================================================================
	// Storage-to-storage pull (storage priority, Phase 2)
	// =============================================================================
	// A higher-priority destination box pulls items UP from strictly-lower-priority boxes. The
	// helpers below register a one-hand carryable item and build storage boxes (StorageConfiguration
	// + Inventory + Position + Appearance) whose Appearance defName resolves so colonyKnowsStorageEntity
	// can key the box in memory.

	namespace {
		// Register a light one-hand carryable RawMaterial; returns its defNameId. Light so carry
		// weight never binds the pull's per-trip clamp.
		[[nodiscard]] uint32_t registerPullItem(engine::assets::AssetRegistry& registry, const std::string& defName) {
			engine::assets::AssetDefinition item;
			item.defName = defName;
			item.label = defName;
			item.handsRequired = 1;
			item.category = engine::assets::ItemCategory::RawMaterial;
			item.capabilities.carryable = engine::assets::CarryableCapability{1};
			item.itemProperties = engine::assets::ItemProperties{};
			item.itemProperties->stackSize = 100;
			item.itemProperties->massKg = 0.1F;
			registry.registerTestDefinition(std::move(item));
			return registry.getDefNameId(defName);
		}

		// Register a storage container asset def (Storage capability). One def is shared by all boxes
		// in a test; its defName is what colonyKnowsStorageEntity resolves from a box's Appearance.
		void registerStorageDef(engine::assets::AssetRegistry& registry, const std::string& defName) {
			engine::assets::AssetDefinition box;
			box.defName = defName;
			box.label = defName;
			box.capabilities.storage = engine::assets::StorageCapability{};
			registry.registerTestDefinition(std::move(box));
		}

		// Find the storage->storage pull option (a Haul with a source box set) in a trace.
		[[nodiscard]] const EvaluatedOption* findPullOption(const DecisionTrace& trace) {
			return findOption(trace, [](const EvaluatedOption& o) {
				return o.taskType == TaskType::Haul && o.haulSourceStorageId != 0;
			});
		}
	} // namespace

	// 1. Pull from lower: a Low box holds Wood, a High box wants Wood -> a pull option sourced from the
	//    Low box, targeting the High box.
	TEST_F(AIDecisionSystemTest, PullFromLowerPriorityBox) {
		auto& registry = engine::assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 lowPos{2.0F, 0.0F};
		const glm::vec2 highPos{4.0F, 0.0F};
		const EntityID lowBox = makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10);
		const EntityID highBox = makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, lowPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, highPos, boxDefId, engine::assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);

		world->update(0.016F);

		const auto* pull = findPullOption(*getTrace(colonist));
		ASSERT_NE(pull, nullptr) << "a high box should pull Wood from a low box";
		EXPECT_EQ(pull->haulSourceStorageId, static_cast<uint64_t>(lowBox));
		EXPECT_EQ(pull->haulTargetStorageId, static_cast<uint64_t>(highBox));
		EXPECT_TRUE(pull->servesStorageStocking);
	}

	// 2. Never from same: two Medium boxes both holding Wood -> NO pull between them (strict-< gate).
	TEST_F(AIDecisionSystemTest, NoPullBetweenSamePriorityBoxes) {
		auto& registry = engine::assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 aPos{2.0F, 0.0F};
		const glm::vec2 bPos{4.0F, 0.0F};
		makeStorageBox(aPos, "Box", "Wood", ecs::StoragePriority::Medium, /*qty=*/10);
		const EntityID boxB = makeStorageBox(bPos, "Box", "Wood", ecs::StoragePriority::Medium, /*qty=*/10);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, aPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, bPos, boxDefId, engine::assets::CapabilityType::Storage);

		makeUmbrellaGoal(boxB, bPos, woodId);

		world->update(0.016F);

		EXPECT_EQ(findPullOption(*getTrace(colonist)), nullptr)
			<< "equal priority must not pull (strict-< gate)";
	}

	// 3. Never from higher: a High box holds Wood, a Low box wants Wood -> no pull from High into Low.
	TEST_F(AIDecisionSystemTest, NoPullFromHigherPriorityBox) {
		auto& registry = engine::assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 highPos{2.0F, 0.0F};
		const glm::vec2 lowPos{4.0F, 0.0F};
		makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/10);
		const EntityID lowBox = makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, highPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, lowPos, boxDefId, engine::assets::CapabilityType::Storage);

		makeUmbrellaGoal(lowBox, lowPos, woodId);

		world->update(0.016F);

		EXPECT_EQ(findPullOption(*getTrace(colonist)), nullptr)
			<< "a low box must not pull from a higher box";
	}

	// 4. Conflict, higher dest wins: a Low box holds Wood; a High box and a Critical box both want it.
	//    The selected option's destination is the Critical box (storagePriorityBias breaks the tie
	//    within tier 6), deterministically.
	TEST_F(AIDecisionSystemTest, PullConflictHigherDestinationWins) {
		auto& registry = engine::assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 lowPos{2.0F, 0.0F};
		const glm::vec2 highPos{4.0F, 0.0F};
		const glm::vec2 critPos{6.0F, 0.0F};
		const EntityID lowBox = makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10);
		const EntityID highBox = makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High);
		const EntityID critBox = makeStorageBox(critPos, "Box", "Wood", ecs::StoragePriority::Critical);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, lowPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, highPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, critPos, boxDefId, engine::assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);
		makeUmbrellaGoal(critBox, critPos, woodId);

		world->update(0.016F);

		const auto* selected = getTrace(colonist)->getSelected();
		ASSERT_NE(selected, nullptr);
		EXPECT_EQ(selected->haulSourceStorageId, static_cast<uint64_t>(lowBox));
		EXPECT_EQ(selected->haulTargetStorageId, static_cast<uint64_t>(critBox))
			<< "the Critical destination outranks the High one within tier 6";
	}

	// 5. Don't-drain-below-min: source Low box holds 10 Wood with its OWN rule min=8 -> pull qty == 2.
	TEST_F(AIDecisionSystemTest, PullDoesNotDrainSourceBelowItsMin) {
		auto& registry = engine::assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 lowPos{2.0F, 0.0F};
		const glm::vec2 highPos{4.0F, 0.0F};
		makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10, /*min=*/8);
		const EntityID highBox = makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, lowPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, highPos, boxDefId, engine::assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);

		world->update(0.016F);

		const auto* pull = findPullOption(*getTrace(colonist));
		ASSERT_NE(pull, nullptr);
		EXPECT_EQ(pull->haulQuantity, 2U) << "10 held - 8 source min = 2 drainable";
	}

	// 6. Don't-overfill: dest High box max=15 already holds 10; source Low box holds 10 -> pull qty == 5.
	TEST_F(AIDecisionSystemTest, PullDoesNotOverfillDestinationMax) {
		auto& registry = engine::assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 lowPos{2.0F, 0.0F};
		const glm::vec2 highPos{4.0F, 0.0F};
		makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10);
		const EntityID highBox =
			makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/10, /*min=*/0, /*max=*/15);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, lowPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, highPos, boxDefId, engine::assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);

		world->update(0.016F);

		const auto* pull = findPullOption(*getTrace(colonist));
		ASSERT_NE(pull, nullptr);
		EXPECT_EQ(pull->haulQuantity, 5U) << "dest max 15 - 10 already held = 5 room";
	}

	// 7. Within-tier ordering: a Critical-dest pull and a Low-dest pull -> both at tier 6, the
	//    Critical-dest pull has the higher score and is Selected.
	TEST_F(AIDecisionSystemTest, PullCriticalDestinationOutscoresLowWithinTier6) {
		auto& registry = engine::assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		// One shared Low source feeds BOTH a Medium dest and a Critical dest (Low < both, so each is a
		// legal pull). Both pulls are tier 6; the Critical destination's bias gives it the higher score.
		const glm::vec2 lowSrcPos{2.0F, 0.0F};
		const glm::vec2 medDestPos{4.0F, 0.0F};
		const glm::vec2 critDestPos{6.0F, 0.0F};
		makeStorageBox(lowSrcPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10);
		const EntityID medBox = makeStorageBox(medDestPos, "Box", "Wood", ecs::StoragePriority::Medium);
		const EntityID critBox = makeStorageBox(critDestPos, "Box", "Wood", ecs::StoragePriority::Critical);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, lowSrcPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, medDestPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, critDestPos, boxDefId, engine::assets::CapabilityType::Storage);

		makeUmbrellaGoal(medBox, medDestPos, woodId);
		makeUmbrellaGoal(critBox, critDestPos, woodId);

		world->update(0.016F);

		const auto* medPull = findOption(*getTrace(colonist), [medBox](const EvaluatedOption& o) {
			return o.haulSourceStorageId != 0 && o.haulTargetStorageId == static_cast<uint64_t>(medBox);
		});
		const auto* critPull = findOption(*getTrace(colonist), [critBox](const EvaluatedOption& o) {
			return o.haulSourceStorageId != 0 && o.haulTargetStorageId == static_cast<uint64_t>(critBox);
		});
		ASSERT_NE(medPull, nullptr);
		ASSERT_NE(critPull, nullptr);
		EXPECT_EQ(medPull->tier, 6) << "stocking pulls stay at tier 6";
		EXPECT_EQ(critPull->tier, 6) << "stocking pulls stay at tier 6";
		EXPECT_GT(critPull->score, medPull->score) << "Critical destination outscores Medium within the tier";
		EXPECT_EQ(getTrace(colonist)->getSelected()->haulTargetStorageId, static_cast<uint64_t>(critBox));
	}

	// 8. Discovery-gating: the source box is NOT in any colonist's memory -> no pull option (no god-view).
	TEST_F(AIDecisionSystemTest, PullRequiresColonyToKnowSourceBox) {
		auto& registry = engine::assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 lowPos{2.0F, 0.0F};
		const glm::vec2 highPos{4.0F, 0.0F};
		makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10);
		const EntityID highBox = makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		// Knows the DESTINATION box only; the source Low box is deliberately unknown.
		addKnownEntity(colonist, highPos, boxDefId, engine::assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);

		world->update(0.016F);

		EXPECT_EQ(findPullOption(*getTrace(colonist)), nullptr)
			<< "an unknown source box must not be pulled from (no magic discovery)";
	}

	// 9. Two-hand carry holds against a fresh same-tier pull (the drop-loop regression). A pull is a
	//    two-phase Haul: walk to SOURCE box A, Withdraw the armful, then carry to DEST box B and
	//    deposit. Mid-carry (phase 2, target B, the two-hand Wood riding in both hands), the colonist
	//    is still standing on A which still holds Wood, so evaluateHaulOptions' pull-source scan
	//    re-emits a FRESH withdraw option pointing back at A with a high score (distance ~0). The
	//    gate's isSameTask compares the in-flight deposit target (B) to the fresh option target (A) ->
	//    different -> would treat it as a same-tier switch and drop the un-stowable armful, then
	//    withdraw + drop again forever. The two-hand-carry guard must HOLD the delivery so it
	//    completes. Real two-hand Wood; this exercises the re-eval mid-carry the action round-trip
	//    test (WithdrawTwoHandWoodRidesInHandsThenDepositsIntoDestBox) skipped.
	TEST_F(AIDecisionSystemTest, TwoHandPullHoldsAgainstFreshSamePriorityPullMidCarry) {
		auto& registry = engine::assets::AssetRegistry::Get();
		// A REAL two-hand bulk material (handsRequired==2): rides in both hands, can't stow to
		// belt/backpack, so an interruption would DROP it. registerPullItem registers a one-hand item,
		// so build Wood explicitly here.
		engine::assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = engine::assets::ItemCategory::RawMaterial;
		woodDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		woodDef.itemProperties = engine::assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 2.5F;
		registry.registerTestDefinition(std::move(woodDef));
		const uint32_t woodId = registry.getDefNameId("Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		// Source A (Low) still holds Wood -> the pull-source scan keeps re-emitting a source-A option.
		// Dest B (High) wants Wood. Repro mirrors the in-game report (A Low 30, B High min 20).
		const glm::vec2 srcPos{2.0F, 0.0F};
		const glm::vec2 destPos{4.0F, 0.0F};
		const EntityID srcBox = makeStorageBox(srcPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/30);
		const EntityID destBox = makeStorageBox(destPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/0, /*min=*/20);

		// Colonist standing ON the source box, mid-carry: a two-hand Wood armful in hands, task on its
		// deposit leg (phase 2) heading to the dest box. priorityTier 6 = the opportunistic-stocking
		// tier the original pull selection stamped (a same-tier challenger must NOT preempt it).
		auto  colonist = createColonist(srcPos);
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, srcPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, destPos, boxDefId, engine::assets::CapabilityType::Storage);

		const uint64_t haulId = makeUmbrellaGoal(destBox, destPos, woodId);

		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		const uint32_t carried = ecs::addArmful(*inventory, registry, "Wood", 14); // the in-game two-hand load
		ASSERT_EQ(carried, 14U) << "seat the full armful into both hands";
		ASSERT_TRUE(inventory->isHolding("Wood"));

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		task->type = TaskType::Haul;
		task->state = TaskState::Moving; // in-flight toward the dest box (phase 2)
		task->navState = NavState::Traveling; // has a believed route (NOT CantFindWayTo) -> guard applies
		task->haulGoalId = haulId;
		task->haulItemDefName = "Wood";
		task->haulQuantity = 14;
		task->haulSourceStorageId = static_cast<uint64_t>(srcBox);
		task->haulSourcePosition = srcPos;
		task->haulTargetStorageId = static_cast<uint64_t>(destBox);
		task->haulTargetPosition = destPos;
		task->haulFromInventory = false;
		task->targetPosition = destPos; // the deposit leg targets B; the fresh pull option targets A
		task->chainId = haulId;
		task->chainStep = 1; // past the Withdraw leg
		task->priorityTier = 6; // opportunistic stocking tier
		task->timeSinceEvaluation = 1.0F; // > kReEvalInterval (0.5) -> force a re-eval this tick

		world->update(0.016F);

		// The bug's trigger must actually be present: a fresh pull option still sourced from box A.
		const auto* freshPull = findPullOption(*getTrace(colonist));
		ASSERT_NE(freshPull, nullptr) << "the pull-source scan re-emits a source-A option mid-carry (the trigger)";
		EXPECT_EQ(freshPull->haulSourceStorageId, static_cast<uint64_t>(srcBox));
		ASSERT_TRUE(freshPull->targetPosition.has_value());
		EXPECT_FLOAT_EQ(freshPull->targetPosition->x, srcPos.x)
			<< "the fresh option targets the SOURCE (A), differing from the in-flight deposit target (B)";

		// The colonist must HOLD the in-flight delivery: still hauling the same Wood to box B, with the
		// armful still in hands. No switch, no drop loop.
		EXPECT_EQ(task->type, TaskType::Haul) << "stays on the haul (did not switch tasks)";
		EXPECT_EQ(task->haulTargetStorageId, static_cast<uint64_t>(destBox)) << "still delivering to the dest box";
		EXPECT_FLOAT_EQ(task->targetPosition.x, destPos.x) << "still targeting the deposit leg, not re-pointed at A";
		EXPECT_TRUE(inventory->isHolding("Wood")) << "the two-hand armful stayed in hands (not dropped)";
		EXPECT_EQ(ecs::handHeldQuantity(*inventory, "Wood"), 14U) << "the full armful is intact -- nothing dropped";
	}

	// 9b. Two-hand carry holds against idle Wander when no pull is left (the second-haul abandonment).
	//     Same mid-carry setup as #9, but the source box is drained to its OWN min so the pull-source
	//     scan offers NOTHING. With needs met and no haul option, the top option is Wander (tier 7).
	//     The old guard only blocked a same-tier *Haul*, so Wander -- not a Haul -- slipped past and the
	//     colonist walked off carrying the un-stowable armful indefinitely (dest box stalls, Wood
	//     stranded in hands). The broadened guard (ANY same-or-lower-priority challenger) must HOLD the
	//     delivery against Wander so the deposit completes.
	TEST_F(AIDecisionSystemTest, TwoHandPullHoldsAgainstWanderWhenSourceDrainedMidCarry) {
		auto& registry = engine::assets::AssetRegistry::Get();
		// REAL two-hand Wood (handsRequired==2): rides in both hands, can't stow -> a switch drops it.
		engine::assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = engine::assets::ItemCategory::RawMaterial;
		woodDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		woodDef.itemProperties = engine::assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 2.5F;
		registry.registerTestDefinition(std::move(woodDef));
		const uint32_t woodId = registry.getDefNameId("Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		// Source A (Low) is at its OWN min (held == minAmount == 8) -> 0 drainable -> NO pull offered.
		// Dest B (High) still wants Wood. Mirrors the in-game state after the FIRST haul drained A to min.
		const glm::vec2 srcPos{2.0F, 0.0F};
		const glm::vec2 destPos{4.0F, 0.0F};
		const EntityID srcBox = makeStorageBox(srcPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/8, /*min=*/8);
		const EntityID destBox = makeStorageBox(destPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/13, /*min=*/20);

		// Colonist mid-carry: a two-hand Wood armful in hands, task on its deposit leg (phase 2) to B.
		auto colonist = createColonist(srcPos);
		satisfyAllNeeds(*getNeeds(colonist)); // all needs met -> idle; the only floor option is Wander
		addKnownEntity(colonist, srcPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, destPos, boxDefId, engine::assets::CapabilityType::Storage);

		const uint64_t haulId = makeUmbrellaGoal(destBox, destPos, woodId);

		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		const uint32_t carried = ecs::addArmful(*inventory, registry, "Wood", 14);
		ASSERT_EQ(carried, 14U) << "seat the full armful into both hands";
		ASSERT_TRUE(inventory->isHolding("Wood"));

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		task->type = TaskType::Haul;
		task->state = TaskState::Moving;	  // in-flight toward the dest box (phase 2)
		task->navState = NavState::Traveling; // has a believed route (NOT CantFindWayTo) -> guard applies
		task->haulGoalId = haulId;
		task->haulItemDefName = "Wood";
		task->haulQuantity = 14;
		task->haulSourceStorageId = static_cast<uint64_t>(srcBox);
		task->haulSourcePosition = srcPos;
		task->haulTargetStorageId = static_cast<uint64_t>(destBox);
		task->haulTargetPosition = destPos;
		task->haulFromInventory = false;
		task->targetPosition = destPos;
		task->chainId = haulId;
		task->chainStep = 1;			   // past the Withdraw leg
		task->priorityTier = 6;			   // opportunistic stocking tier
		task->timeSinceEvaluation = 1.0F;  // > kReEvalInterval (0.5) -> force a re-eval this tick

		world->update(0.016F);

		// The bug's trigger must be present: the source offers no pull, so Wander is the top option.
		ASSERT_EQ(findPullOption(*getTrace(colonist)), nullptr)
			<< "source drained to its min -> no pull option offered (the trigger for the Wander abandonment)";
		const auto* selected = getTrace(colonist)->getSelected();
		ASSERT_NE(selected, nullptr);
		EXPECT_EQ(selected->taskType, TaskType::Wander)
			<< "with needs met and no haul, the highest-ranked option is idle Wander (tier 7)";

		// The colonist must HOLD the in-flight delivery despite Wander outranking it: still hauling Wood
		// to box B, armful still in hands. NOT switched to Wander, NOT carrying the load off forever.
		EXPECT_EQ(task->type, TaskType::Haul) << "did NOT switch to Wander -- the two-hand carry is held to finish";
		EXPECT_EQ(task->haulTargetStorageId, static_cast<uint64_t>(destBox)) << "still delivering to the dest box";
		EXPECT_FLOAT_EQ(task->targetPosition.x, destPos.x) << "still targeting the deposit leg (box B)";
		EXPECT_TRUE(inventory->isHolding("Wood")) << "the two-hand armful stayed in hands (not stranded by a wander)";
		EXPECT_EQ(ecs::handHeldQuantity(*inventory, "Wood"), 14U) << "the full armful is intact -- nothing dropped";
	}

	// 9c. The broadening must NOT over-hold: a strictly-HIGHER-tier challenger (a critical need, tier 2)
	//     must still preempt the mid-carry two-hand haul (tier 6). Dropping the load for an emergency is
	//     acceptable; the guard only blocks same-or-lower-priority challengers. Guards the broadened
	//     condition from pinning a colonist on a haul through a genuine emergency.
	TEST_F(AIDecisionSystemTest, TwoHandPullYieldsToCriticalNeedMidCarry) {
		auto& registry = engine::assets::AssetRegistry::Get();
		engine::assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = engine::assets::ItemCategory::RawMaterial;
		woodDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		woodDef.itemProperties = engine::assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 2.5F;
		registry.registerTestDefinition(std::move(woodDef));
		const uint32_t woodId = registry.getDefNameId("Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 srcPos{2.0F, 0.0F};
		const glm::vec2 destPos{4.0F, 0.0F};
		const EntityID srcBox = makeStorageBox(srcPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/8, /*min=*/8);
		const EntityID destBox = makeStorageBox(destPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/13, /*min=*/20);

		auto colonist = createColonist(srcPos);
		addKnownEntity(colonist, srcPos, boxDefId, engine::assets::CapabilityType::Storage);
		addKnownEntity(colonist, destPos, boxDefId, engine::assets::CapabilityType::Storage);
		// A critical hunger (5%) with a valid eat-from-inventory path -> tier 2, strictly above the haul.
		auto* needsForPreempt = getNeeds(colonist);
		needsForPreempt->get(NeedType::Thirst).value = 100.0F;
		needsForPreempt->get(NeedType::Energy).value = 100.0F;
		needsForPreempt->get(NeedType::Bladder).value = 100.0F;
		needsForPreempt->get(NeedType::Digestion).value = 100.0F;
		needsForPreempt->get(NeedType::Hunger).value = 5.0F; // critical -> tier 2

		const uint64_t haulId = makeUmbrellaGoal(destBox, destPos, woodId);

		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		inventory->addItem("Berry", 3); // the eat-from-inventory fulfillment path for critical hunger
		const uint32_t carried = ecs::addArmful(*inventory, registry, "Wood", 14);
		ASSERT_GT(carried, 0U) << "a two-hand Wood armful is seated in both hands (exact count is immaterial here)";
		ASSERT_TRUE(inventory->isHolding("Wood"));

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		task->type = TaskType::Haul;
		task->state = TaskState::Moving;
		task->navState = NavState::Traveling;
		task->haulGoalId = haulId;
		task->haulItemDefName = "Wood";
		task->haulQuantity = carried;
		task->haulSourceStorageId = static_cast<uint64_t>(srcBox);
		task->haulSourcePosition = srcPos;
		task->haulTargetStorageId = static_cast<uint64_t>(destBox);
		task->haulTargetPosition = destPos;
		task->haulFromInventory = false;
		task->targetPosition = destPos;
		task->chainId = haulId;
		task->chainStep = 1;
		task->priorityTier = 6;
		task->timeSinceEvaluation = 1.0F;

		world->update(0.016F);

		// A critical need outranks the stocking haul: the guard must NOT hold here. The colonist switches
		// off the haul to the need (dropping the armful is the accepted emergency cost).
		EXPECT_NE(task->type, TaskType::Haul) << "a critical need (tier 2) preempts the held haul (tier 6)";
		EXPECT_EQ(task->type, TaskType::FulfillNeed) << "switched to fulfilling the critical need";
		EXPECT_EQ(task->needToFulfill, NeedType::Hunger) << "the critical hunger is the new task";
	}

	// Runtime hysteresis, driven through world->update() (the static comparator test above only pins
	// the KEY math on hand-built options). A colonist committed to harvesting bush A must NOT thrash
	// to an equidistant same-tier bush B on the next re-eval (the in-progress option carries the +50
	// margin), but DOES switch to a third bush C placed clearly closer (its distance factor beats the
	// in-progress score plus the margin). Only AIDecisionSystem runs in this fixture, so the colonist
	// never physically arrives; distances stay fixed and the committed Task target is the assertion.
	TEST_F(AIDecisionSystemTest, HysteresisHoldsThenYieldsThroughUpdate) {
		auto& registry = engine::assets::AssetRegistry::Get();
		auto [bushId, stickId] = registerHarvestableYielding(registry, "Flora_HystBush", "HystStick");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist)); // idle of needs: only the stocking harvest competes

		// Two equidistant harvestable sources (30 m each). The distance curve is 5 score/m, so the 50
		// margin is worth ~10 m: B at the same distance can never overcome A's stickiness.
		addKnownEntity(colonist, {30.0F, 0.0F}, bushId, engine::assets::CapabilityType::Harvestable);  // A
		addKnownEntity(colonist, {0.0F, 30.0F}, bushId, engine::assets::CapabilityType::Harvestable);  // B

		// A stocking goal makes both bushes Available tier-6 harvest options.
		auto storage = world->createEntity();
		world->addComponent<Position>(storage, Position{{2.0F, 2.0F}});
		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::StorageGoalSystem;
		harvest.destinationEntity = storage;
		harvest.destinationPosition = {2.0F, 2.0F};
		harvest.yieldDefNameId = stickId;
		harvest.targetAmount = 99;
		harvest.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(harvest));

		// First re-eval: the system commits to one of the two equidistant bushes (deterministic via
		// tiebreakId). Capture that target; it is now the in-progress option.
		world->update(0.5F);
		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::Harvest) << "colonist commits to the stocking harvest";
		const uint64_t committedTarget = task->harvestTargetEntityId;
		ASSERT_NE(committedTarget, 0U) << "a concrete harvest target was selected";

		// Second re-eval with the equidistant sibling still present: the in-progress target holds.
		// Its +50 hysteresis margin is unbeatable by an equal-distance, equal-skill challenger.
		world->update(0.5F);
		EXPECT_EQ(task->harvestTargetEntityId, committedTarget)
			<< "an equidistant same-tier sibling must not overcome the hysteresis margin (no thrash)";

		// Now introduce a clearly-closer source (5 m vs 30 m: 25 m closer, far beyond the ~10 m margin).
		// Its distance factor (~275) beats the in-progress score (150 + 50). The next re-eval switches.
		addKnownEntity(colonist, {5.0F, 0.0F}, bushId, engine::assets::CapabilityType::Harvestable);  // C
		world->update(0.5F);
		EXPECT_EQ(task->type, TaskType::Harvest);
		EXPECT_NE(task->harvestTargetEntityId, committedTarget)
			<< "a clearly-closer same-tier target overcomes hysteresis and wins the switch";
	}

	// The gather-food sentinel (needValue==100, threshold==0, emitted when the colonist has no food
	// and knows an edible harvestable) classifies at tier 7 (idle) and, with my wander-floor fix,
	// outranks the same-tier wander option because wander's within-tier score is pinned to 0.
	TEST_F(AIDecisionSystemTest, GatherFoodClassifiesTier7AndBeatsWander) {
		auto& registry = engine::assets::AssetRegistry::Get();
		// A harvestable yielding the already-registered edible Berry, so the gather-food path fires.
		engine::assets::AssetDefinition berryBush;
		berryBush.defName = "Flora_BerryBush";
		berryBush.label = "Berry Bush";
		berryBush.capabilities.harvestable = engine::assets::HarvestableCapability{};
		berryBush.capabilities.harvestable->yieldDefName = "Berry";
		berryBush.capabilities.harvestable->requiredToolType = "";
		registry.registerTestDefinition(std::move(berryBush));
		const uint32_t berryBushId = registry.getDefNameId("Flora_BerryBush");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist)); // all needs met -> idle tier; no food in inventory
		// A nearby edible harvestable: drives the gather-food sentinel (and a reachable food source).
		addKnownEntity(colonist, {3.0F, 0.0F}, berryBushId, engine::assets::CapabilityType::Harvestable);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);

		// The gather-food sentinel reuses FulfillNeed with needValue==100 && threshold==0.
		const auto* gatherFood = findOption(*trace, [](const EvaluatedOption& o) {
			return o.taskType == TaskType::FulfillNeed && o.needValue >= 100.0F && o.threshold == 0.0F;
		});
		const auto* wander = findOption(*trace, [](const EvaluatedOption& o) { return o.taskType == TaskType::Wander; });
		ASSERT_NE(gatherFood, nullptr) << "an empty-inventory colonist with a known edible source gathers food";
		ASSERT_NE(wander, nullptr) << "wander is always offered as the idle floor";

		EXPECT_EQ(gatherFood->tier, 7) << "the gather-food sentinel is tier 7 (idle)";
		EXPECT_EQ(wander->tier, 7) << "wander is tier 7 (idle)";
		EXPECT_FLOAT_EQ(wander->score, 0.0F) << "wander is the idle floor: within-tier score pinned to 0";
		EXPECT_GT(gatherFood->score, wander->score) << "reachable gather-food outranks wander within the idle tier";
		EXPECT_TRUE(EvaluatedOption::higherPriority(*gatherFood, *wander))
			<< "gather-food wins the (tier, score) key over wander";
	}

	// A critical need (needValue < 10%) classifies at tier 2 (above everything in Phase 1/2).
	TEST_F(AIDecisionSystemTest, CriticalNeedClassifiesTier2) {
		auto colonist = createColonist({0.0F, 0.0F});
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Berry", 3); // a valid hunger fulfillment path (eat from inventory)

		setNeedValue(colonist, NeedType::Hunger, 5.0F); // critical
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* hunger = findOption(*trace, [](const EvaluatedOption& o) {
			return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Hunger;
		});
		ASSERT_NE(hunger, nullptr);
		EXPECT_EQ(hunger->tier, 2) << "a need below the 10% critical threshold classifies at tier 2";
	}

	// An actionable need (below its seek threshold but not critical) classifies at tier 5.
	TEST_F(AIDecisionSystemTest, ActionableNeedClassifiesTier5) {
		auto colonist = createColonist({0.0F, 0.0F});
		// A known water source so thirst is actionable (Available), not NoSource.
		addKnownEntity(colonist, {3.0F, 4.0F}, kWaterDefId, engine::assets::CapabilityType::Drinkable);

		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 40.0F); // actionable, above the 10% critical line
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* thirst = findOption(*trace, [](const EvaluatedOption& o) {
			return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Thirst;
		});
		ASSERT_NE(thirst, nullptr);
		EXPECT_EQ(thirst->status, OptionStatus::Selected) << "the actionable thirst is the chosen task";
		EXPECT_EQ(thirst->tier, 5) << "an actionable (non-critical) need classifies at tier 5";
	}

	// =============================================================================
	// Integration Tests with ActionSystem - Bug Reproduction
	// =============================================================================

	// Test fixture that includes both AI and Action systems
	class AIActionIntegrationTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			world = std::make_unique<World>();

			// Reset the PriorityConfig singleton to its default arbitration tiers: task selection here
			// depends on the per-task tier table, and an earlier suite that loads a partial
			// priority-tuning.xml now leaves it empty (loadFromFile is authoritative post-fix).
			engine::assets::PriorityConfig::Get().clear();

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
		// Unit test: Verify Task component supports chainId and chainStep fields.
		// NOTE: This tests the data structure, not the integration path. The actual
		// chainId assignment happens in selectTaskFromTrace(), which is tested through
		// full system update cycles in integration tests.
		auto colonist = createColonist({0.0F, 0.0F});

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);

		// Initially no chainId
		EXPECT_FALSE(task->chainId.has_value());
		EXPECT_EQ(task->chainStep, 0);

		// Verify the Task struct can store chain data correctly
		task->type = TaskType::Haul;
		task->chainId = 42ULL;
		task->chainStep = 0;
		task->haulItemDefName = "Berry";
		task->haulQuantity = 1;
		task->haulSourcePosition = {5.0F, 5.0F};
		task->haulTargetPosition = {10.0F, 10.0F};
		task->targetPosition = task->haulSourcePosition;
		task->state = TaskState::Moving;

		// Verify chain data is stored correctly
		EXPECT_TRUE(task->chainId.has_value());
		EXPECT_EQ(task->chainId.value(), 42ULL);
		EXPECT_EQ(task->chainStep, 0);
	}

	TEST_F(AIDecisionSystemTest, ChainHasNoScoreTermOnlyTierClassification) {
		// The old +2000 chain SCORE bonus is GONE from arbitration. A mid-chain provisioning task is
		// now a TIER-4 classification (servesActiveWorkOrder), not a score inflation. The
		// AvailableWorkOrderHaulClassifiesTier4 / ...Tier6 integration tests below exercise that
		// classification end-to-end through a real trace; this unit test pins the score side: the
		// within-tier score is composed ONLY of distanceFactor + skill + age + hysteresis, with no
		// hidden +2000 chain term, so a far provisioning haul cannot swamp a near one inside tier 4.
		EvaluatedOption provisioningHaul;
		provisioningHaul.taskType = TaskType::Haul;
		provisioningHaul.status = OptionStatus::Available;
		provisioningHaul.servesActiveWorkOrder = true; // mid-chain provisioning
		provisioningHaul.distanceFactor = distanceFactorAt(20.0F);
		provisioningHaul.skillBonus = 30;
		provisioningHaul.taskAgeBonus = 10;
		provisioningHaul.hysteresisBonus = 0;

		const float expected = provisioningHaul.distanceFactor + 30.0F + 10.0F + 0.0F;
		EXPECT_FLOAT_EQ(provisioningHaul.computeScore(), expected)
			<< "score is exactly the four within-tier terms; no chain bonus is added";
		EXPECT_LT(provisioningHaul.computeScore(), 1000.0F)
			<< "the deleted +2000 chain inflation must not reappear in the score";
	}

	TEST_F(AIDecisionSystemTest, ChainStepTracksCurrent) {
		// Unit test: Verify chainStep tracks phase transitions correctly.
		// In the full system, ActionSystem increments chainStep when:
		// - Haul: Pickup completes → chainStep 0→1
		// - PlacePackaged: PickupPackaged completes → chainStep 0→1
		auto colonist = createColonist({0.0F, 0.0F});

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);

		// Initial state: chainStep defaults to 0
		EXPECT_EQ(task->chainStep, 0);

		// Set up Haul task at step 0 (pickup phase)
		task->type = TaskType::Haul;
		task->chainId = 42ULL;
		task->chainStep = 0;
		EXPECT_EQ(task->chainStep, 0);

		// Simulate ActionSystem phase transition
		task->chainStep++;
		EXPECT_EQ(task->chainStep, 1) << "After pickup, chainStep should be 1 (delivery phase)";

		// Verify uint8_t type can track multiple phases if needed
		task->chainStep++;
		EXPECT_EQ(task->chainStep, 2);
	}

	// Chain-interruption stowing drives the real AIDecisionSystem::handleChainInterruption,
	// not a stand-in. A freed one-hand tool goes belt -> backpack -> drop, in that order. The
	// handler only proceeds when the NEW task's first action needs hands, so each test seeds the
	// ActionTypeRegistry with a hands-needing action and drives a Harvest interruption.
	class ChainInterruptionFixture : public AIDecisionSystemTest {
	  protected:
		// Register a 1-hand Axe tool and ensure the new task's first action (Harvest) needs hands.
		void SetUp() override {
			AIDecisionSystemTest::SetUp();

			auto& registry = engine::assets::AssetRegistry::Get();
			engine::assets::AssetDefinition axeDef;
			axeDef.defName = "Axe";
			axeDef.label = "Axe";
			axeDef.handsRequired = 1;
			axeDef.category = engine::assets::ItemCategory::Tool;
			axeDef.itemProperties = engine::assets::ItemProperties{};
			axeDef.itemProperties->stackSize = 1;
			axeDef.itemProperties->massKg = 1.0F;
			registry.registerTestDefinition(std::move(axeDef));

			engine::assets::ActionTypeDef harvestAction;
			harvestAction.defName = "Harvest";
			harvestAction.needsHands = true;
			engine::assets::ActionTypeRegistry::Get().registerTestAction(std::move(harvestAction));
		}

		void TearDown() override {
			engine::assets::ActionTypeRegistry::Get().clear();
			AIDecisionSystemTest::TearDown();
		}

		// Put the colonist mid-Haul, holding the Axe in one hand (chainStep=1).
		Task* armMidHaulCarryingAxe(EntityID colonist) {
			auto* task = getTask(colonist);
			task->type = TaskType::Haul;
			task->chainId = 100ULL;
			task->chainStep = 1;
			task->haulItemDefName = "Axe";
			task->state = TaskState::Moving;
			return task;
		}

		// Drive the real handler for a switch to a hands-needing Harvest task.
		void interruptForHarvest(EntityID colonist, const Task& task) {
			auto* inventory = world->getComponent<Inventory>(colonist);
			auto* position = world->getComponent<Position>(colonist);
			callHandleChainInterruption(colonist, task, *inventory, *position, TaskType::Harvest, NeedType::Count);
		}
	};

	TEST_F(ChainInterruptionFixture, FreedOneHandToolStowsToBeltFirst) {
		auto  colonist = createColonist({5.0F, 5.0F});
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);

		inventory->pickUp("Axe", 1);
		ASSERT_TRUE(inventory->isHolding("Axe"));

		auto* task = armMidHaulCarryingAxe(colonist);
		interruptForHarvest(colonist, *task);

		// Belt is the first stow target: the Axe lands in a belt slot, the hand is freed,
		// and nothing fell through to the backpack.
		EXPECT_TRUE(inventory->belt[0].has_value()) << "Axe stows to the first belt slot";
		EXPECT_EQ(inventory->belt[0]->defName, "Axe");
		EXPECT_FALSE(inventory->isHolding("Axe")) << "Hand freed for the new task";
		EXPECT_FALSE(inventory->hasItem("Axe")) << "Belt stow does not also hit the backpack";
	}

	TEST_F(ChainInterruptionFixture, FreedOneHandToolFallsBackToBackpackWhenBeltFull) {
		auto  colonist = createColonist({5.0F, 5.0F});
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);

		// Belt full: both quick-draw slots taken, so the Axe must fall back to the backpack.
		inventory->belt[0] = ItemStack{"Knife", 1};
		inventory->belt[1] = ItemStack{"Hammer", 1};
		inventory->pickUp("Axe", 1);
		ASSERT_TRUE(inventory->isHolding("Axe"));

		auto* task = armMidHaulCarryingAxe(colonist);
		interruptForHarvest(colonist, *task);

		EXPECT_FALSE(inventory->isHolding("Axe")) << "Hand freed for the new task";
		EXPECT_TRUE(inventory->hasItem("Axe")) << "Belt full -> Axe goes to the backpack";
		EXPECT_EQ(inventory->getQuantity("Axe"), 1U);
	}

	TEST_F(ChainInterruptionFixture, FreedOneHandToolDroppedWhenBeltAndBackpackFull) {
		auto  colonist = createColonist({5.0F, 5.0F});
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);

		// Capture drops through the real callback the handler fires.
		std::string droppedDef;
		int			dropCalls = 0;
		world->getSystem<AIDecisionSystem>().setDropItemCallback(
			[&](const std::string& defName, float /*x*/, float /*y*/) {
				++dropCalls;
				droppedDef = defName;
			}
		);

		// Belt full and backpack saturated to its slot capacity: no stow target remains.
		inventory->belt[0] = ItemStack{"Knife", 1};
		inventory->belt[1] = ItemStack{"Hammer", 1};
		inventory->maxCapacity = 1;
		inventory->addItem("Berry", 1); // fills the only backpack slot with a different type
		ASSERT_FALSE(inventory->canAdd("Axe", 1));

		inventory->pickUp("Axe", 1);
		ASSERT_TRUE(inventory->isHolding("Axe"));

		auto* task = armMidHaulCarryingAxe(colonist);
		interruptForHarvest(colonist, *task);

		EXPECT_FALSE(inventory->isHolding("Axe")) << "Hand freed even when nowhere to stow";
		EXPECT_FALSE(inventory->hasItem("Axe")) << "Backpack was full, Axe not stowed";
		EXPECT_EQ(dropCalls, 1) << "Axe dropped via the drop callback";
		EXPECT_EQ(droppedDef, "Axe");
	}

	TEST_F(AIDecisionSystemTest, TaskFirstActionNeedsHandsMapping) {
		// Unit test: Verify ActionTypeRegistry returns expected needsHands values.
		// The getFirstActionDefName() helper maps TaskType→ActionDefName, and
		// ActionTypeRegistry provides the needsHands property from XML config.

		auto& actionRegistry = engine::assets::ActionTypeRegistry::Get();

		// Check if action types are loaded (they should be from game config)
		// If not loaded in test environment, we verify the registry API works
		if (actionRegistry.size() == 0) {
			// Registry not loaded - just verify the API exists and doesn't crash
			// The actual values are tested in WorkConfig.test.cpp
			EXPECT_FALSE(actionRegistry.actionNeedsHands("NonExistent"));
			return;
		}

		// If registry is loaded, verify expected mappings from action-types.xml:
		// - Pickup: needsHands=true (picking up items)
		// - Craft: needsHands=true (crafting actions)
		// - Harvest: needsHands=true (gathering actions)
		// - Sleep: needsHands=false (sleeping doesn't need hands)
		// - Wander: needsHands=false (walking around)
		// - Toilet: needsHands=false (using toilet)
		// - Eat: needsHands=true (eating food)

		if (actionRegistry.hasAction("Pickup")) {
			EXPECT_TRUE(actionRegistry.actionNeedsHands("Pickup"))
			    << "Pickup action should need hands";
		}
		if (actionRegistry.hasAction("Sleep")) {
			EXPECT_FALSE(actionRegistry.actionNeedsHands("Sleep"))
			    << "Sleep action should NOT need hands";
		}
		if (actionRegistry.hasAction("Wander")) {
			EXPECT_FALSE(actionRegistry.actionNeedsHands("Wander"))
			    << "Wander action should NOT need hands";
		}
		if (actionRegistry.hasAction("Eat")) {
			EXPECT_TRUE(actionRegistry.actionNeedsHands("Eat"))
			    << "Eat action should need hands";
		}
	}

	// =============================================================================
	// Deconstruct work (work-driven demolish): an Available Deconstruct goal for a
	// demolishing structure with work to undo must yield a Deconstruct task for a
	// Construction-capable colonist whose needs are all satisfied.
	// =============================================================================

	TEST_F(AIDecisionSystemTest, AvailableDeconstructGoalYieldsDeconstructTask) {
		auto colonist = createColonist({0.0F, 0.0F});
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		// Construction skill makes the colonist a builder/deconstructor (feeds the work bonus).
		Skills skills;
		skills.setLevel("Construction", 5.0F);
		world->addComponent<Skills>(colonist, std::move(skills));

		// A BUILT structure marked for demolition (work invested, so it can be deconstructed).
		auto blueprint = world->createEntity();
		world->addComponent<Position>(blueprint, Position{{3.0F, 0.0F}});
		world->addComponent<Structure>(blueprint, Structure{StructureKind::Foundation, /*graphId=*/0});
		StructureBlueprint bp;
		bp.phase = StructureBlueprint::BuildPhase::Complete;
		bp.workTotal = 50.0F;
		bp.workDone = 50.0F;
		bp.demolishing = true;
		world->addComponent<StructureBlueprint>(blueprint, bp);

		// An Available Deconstruct goal targeting it (what ConstructionSystem would emit).
		GoalTask goal;
		goal.type = TaskType::Deconstruct;
		goal.owner = GoalOwner::ConstructionGoalSystem;
		goal.destinationEntity = blueprint;
		goal.destinationPosition = {3.0F, 0.0F};
		goal.targetAmount = 1;
		goal.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::Deconstruct);
		EXPECT_EQ(task->buildBlueprintEntityId, static_cast<uint64_t>(blueprint));
	}

	// =============================================================================
	// Storage-haul sizing: a haul to a storage is sized by what the destination can ACCEPT
	// of the specific item (Inventory::addableCount = stack headroom + freeSlots * stackSize),
	// NOT the storage's free-slot count. A 5-slot storage accepting RawMaterial proposes a haul
	// of up to 5 * 40 = 200 of a 40-stack material, not 5. This is the headline bug the physical-
	// stack model exposed: storage badly under-filled because the goal's slot-count drove the size.
	// =============================================================================

	TEST_F(AIDecisionSystemTest, StorageHaulSizedByAddableCountNotSlotCount) {
		auto& registry = engine::assets::AssetRegistry::Get();

		// A light, one-hand, carryable RawMaterial with a 40 stack. Light enough (0.1 kg) that
		// carry weight (350/trip at 35 kg) never binds, so the destination's addableCount is the
		// sole cap and the slot-vs-item distinction is unambiguous.
		engine::assets::AssetDefinition pelletDef;
		pelletDef.defName = "Pellet";
		pelletDef.label = "Pellet";
		pelletDef.handsRequired = 1;
		pelletDef.category = engine::assets::ItemCategory::RawMaterial;
		pelletDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		pelletDef.itemProperties = engine::assets::ItemProperties{};
		pelletDef.itemProperties->stackSize = 40;
		pelletDef.itemProperties->massKg = 0.1F;
		registry.registerTestDefinition(std::move(pelletDef));
		const uint32_t pelletId = registry.getDefNameId("Pellet");

		auto colonist = createColonist({0.0F, 0.0F});
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		// A loose ground pile of Pellet the colonist knows about (the haul source).
		addKnownEntity(colonist, {2.0F, 0.0F}, pelletId, engine::assets::CapabilityType::Carryable);

		// A 5-slot storage with an empty Inventory, plus the StorageGoalSystem-style Haul goal
		// pointing at it. Its targetAmount mirrors the free-slot count (5) -- the value under test
		// is that the PROPOSED haul is sized by addableCount (200), not this 5.
		auto	  storage = world->createEntity();
		world->addComponent<Position>(storage, Position{{4.0F, 0.0F}});
		Inventory storageInv;
		storageInv.maxCapacity = 5;
		world->addComponent<Inventory>(storage, storageInv);

		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = storage;
		goal.destinationPosition = {4.0F, 0.0F};
		goal.acceptedCategory = engine::assets::ItemCategory::RawMaterial;
		goal.targetAmount = 5; // free-slot count, must NOT cap the haul
		goal.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::Haul) << "colonist takes the storage haul when all needs are met";
		EXPECT_EQ(task->haulItemDefName, "Pellet");
		EXPECT_EQ(task->haulTargetStorageId, static_cast<uint64_t>(storage));
		EXPECT_EQ(task->haulQuantity, 200U)
			<< "sized by the storage's addableCount (5 slots x 40 stack = 200), not the 5-slot count";
		EXPECT_GT(task->haulQuantity, 5U) << "not capped at the free-slot count";
	}

	// A storage that is genuinely full (no free slot AND every stack at its cap) offers no haul:
	// addableCount is 0, so evaluateHaulOptions emits no option for it and the colonist falls
	// through to another task (here, Wander).
	TEST_F(AIDecisionSystemTest, FullStorageOffersNoHaulOption) {
		auto& registry = engine::assets::AssetRegistry::Get();

		engine::assets::AssetDefinition pelletDef;
		pelletDef.defName = "Pellet";
		pelletDef.label = "Pellet";
		pelletDef.handsRequired = 1;
		pelletDef.category = engine::assets::ItemCategory::RawMaterial;
		pelletDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		pelletDef.itemProperties = engine::assets::ItemProperties{};
		pelletDef.itemProperties->stackSize = 40;
		pelletDef.itemProperties->massKg = 0.1F;
		registry.registerTestDefinition(std::move(pelletDef));
		const uint32_t pelletId = registry.getDefNameId("Pellet");

		auto colonist = createColonist({0.0F, 0.0F});
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		addKnownEntity(colonist, {2.0F, 0.0F}, pelletId, engine::assets::CapabilityType::Carryable);

		// 2-slot storage filled to its 40 cap on both slots: no free slot, no stack headroom.
		auto	  storage = world->createEntity();
		world->addComponent<Position>(storage, Position{{4.0F, 0.0F}});
		Inventory storageInv;
		storageInv.maxCapacity = 2;
		storageInv.addItem("Pellet", 80);
		ASSERT_EQ(storageInv.addableCount("Pellet"), 0U);
		world->addComponent<Inventory>(storage, storageInv);

		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = storage;
		goal.destinationPosition = {4.0F, 0.0F};
		goal.acceptedCategory = engine::assets::ItemCategory::RawMaterial;
		goal.targetAmount = 0;
		goal.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_NE(task->type, TaskType::Haul) << "a full storage offers no haul; addableCount is 0";
	}

	// A craft fetch must not be offered when the colonist is at/over its carry-weight cap: the
	// Pickup clamps to cargoUnitsThatFit and adds 0, the staged count never rises, and the AI would
	// otherwise re-issue the identical fetch every tick -- an infinite "fetch -> collect 0 -> fetch"
	// loop that never completes the craft (observed in-game with an over-loaded colonist). The fetch
	// branch skips when no unit fits, so the colonist falls through to another task instead of spinning.
	TEST_F(AIDecisionSystemTest, OverweightColonistOffersNoCraftFetch) {
		auto& registry = engine::assets::AssetRegistry::Get();

		// The recipe material to fetch: a one-hand carryable RawMaterial.
		engine::assets::AssetDefinition stoneDef;
		stoneDef.defName = "SmallStone";
		stoneDef.label = "Small Stone";
		stoneDef.handsRequired = 1;
		stoneDef.category = engine::assets::ItemCategory::RawMaterial;
		stoneDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		stoneDef.itemProperties = engine::assets::ItemProperties{};
		stoneDef.itemProperties->stackSize = 100;
		stoneDef.itemProperties->massKg = 1.5F;
		registry.registerTestDefinition(std::move(stoneDef));

		// Heavy ballast UNRELATED to the recipe: the colonist is carrying a load of this (e.g. cargo
		// hauled earlier), so it's at the carry cap when the craft fetch comes up. It must NOT be the
		// fetched material, or the colonist would just deliver it from inventory.
		engine::assets::AssetDefinition ballastDef;
		ballastDef.defName = "Ballast";
		ballastDef.label = "Ballast";
		ballastDef.handsRequired = 1;
		ballastDef.category = engine::assets::ItemCategory::RawMaterial;
		ballastDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		ballastDef.itemProperties = engine::assets::ItemProperties{};
		ballastDef.itemProperties->stackSize = 100;
		ballastDef.itemProperties->massKg = 1.5F;
		registry.registerTestDefinition(std::move(ballastDef));

		// Resolve the material id after both registrations are in. registerTestDefinition now appends
		// ids stably, but capturing once everything is registered keeps the test honest regardless of
		// interning internals. (The original CI-only failure was a stale id captured between
		// registrations that, after the map rehashed on libstdc++, aliased "Ballast" -- so the
		// inventory-source craft-haul delivered the colonist's 30-unit ballast load to the station,
		// the very Haul this test forbids.)
		const uint32_t stoneId = registry.getDefNameId("SmallStone");

		auto colonist = createColonist({0.0F, 0.0F});
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		// Load the pack past the carry-weight cap (30 x 1.5 kg = 45 kg > 35 kg) with ballast, so any
		// further pickup of SmallStone lifts 0.
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		inventory->addItem("Ballast", 30);
		ASSERT_EQ(ecs::cargoUnitsThatFit(*inventory, registry, "SmallStone"), 0U)
			<< "precondition: the over-loaded colonist can lift no SmallStone";
		ASSERT_EQ(ecs::availableQuantity(*inventory, "SmallStone"), 0U)
			<< "precondition: the colonist holds none of the fetched material (no inventory-source delivery)";

		// A loose SmallStone the colonist remembers (the fetch source).
		addKnownEntity(colonist, {2.0F, 0.0F}, stoneId, engine::assets::CapabilityType::Carryable);

		// Craft goal needs 2 SmallStone; its child craft-haul goal is Available to fetch.
		const auto station = world->createEntity();
		world->addComponent<Position>(station, Position{{4.0F, 0.0F}});

		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.destinationPosition = {4.0F, 0.0F};
		craft.targetAmount = 2;
		craft.status = GoalStatus::Blocked;
		const uint64_t craftId = GoalTaskRegistry::Get().createGoal(std::move(craft));

		GoalTask haul;
		haul.type = TaskType::Haul;
		haul.owner = GoalOwner::CraftingGoalSystem;
		haul.destinationEntity = station;
		haul.destinationPosition = {4.0F, 0.0F};
		haul.acceptedDefNameIds = {stoneId};
		haul.targetAmount = 2;
		haul.parentGoalId = craftId;
		haul.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(haul));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_NE(task->type, TaskType::Haul)
			<< "an over-weight colonist offers no craft fetch; the pickup would lift 0 and loop forever";
	}

	// A harvest must not be offered when the colonist can't carry the yield: applyCollectionEffect
	// clamps the take to carry weight AND backpack slots and adds 0 at the cap, so the staged count
	// never rises. Emitting the option anyway makes the colonist pick the harvest, collect 0
	// ("Collected 0 of N ... carry-limited"), re-evaluate, and pick the same harvest again -- an
	// infinite chop-nothing loop (observed in-game as a colonist "stuck harvesting"). The carry gate
	// skips the option when no unit fits, so the colonist falls through instead of spinning.
	TEST_F(AIDecisionSystemTest, OverweightColonistOffersNoHarvest) {
		auto& registry = engine::assets::AssetRegistry::Get();

		// The yield the harvestable produces: a one-hand carryable RawMaterial.
		engine::assets::AssetDefinition stickDef;
		stickDef.defName = "Stick";
		stickDef.label = "Stick";
		stickDef.handsRequired = 1;
		stickDef.category = engine::assets::ItemCategory::RawMaterial;
		stickDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		stickDef.itemProperties = engine::assets::ItemProperties{};
		stickDef.itemProperties->stackSize = 100;
		stickDef.itemProperties->massKg = 1.5F;
		registry.registerTestDefinition(std::move(stickDef));

		// A tool-less harvestable (a woody bush) whose yield is the Stick above. requiredToolType
		// empty so the tool gate passes and only the carry gate decides.
		engine::assets::AssetDefinition bushDef;
		bushDef.defName = "Flora_WoodyBush";
		bushDef.label = "Woody Bush";
		bushDef.capabilities.harvestable = engine::assets::HarvestableCapability{};
		bushDef.capabilities.harvestable->yieldDefName = "Stick";
		bushDef.capabilities.harvestable->requiredToolType = "";
		registry.registerTestDefinition(std::move(bushDef));

		// Heavy ballast UNRELATED to the yield: the colonist is at its carry cap, so a Stick can't fit.
		engine::assets::AssetDefinition ballastDef;
		ballastDef.defName = "Ballast";
		ballastDef.label = "Ballast";
		ballastDef.handsRequired = 1;
		ballastDef.category = engine::assets::ItemCategory::RawMaterial;
		ballastDef.capabilities.carryable = engine::assets::CarryableCapability{1};
		ballastDef.itemProperties = engine::assets::ItemProperties{};
		ballastDef.itemProperties->stackSize = 100;
		ballastDef.itemProperties->massKg = 1.5F;
		registry.registerTestDefinition(std::move(ballastDef));

		// Resolve ids once all definitions are registered (see OverweightColonistOffersNoCraftFetch):
		// keeps captures correct independent of the interning index's ordering internals.
		const uint32_t bushId = registry.getDefNameId("Flora_WoodyBush");
		const uint32_t stickId = registry.getDefNameId("Stick");

		auto colonist = createColonist({0.0F, 0.0F});
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		inventory->addItem("Ballast", 30); // 30 x 1.5 kg = 45 kg > 35 kg cap
		ASSERT_EQ(ecs::cargoUnitsThatFit(*inventory, registry, "Stick"), 0U)
			<< "precondition: the over-loaded colonist can lift no Stick";

		// The colonist remembers a harvestable bush yielding Stick.
		addKnownEntity(colonist, {2.0F, 0.0F}, bushId, engine::assets::CapabilityType::Harvestable);

		// A Harvest goal wanting Stick, available.
		const auto station = world->createEntity();
		world->addComponent<Position>(station, Position{{4.0F, 0.0F}});

		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::CraftingGoalSystem;
		harvest.destinationEntity = station;
		harvest.destinationPosition = {4.0F, 0.0F};
		harvest.acceptedDefNameIds = {stickId};
		harvest.yieldDefNameId = stickId;
		harvest.targetAmount = 2;
		harvest.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(harvest));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_NE(task->type, TaskType::Harvest)
			<< "an over-weight colonist offers no harvest; the chop would yield 0 and loop forever";
	}

	// --- Off-mesh recovery resolution (colony-origin last-resort fallback) -------
	//
	// The recovery at the top of AIDecisionSystem::update resolves an off-mesh colonist
	// in three tiers: (1) snap to the nearest walkable navmesh face; (2) if no walkable
	// face is in range, snap to the colony origin (the home clearing); (3) if neither is
	// available, leave it (a headless test with no origin set). The full NavigationSystem
	// wiring is exercised in NavigationSystem.test.cpp; this mirrors the resolution choice
	// as a pure predicate so all three tiers are covered deterministically (same pattern as
	// NavPathStalenessTest, which mirrors the replan predicate).
	namespace {
		// Mirror of the recovery's target choice: nearest floor wins, else the colony origin,
		// else no move (nullopt). nearestFloor and colonyOrigin are each independently optional.
		std::optional<glm::vec2> recoveryTarget(std::optional<glm::vec2> nearestFloor,
												std::optional<glm::vec2> colonyOrigin) {
			if (nearestFloor.has_value()) {
				return nearestFloor;
			}
			if (colonyOrigin.has_value()) {
				return colonyOrigin;
			}
			return std::nullopt;
		}
	} // namespace

	TEST(OffMeshRecoveryResolutionTest, PrefersNearestFloorThenColonyOriginThenNoMove) {
		const glm::vec2 floor{3.0F, 4.0F};
		const glm::vec2 origin{1.5F, 4.5F};

		// Tier 1: a nearest walkable face exists -> snap there, NOT to the colony origin.
		EXPECT_EQ(recoveryTarget(floor, origin), std::optional<glm::vec2>(floor));

		// Tier 2: no walkable face in range, but the colony origin is set -> last-resort snap to it.
		EXPECT_EQ(recoveryTarget(std::nullopt, origin), std::optional<glm::vec2>(origin));

		// Tier 3: neither available (headless, no origin wired) -> leave the colonist in place.
		EXPECT_FALSE(recoveryTarget(std::nullopt, std::nullopt).has_value());
	}

	// --- Colony origin is the single source, read by both consumers -------------
	//
	// The colony origin is set once (GameWorldState::Colony, set at landing) and read by
	// two consumers: GameScene's camera-home sync and the AI off-mesh recovery fallback.
	// This asserts the consolidation: the value pushed into AIDecisionSystem via the public
	// setColonyOrigin is exactly what the recovery fallback reads back -- no independent copy,
	// no drift. The fixture is a friend of AIDecisionSystem, so it can read the private store
	// the recovery uses, mirroring what GameScene reads from m_colony.originPosition.
	TEST_F(AIDecisionSystemTest, ColonyOriginConsumersReadSingleSource) {
		const glm::vec2 origin{1.50F, 4.50F};

		// Before wiring, the fallback target is unset (matches Tier 3 above: no origin -> no snap).
		EXPECT_FALSE(getColonyOrigin().has_value());

		// One write to the single source...
		world->getSystem<AIDecisionSystem>().setColonyOrigin(origin);

		// ...and the recovery fallback reads exactly that value back -- the AI consumer mirrors
		// the colony origin GameScene also reads, rather than holding an independently derived copy.
		ASSERT_TRUE(getColonyOrigin().has_value());
		EXPECT_EQ(*getColonyOrigin(), origin);
	}

} // namespace ecs::test
