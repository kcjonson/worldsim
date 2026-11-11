// Tests for AIDecisionSystem - Colonist autonomous decision making
// Tests tier-based priority, re-evaluation logic, wander behavior, memory dependency,
// and DecisionTrace generation for task queue display.

#include "AIDecisionSystem.h"
#include "ActionSystem.h"
#include "MovementSystem.h"
#include "NavigationSystem.h"
#include "NavigationSystem.test.h"
#include "PhysicsSystem.h"
#include "TimeSystem.h"
#include "VisionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "../WorldSource.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Colonist.h"
#include "../components/DecisionTrace.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/NavPath.h"
#include "../components/Needs.h"
#include "../components/PlayerControlled.h"
#include "../components/Skills.h"
#include "../components/SourceReachMemo.h"
#include "../components/StorageConfiguration.h"
#include "../components/Structure.h"
#include "../components/StructureBlueprint.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include "assets/ActionTypeRegistry.h"
#include "assets/AssetRegistry.h"
#include "assets/ConstructionRegistry.h"
#include "assets/PriorityConfig.h"
#include "assets/RecipeRegistry.h"
#include "assets/placement/PlacementExecutor.h"

#include <construction/ConstructionWorld.h>

#include <world/Biome.h>
#include <world/BiomeWeights.h>
#include <world/chunk/ChunkManager.h>
#include <world/chunk/ChunkSampleResult.h>
#include <world/chunk/IWorldSampler.h>
#include <world/chunk/SyntheticWorldSampler.h>

#include <worldgen/sampling/PondNetwork2D.h>

#include <gtest/gtest.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
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
			assets::PriorityConfig::Get().clear();

			// Initialize AssetRegistry (singleton) - needed for capability lookups
			auto& registry = assets::AssetRegistry::Get();

			// Register Berry as edible entity for testing (unified entity/item model)
			assets::AssetDefinition berryDef;
			berryDef.defName = "Berry";
			berryDef.label = "Berry";
			berryDef.itemProperties = assets::ItemProperties{};
			berryDef.itemProperties->stackSize = 20;
			berryDef.itemProperties->edible = assets::EdibleCapability{0.3F, assets::CapabilityQuality::Normal, true};
			registry.registerTestDefinition(std::move(berryDef));

			// Register system with deterministic RNG seed for reproducible tests
			auto& recipeRegistry = assets::RecipeRegistry::Get();
			world->registerSystem<AIDecisionSystem>(registry, recipeRegistry, kTestRngSeed);
		}

		void TearDown() override {
			// Clean up test definitions
			assets::AssetRegistry::Get().clearDefinitions();
			assets::RecipeRegistry::Get().clear();
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
			world->addComponent<SourceReachMemo>(entity, SourceReachMemo{});
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
		void addKnownEntity(EntityID entity, glm::vec2 position, uint32_t defNameId, assets::CapabilityType capability) {
			auto* memory = world->getComponent<Memory>(entity);
			ASSERT_NE(memory, nullptr);
			// Create capability mask from single capability
			uint16_t capabilityMask = static_cast<uint16_t>(1U << static_cast<size_t>(capability));
			memory->rememberWorldEntity(position, defNameId, capabilityMask);
		}

		/// Get the current task for an entity
		Task* getTask(EntityID entity) { return world->getComponent<Task>(entity); }

#ifdef DEVELOPMENT_BUILD
		/// Memory entries the work evaluators have looked at so far.
		uint64_t workSourceVisits() { return world->getSystem<AIDecisionSystem>().m_workSources.visits; }

		/// Boxes the colony's box knowledge has worked out so far.
		uint64_t colonyStorageWorkedOut() { return world->getSystem<AIDecisionSystem>().m_colonyStorage.workedOut; }
		uint64_t boxRoomsReadOut() { return world->getSystem<AIDecisionSystem>().m_boxRooms.readOut; }
		uint64_t boxPrioritiesReadOut() { return world->getSystem<AIDecisionSystem>().m_boxRooms.priorityReadOut; }

		/// Remaining needs the AI has worked out so far.
		uint64_t needsWorkedOut() { return world->getSystem<AIDecisionSystem>().m_needs.computed; }
#endif

		/// Drive the private chain-interruption handler directly. This fixture is a friend of
		/// AIDecisionSystem (derived fixtures are not), so the seam lives here.
		void callHandleChainInterruption(
			EntityID entity, Task& task, Inventory& inventory, const Position& position,
			TaskType newTaskType, NeedType newNeedType
		) {
			EvaluatedOption selected;
			selected.taskType = newTaskType;
			selected.needType = newNeedType;
			world->getSystem<AIDecisionSystem>().handleChainInterruption(entity, task, inventory, position, selected);
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
		/// Appearance (defName `boxDef`, a registered Storage def so ColonyStorageKnowledge can key it).
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
				.category = assets::ItemCategory::RawMaterial,
				.priority = priority,
				.minAmount = minAmount,
				.maxAmount = maxAmount,
			});
			world->addComponent<ecs::StorageConfiguration>(box, std::move(config));
			return box;
		}

		/// Create the ordinary umbrella Haul goal (StorageGoalSystem-owned) that a storage
		/// box raises when it wants items. This is the goal whose evaluation runs the pull scan.
		uint64_t makeUmbrellaGoal(EntityID destBox, glm::vec2 destPos, uint32_t itemDefNameId) {
			GoalTask haul;
			haul.type = TaskType::Haul;
			haul.owner = GoalOwner::StorageGoalSystem;
			haul.destinationEntity = destBox;
			haul.destinationPosition = destPos;
			haul.acceptedDefNameIds = {itemDefNameId};
			haul.status = GoalStatus::Available;
			return GoalTaskRegistry::Get().createGoal(std::move(haul));
		}

		/// A storage box at `pos` stocking `item` toward `minAmount`, its umbrella, and the stocking
		/// Harvest child StorageGoalSystem raises for it. Returns the Harvest goal's id.
		uint64_t makeStockingHarvest(glm::vec2 pos, const std::string& item, uint32_t minAmount = 10) {
			const uint32_t itemId = assets::AssetRegistry::Get().getDefNameId(item);
			const EntityID box = makeStorageBox(pos, "Box", item, ecs::StoragePriority::Medium, 0, minAmount);
			GoalTask	   harvest;
			harvest.type = TaskType::Harvest;
			harvest.owner = GoalOwner::StorageGoalSystem;
			harvest.destinationEntity = box;
			harvest.destinationPosition = pos;
			harvest.acceptedDefNameIds = {itemId};
			harvest.yieldDefNameId = itemId;
			harvest.parentGoalId = makeUmbrellaGoal(box, pos, itemId);
			harvest.status = GoalStatus::Available;
			return GoalTaskRegistry::Get().createGoal(std::move(harvest));
		}

		/// Queue a job at `station` whose recipe needs `count` of `item`, with an empty store, so the
		/// station's Craft root needs them.
		void makeStationNeed(EntityID station, const std::string& item, uint32_t count) {
			assets::RecipeDef recipe;
			recipe.defName = "Recipe_Need_" + item;
			recipe.label = recipe.defName;
			recipe.inputs.push_back({item, 0, count});
			assets::RecipeRegistry::Get().registerTestRecipe(recipe);
			world->addComponent<WorkQueue>(station).addJob(recipe.defName, 1);
			world->addComponent<Inventory>(station, Inventory::createForStorage());
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
		addKnownEntity(colonist, {10.0F, 10.0F}, kWaterDefId, assets::CapabilityType::Drinkable);

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
		addKnownEntity(colonist, waterPosition, kWaterDefId, assets::CapabilityType::Drinkable);

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
		addKnownEntity(colonist, {5.0F, 0.0F}, kBerryBushDefId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, {0.0F, 5.0F}, kWaterDefId, assets::CapabilityType::Drinkable);

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

	// The session seed is the only source of the wander sequence: a replay with the same seed picks
	// the same targets, arrival after arrival.
	TEST_F(AIDecisionSystemTest, WanderSequenceFollowsSeed) {
		auto wanderTargets = [this](uint32_t seed) {
			world = std::make_unique<World>();
			world->registerSystem<AIDecisionSystem>(assets::AssetRegistry::Get(), assets::RecipeRegistry::Get(), seed);
			auto colonist = createColonist({0.0F, 0.0F});
			for (auto need : {NeedType::Hunger, NeedType::Thirst, NeedType::Energy, NeedType::Bladder}) {
				setNeedValue(colonist, need, 100.0F);
			}
			std::vector<glm::vec2> targets;
			for (int i = 0; i < 8; ++i) {
				world->update(0.016F);
				auto* task = getTask(colonist);
				if (task == nullptr) {
					ADD_FAILURE() << "colonist has no Task";
					break;
				}
				EXPECT_EQ(task->type, TaskType::Wander);
				targets.push_back(task->targetPosition);
				task->state = TaskState::Arrived;
			}
			return targets;
		};

		const auto first = wanderTargets(kTestRngSeed);
		EXPECT_EQ(wanderTargets(kTestRngSeed), first);
		EXPECT_NE(wanderTargets(kTestRngSeed + 1), first);
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

		// A reachable water source + actionable thirst -> a FulfillNeed task (Moving). Unlike wander
		// (which now re-evaluates every tick), a moving need task is not re-evaluated, so its
		// timeSinceEvaluation accumulates between the periodic re-evals.
		addKnownEntity(colonist, {8.0F, 3.0F}, kWaterDefId, assets::CapabilityType::Drinkable);
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 40.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::FulfillNeed) << "a real need task, not wander";
		EXPECT_EQ(task->timeSinceEvaluation, 0.0F); // Just evaluated

		// Multiple updates while moving (not arrived)
		world->update(0.1F);
		EXPECT_NEAR(task->timeSinceEvaluation, 0.1F, 0.01F);

		world->update(0.1F);
		EXPECT_NEAR(task->timeSinceEvaluation, 0.2F, 0.01F);
	}

	// Bug 89: idle wander must yield to a higher-tier task the very next tick, not lag until the
	// periodic kReEvalInterval (0.5s) re-eval. A moving wander now re-evaluates every tick.
	TEST_F(AIDecisionSystemTest, WanderYieldsImmediatelyToActionableNeed) {
		auto colonist = createColonist({0.0F, 0.0F});

		// All needs satisfied -> the colonist wanders.
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->update(0.016F);
		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::Wander);
		ASSERT_EQ(task->state, TaskState::Moving) << "wandering toward its point";

		// Thirst crosses into actionable with a reachable water source known. One tick later -- far
		// under kReEvalInterval (0.5s) -- the colonist must abandon wander for the higher-tier need.
		addKnownEntity(colonist, {8.0F, 3.0F}, kWaterDefId, assets::CapabilityType::Drinkable);
		setNeedValue(colonist, NeedType::Thirst, 40.0F);

		world->update(0.016F);
		EXPECT_EQ(task->type, TaskType::FulfillNeed) << "wander yields immediately to the actionable need";
		EXPECT_EQ(task->needToFulfill, NeedType::Thirst);
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

		addKnownEntity(colonist, farWater, kWaterDefId, assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, nearWater, kWaterDefId + 1, assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, midWater, kWaterDefId + 2, assets::CapabilityType::Drinkable);

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
		auto& registry = assets::AssetRegistry::Get();
		auto& recipeRegistry = assets::RecipeRegistry::Get();
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

		addKnownEntity(colonist, {10.0F, 10.0F}, kBerryBushDefId, assets::CapabilityType::Edible);

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
	// Scale Tests
	// =============================================================================

#ifdef DEVELOPMENT_BUILD
	// A box wanting an item nobody knows a source of, and no lower-priority box holding it: its room is
	// never read, however many items it names.
	TEST_F(AIDecisionSystemTest, AStorageBoxWithNoSourceIsNeverMeasured) {
		auto&					registry = assets::AssetRegistry::Get();
		assets::AssetDefinition wood;
		wood.defName = "Wood";
		wood.label = "Wood";
		wood.category = assets::ItemCategory::RawMaterial;
		wood.capabilities.carryable = assets::CarryableCapability{1};
		wood.itemProperties = assets::ItemProperties{};
		wood.itemProperties->stackSize = 100;
		registry.registerTestDefinition(std::move(wood));
		assets::AssetDefinition boxDef;
		boxDef.defName = "Box";
		boxDef.label = "Box";
		boxDef.capabilities.storage = assets::StorageCapability{};
		registry.registerTestDefinition(std::move(boxDef));
		const glm::vec2 boxPos{6.0F, -4.0F};
		const EntityID	box = makeStorageBox(boxPos, "Box", "Wood", ecs::StoragePriority::High);
		makeUmbrellaGoal(box, boxPos, registry.getDefNameId("Wood"));
		const EntityID colonist = createColonist({0.0F, 0.0F});
		addKnownEntity(colonist, boxPos, registry.getDefNameId("Box"), assets::CapabilityType::Storage);

		const uint64_t before = boxRoomsReadOut();
		world->update(0.016F);
		ASSERT_TRUE(getTask(colonist)->isActive());
		EXPECT_EQ(boxRoomsReadOut(), before);
	}
#endif

	TEST_F(AIDecisionSystemTest, EveryColonistOfManyGetsATask) {
		constexpr int kNumColonists = 100;

		// A low box holding Wood and a high box wanting it, both known to every colonist, so each
		// colonist's evaluation asks whether the colony knows the source box. A second low box holds
		// Wood nobody has seen, and a third holds none, so it is never asked about.
		auto&					registry = assets::AssetRegistry::Get();
		assets::AssetDefinition wood;
		wood.defName = "Wood";
		wood.label = "Wood";
		wood.category = assets::ItemCategory::RawMaterial;
		wood.capabilities.carryable = assets::CarryableCapability{1};
		wood.itemProperties = assets::ItemProperties{};
		wood.itemProperties->stackSize = 100;
		wood.itemProperties->massKg = 0.1F;
		registry.registerTestDefinition(std::move(wood));
		assets::AssetDefinition boxDef;
		boxDef.defName = "Box";
		boxDef.label = "Box";
		boxDef.capabilities.storage = assets::StorageCapability{};
		registry.registerTestDefinition(std::move(boxDef));
		const uint32_t	woodId = registry.getDefNameId("Wood");
		const uint32_t	boxDefId = registry.getDefNameId("Box");
		const glm::vec2 lowPos{2.0F, -4.0F};
		const glm::vec2 highPos{6.0F, -4.0F};
		const EntityID	lowBox = makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10);
		const EntityID	highBox = makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High);
		const EntityID	unseenBox = makeStorageBox({-40.0F, -40.0F}, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10);
		makeStorageBox({-50.0F, -40.0F}, "Box", "Wood", ecs::StoragePriority::Low);
		makeUmbrellaGoal(highBox, highPos, woodId);

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
					assets::CapabilityType::Harvestable
				);
			}
		}
		for (auto colonist : colonists) {
			addKnownEntity(colonist, lowPos, boxDefId, assets::CapabilityType::Storage);
			addKnownEntity(colonist, highPos, boxDefId, assets::CapabilityType::Storage);
		}

#ifdef DEVELOPMENT_BUILD
		const uint64_t workedOutBefore = colonyStorageWorkedOut();
		const uint64_t roomsBefore = boxRoomsReadOut();
		const uint64_t prioritiesBefore = boxPrioritiesReadOut();
#endif
		world->update(0.016F);

		// Verify all colonists have tasks
		for (auto colonist : colonists) {
			auto* task = getTask(colonist);
			ASSERT_NE(task, nullptr);
			EXPECT_TRUE(task->isActive());
			const auto& options = getTrace(colonist)->options;
			EXPECT_TRUE(std::any_of(options.begin(), options.end(), [lowBox](const EvaluatedOption& o) {
				return o.haulSourceStorageId == static_cast<uint64_t>(lowBox);
			})) << "every colonist is offered the pull from the known low box";
			EXPECT_FALSE(std::any_of(options.begin(), options.end(), [unseenBox](const EvaluatedOption& o) {
				return o.haulSourceStorageId == static_cast<uint64_t>(unseenBox);
			})) << "nobody has seen the other box";
		}

#ifdef DEVELOPMENT_BUILD
		// Each box asked about is worked out once for the update, not once per colonist, and the box
		// holding no Wood is never worked out at all.
		EXPECT_EQ(colonyStorageWorkedOut() - workedOutBefore, 2U);
		// The high box's room for Wood is read from the world once for the hundred colonists.
		EXPECT_EQ(boxRoomsReadOut() - roomsBefore, 1U);
		// Its priority for Wood, which orders every colonist's pull within the tier, is read once too.
		EXPECT_EQ(boxPrioritiesReadOut() - prioritiesBefore, 1U);
#endif
	}

#ifdef DEVELOPMENT_BUILD
	// Twenty colonists decide in one update against ten storage boxes, each naming three items and
	// stocking one of them by cutting, while each colonist carries two of the items and knows a pile of
	// each and a bush. Every root's remaining need is asked by the loose haul, the pull, the carry-in,
	// and the cut, by every colonist, yet each (root, item, basis) is worked out once for the update.
	TEST_F(AIDecisionSystemTest, EachRemainingNeedIsWorkedOutOncePerUpdate) {
		auto& registry = assets::AssetRegistry::Get();
		for (const char* name : {"Ore", "Clay", "Flint"}) {
			assets::AssetDefinition item;
			item.defName = name;
			item.label = name;
			item.category = assets::ItemCategory::RawMaterial;
			item.capabilities.carryable = assets::CarryableCapability{1};
			item.itemProperties = assets::ItemProperties{};
			item.itemProperties->stackSize = 40;
			item.itemProperties->massKg = 0.5F;
			registry.registerTestDefinition(std::move(item));
		}
		assets::AssetDefinition bush;
		bush.defName = "OreBush";
		bush.label = "OreBush";
		bush.capabilities.harvestable = assets::HarvestableCapability{};
		bush.capabilities.harvestable->yieldDefName = "Ore";
		registry.registerTestDefinition(std::move(bush));
		assets::AssetDefinition box;
		box.defName = "Box";
		box.label = "Box";
		box.capabilities.storage = assets::StorageCapability{};
		registry.registerTestDefinition(std::move(box));
		const uint32_t ore = registry.getDefNameId("Ore");
		const uint32_t clay = registry.getDefNameId("Clay");
		const uint32_t flint = registry.getDefNameId("Flint");

		constexpr int kRoots = 10;
		constexpr int kColonists = 20;
		for (int i = 0; i < kRoots; ++i) {
			const glm::vec2 at{static_cast<float>(i) * 4.0F, 20.0F};
			const EntityID	boxEntity = makeStorageBox(at, "Box", "Ore", StoragePriority::Medium, 0, /*minAmount=*/30);
			auto&			config = *world->getComponent<StorageConfiguration>(boxEntity);
			config.addRule(StorageRule{.defName = "Clay", .category = assets::ItemCategory::RawMaterial});
			config.addRule(StorageRule{.defName = "Flint", .category = assets::ItemCategory::RawMaterial});
			GoalTask umbrella;
			umbrella.type = TaskType::Haul;
			umbrella.owner = GoalOwner::StorageGoalSystem;
			umbrella.destinationEntity = boxEntity;
			umbrella.destinationPosition = at;
			umbrella.acceptedDefNameIds = {ore, clay, flint};
			const uint64_t umbrellaId = GoalTaskRegistry::Get().createGoal(std::move(umbrella));
			GoalTask harvest;
			harvest.type = TaskType::Harvest;
			harvest.owner = GoalOwner::StorageGoalSystem;
			harvest.destinationEntity = boxEntity;
			harvest.destinationPosition = at;
			harvest.acceptedDefNameIds = {ore};
			harvest.yieldDefNameId = ore;
			harvest.parentGoalId = umbrellaId;
			GoalTaskRegistry::Get().createGoal(std::move(harvest));
		}
		for (int i = 0; i < kColonists; ++i) {
			const EntityID colonist = createColonist({static_cast<float>(i), 0.0F});
			for (auto need : NeedsComponent::kActionableNeeds) {
				getNeeds(colonist)->get(need).value = 100.0F;
			}
			auto& inventory = *world->getComponent<Inventory>(colonist);
			inventory.addItem("Clay", 1);
			inventory.addItem("Flint", 1);
			addKnownEntity(colonist, {-5.0F, 0.0F}, ore, assets::CapabilityType::Carryable);
			addKnownEntity(colonist, {-6.0F, 0.0F}, clay, assets::CapabilityType::Carryable);
			addKnownEntity(colonist, {-7.0F, 0.0F}, registry.getDefNameId("OreBush"), assets::CapabilityType::Harvestable);
		}

		const uint64_t before = needsWorkedOut();
		world->update(0.016F);
		int decided = 0;
		for (auto [entity, trace] : world->view<DecisionTrace>()) {
			(void)entity;
			decided += trace.options.empty() ? 0 : 1;
		}
		ASSERT_EQ(decided, kColonists);
		// Ore, Clay, and Flint against each box's stock target, and Ore against its cutting minimum.
		EXPECT_EQ(needsWorkedOut() - before, static_cast<uint64_t>(kRoots * 3 + kRoots));

		// The next update works them out again: a deposit between updates shows at once.
		for (auto [entity, task] : world->view<Task>()) {
			(void)entity;
			task.timeSinceEvaluation = 1.0F;
		}
		const uint64_t second = needsWorkedOut();
		world->update(0.016F);
		EXPECT_EQ(needsWorkedOut() - second, static_cast<uint64_t>(kRoots * 3 + kRoots));
	}
#endif

	// A memory near capacity, almost all of it water no work goal wants: the need is still found, and
	// the work evaluators look only at the remembered wood and trees a goal can use.
	TEST_F(AIDecisionSystemTest, FindsANeedSourceInALargeMemory) {
		auto& registry = assets::AssetRegistry::Get();
		assets::AssetDefinition wood;
		wood.defName = "Wood";
		wood.label = "Wood";
		wood.category = assets::ItemCategory::RawMaterial;
		wood.capabilities.carryable = assets::CarryableCapability{1};
		wood.itemProperties = assets::ItemProperties{};
		wood.itemProperties->stackSize = 40;
		wood.itemProperties->massKg = 0.1F;
		registry.registerTestDefinition(std::move(wood));
		assets::AssetDefinition tree;
		tree.defName = "Tree";
		tree.label = "Tree";
		tree.capabilities.harvestable = assets::HarvestableCapability{};
		tree.capabilities.harvestable->yieldDefName = "Wood";
		registry.registerTestDefinition(std::move(tree));
		const uint32_t woodId = registry.getDefNameId("Wood");
		const uint32_t treeId = registry.getDefNameId("Tree");

		const glm::vec2 boxPos{0.0F, 6.0F};
		makeStockingHarvest(boxPos, "Wood");

		// Two colonists remember the same 3 piles and 2 trees; one also remembers 9,990 waters.
		constexpr int kPiles = 3;
		constexpr int kTrees = 2;
		constexpr int kWaters = 9990;
		const EntityID lean = createColonist({0.0F, 0.0F});
		const EntityID full = createColonist({0.0F, 0.0F});
		for (EntityID colonist : {lean, full}) {
			for (int i = 0; i < kPiles; ++i) {
				addKnownEntity(colonist, {3.0F + static_cast<float>(i), 0.0F}, woodId, assets::CapabilityType::Carryable);
			}
			for (int i = 0; i < kTrees; ++i) {
				addKnownEntity(colonist, {-3.0F - static_cast<float>(i), 0.0F}, treeId, assets::CapabilityType::Harvestable);
			}
			setNeedValue(colonist, NeedType::Hunger, 100.0F);
			setNeedValue(colonist, NeedType::Thirst, 40.0F);
			setNeedValue(colonist, NeedType::Energy, 100.0F);
			setNeedValue(colonist, NeedType::Bladder, 100.0F);
		}
		for (int i = 0; i < kWaters; ++i) {
			addKnownEntity(
				full, {static_cast<float>(i), static_cast<float>(i)}, kWaterDefId + static_cast<uint32_t>(i), assets::CapabilityType::Drinkable
			);
		}
		ASSERT_EQ(world->getComponent<Memory>(full)->worldEntityCount(), static_cast<size_t>(kPiles + kTrees + kWaters));

#ifdef DEVELOPMENT_BUILD
		const uint64_t visitsBefore = workSourceVisits();
#endif
		world->update(0.016F);

		// Should have found an entity and created task
		auto* task = getTask(full);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);

		// The first to decide fetches a pile before it cuts; its fetch covers the box's minimum, so the
		// second is offered no cut.
		for (EntityID colonist : {lean, full}) {
			const auto& options = getTrace(colonist)->options;
			EXPECT_EQ(std::count_if(options.begin(), options.end(), [](const EvaluatedOption& o) { return o.taskType == TaskType::Haul; }), kPiles);
			EXPECT_EQ(
				std::count_if(options.begin(), options.end(), [](const EvaluatedOption& o) { return o.taskType == TaskType::Harvest; }),
				colonist == lean ? kTrees : 0
			);
		}
		EXPECT_EQ(getTask(lean)->type, TaskType::Haul);

#ifdef DEVELOPMENT_BUILD
		// Per colonist: each goal visits only the sources of its item. The 9,990 waters add nothing.
		constexpr uint64_t kPerColonist = kPiles + kTrees;
		EXPECT_EQ(workSourceVisits() - visitsBefore, 2 * kPerColonist);
#endif
	}

	// A goal the colonist can't serve visits none of its sources: a full box, a pack with no room, and
	// a cut without the tool are each settled before any remembered pile or tree is looked at.
	TEST_F(AIDecisionSystemTest, WorkGatesComeBeforeTheSources) {
		auto&					registry = assets::AssetRegistry::Get();
		assets::AssetDefinition wood;
		wood.defName = "Wood";
		wood.label = "Wood";
		wood.category = assets::ItemCategory::RawMaterial;
		wood.capabilities.carryable = assets::CarryableCapability{1};
		wood.itemProperties = assets::ItemProperties{};
		wood.itemProperties->stackSize = 40;
		wood.itemProperties->massKg = 0.1F;
		registry.registerTestDefinition(std::move(wood));
		assets::AssetDefinition tree;
		tree.defName = "Tree";
		tree.label = "Tree";
		tree.capabilities.harvestable = assets::HarvestableCapability{};
		tree.capabilities.harvestable->yieldDefName = "Wood";
		tree.capabilities.harvestable->requiredToolType = "Axe";
		registry.registerTestDefinition(std::move(tree));
		const uint32_t woodId = registry.getDefNameId("Wood");
		const uint32_t treeId = registry.getDefNameId("Tree");

		const glm::vec2 fullPos{0.0F, 6.0F};
		const glm::vec2 roomyPos{0.0F, -6.0F};
		const EntityID	fullBox = makeStorageBox(fullPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/50 * 40);
		ASSERT_EQ(world->getComponent<Inventory>(fullBox)->addableCount("Wood"), 0U);
		const EntityID roomyBox = makeStorageBox(roomyPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/0, /*min=*/10);
		makeUmbrellaGoal(fullBox, fullPos, woodId);
		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::StorageGoalSystem;
		harvest.yieldDefNameId = woodId;
		harvest.destinationPosition = roomyPos;
		harvest.parentGoalId = makeUmbrellaGoal(roomyBox, roomyPos, woodId);
		harvest.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(harvest));

		// No axe, and a pack that can't take a single unit.
		const EntityID colonist = createColonist({0.0F, 0.0F});
		world->getComponent<Inventory>(colonist)->carryCapacityKg = 0.0F;
		for (int i = 0; i < 20; ++i) {
			addKnownEntity(colonist, {3.0F + static_cast<float>(i), 0.0F}, woodId, assets::CapabilityType::Carryable);
			addKnownEntity(colonist, {-3.0F - static_cast<float>(i), 0.0F}, treeId, assets::CapabilityType::Harvestable);
		}

#ifdef DEVELOPMENT_BUILD
		const uint64_t visitsBefore = workSourceVisits();
#endif
		world->update(0.016F);

		const auto& options = getTrace(colonist)->options;
		// The cut is shown as no room, with no source behind it.
		const auto harvests = std::count_if(options.begin(), options.end(), [](const EvaluatedOption& o) { return o.taskType == TaskType::Harvest; });
		const auto harvestNoRoom = std::count_if(options.begin(), options.end(), [](const EvaluatedOption& o) {
			return o.taskType == TaskType::Harvest && o.status == OptionStatus::NoRoom && !o.targetPosition.has_value();
		});
		EXPECT_EQ(harvests, 1);
		EXPECT_EQ(harvestNoRoom, 1);
		// The roomy box's goal says why it can't be served; the full box's says nothing.
		const auto hauls = std::count_if(options.begin(), options.end(), [](const EvaluatedOption& o) { return o.taskType == TaskType::Haul; });
		const auto noRoom = std::count_if(options.begin(), options.end(), [](const EvaluatedOption& o) {
			return o.taskType == TaskType::Haul && o.status == OptionStatus::NoRoom;
		});
		EXPECT_EQ(hauls, 1);
		EXPECT_EQ(noRoom, 1);
#ifdef DEVELOPMENT_BUILD
		EXPECT_EQ(workSourceVisits() - visitsBefore, 0U);
#endif
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
		addKnownEntity(colonist, {5.0F, 0.0F}, kBerryBushDefId, assets::CapabilityType::Harvestable);

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
		EXPECT_TRUE(describeOption(*world, *hungerOption).find("no known source") != std::string::npos);
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
		addKnownEntity(colonist, berryPosition, kBerryBushDefId, assets::CapabilityType::Harvestable);

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

		ASSERT_NE(trace->getSelected(), nullptr);
		EXPECT_FALSE(describeOption(*world, *trace->getSelected()).empty());

		// Trigger re-evaluation by simulating arrival
		task->state = TaskState::Arrived;
		world->update(0.016F);

		// Trace should be rebuilt (may have same or different summary)
		// The key is that it's still valid
		ASSERT_NE(trace->getSelected(), nullptr);
		EXPECT_FALSE(describeOption(*world, *trace->getSelected()).empty());
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
	// option with a nearby reachable source carries a positive
	// distance factor plus any Farming skill, so it wins the (tier, score) key.
	TEST_F(AIDecisionSystemTest, GatherFoodRanksAboveWanderInIdleTier) {
		// Wander after the fix: tier 7, score 0 (no distance/skill/age/hysteresis).
		EvaluatedOption wander;
		wander.taskType = TaskType::Wander;
		wander.status = OptionStatus::Available;
		wander.tier = 7;
		wander.score = 0.0F; // the idle floor

		// Gather food: tier 7, a nearby (5 m) reachable food source plus modest Farming skill.
		EvaluatedOption gatherFood;
		gatherFood.taskType = TaskType::FulfillNeed;
		gatherFood.needType = NeedType::Hunger;
		gatherFood.gathersFood = true;
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

	TEST_F(AIDecisionSystemTest, TraceCriticalNeedHasHighestPriority) {
		auto colonist = createColonist({0.0F, 0.0F});

		// Give colonist berries so hunger has a valid fulfillment path
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Berry", 3);

		// Add water source for thirst
		addKnownEntity(colonist, {0.0F, 5.0F}, kWaterDefId, assets::CapabilityType::Drinkable);

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
	// work-order Haul/Harvest -> 4, stocking Haul/Harvest -> 6, gather food -> 7 (above
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
			assets::AssetRegistry& registry, const std::string& harvestableDef, const std::string& yieldDef
		) {
			assets::AssetDefinition yield;
			yield.defName = yieldDef;
			yield.label = yieldDef;
			yield.handsRequired = 1;
			yield.category = assets::ItemCategory::RawMaterial;
			yield.capabilities.carryable = assets::CarryableCapability{1};
			yield.itemProperties = assets::ItemProperties{};
			yield.itemProperties->stackSize = 100;
			yield.itemProperties->massKg = 0.1F; // light: carry weight never binds the gate
			registry.registerTestDefinition(std::move(yield));

			assets::AssetDefinition harvestable;
			harvestable.defName = harvestableDef;
			harvestable.label = harvestableDef;
			harvestable.capabilities.harvestable = assets::HarvestableCapability{};
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
		auto& registry = assets::AssetRegistry::Get();
		auto [bushId, stickId] = registerHarvestableYielding(registry, "Flora_WorkBush", "WorkStick");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, {2.0F, 0.0F}, bushId, assets::CapabilityType::Harvestable);

		const auto station = world->createEntity();
		world->addComponent<Position>(station, Position{{4.0F, 0.0F}});
		makeStationNeed(station, "WorkStick", 2);

		// Parent Craft goal + its child Harvest goal (the provisioning step).
		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.destinationPosition = {4.0F, 0.0F};
		craft.status = GoalStatus::Blocked;
		const uint64_t craftId = GoalTaskRegistry::Get().createGoal(std::move(craft));

		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::CraftingGoalSystem;
		harvest.destinationEntity = station;
		harvest.destinationPosition = {4.0F, 0.0F};
		harvest.yieldDefNameId = stickId;
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
		auto& registry = assets::AssetRegistry::Get();
		auto [bushId, stickId] = registerHarvestableYielding(registry, "Flora_StockBush", "StockStick");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, {2.0F, 0.0F}, bushId, assets::CapabilityType::Harvestable);

		// A stocking harvest: StorageGoalSystem chops to fill a box toward its minimum. No Craft/Build
		// root, so servesActiveWorkOrder is false and it stays at the opportunistic base tier.
		(void)stickId;
		makeStockingHarvest({4.0F, 0.0F}, "StockStick");

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
	// + Inventory + Position + Appearance) whose Appearance defName resolves so ColonyStorageKnowledge
	// can key the box in memory.

	namespace {
		// Register a light one-hand carryable RawMaterial; returns its defNameId. Light so carry
		// weight never binds the pull's per-trip clamp.
		[[nodiscard]] uint32_t registerPullItem(assets::AssetRegistry& registry, const std::string& defName) {
			assets::AssetDefinition item;
			item.defName = defName;
			item.label = defName;
			item.handsRequired = 1;
			item.category = assets::ItemCategory::RawMaterial;
			item.capabilities.carryable = assets::CarryableCapability{1};
			item.itemProperties = assets::ItemProperties{};
			item.itemProperties->stackSize = 100;
			item.itemProperties->massKg = 0.1F;
			registry.registerTestDefinition(std::move(item));
			return registry.getDefNameId(defName);
		}

		// Register a storage container asset def (Storage capability). One def is shared by all boxes
		// in a test; its defName is what ColonyStorageKnowledge resolves from a box's Appearance.
		void registerStorageDef(assets::AssetRegistry& registry, const std::string& defName) {
			assets::AssetDefinition box;
			box.defName = defName;
			box.label = defName;
			box.capabilities.storage = assets::StorageCapability{};
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
		auto& registry = assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 lowPos{2.0F, 0.0F};
		const glm::vec2 highPos{4.0F, 0.0F};
		const EntityID lowBox = makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10);
		const EntityID highBox = makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, lowPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, highPos, boxDefId, assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);

		world->update(0.016F);

		const auto* pull = findPullOption(*getTrace(colonist));
		ASSERT_NE(pull, nullptr) << "a high box should pull Wood from a low box";
		EXPECT_EQ(pull->haulSourceStorageId, static_cast<uint64_t>(lowBox));
		EXPECT_EQ(pull->haulDestinationId, static_cast<uint64_t>(highBox));
		EXPECT_TRUE(pull->servesStorageStocking);
	}

	// 2. Never from same: two Medium boxes both holding Wood -> NO pull between them (strict-< gate).
	TEST_F(AIDecisionSystemTest, NoPullBetweenSamePriorityBoxes) {
		auto& registry = assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 aPos{2.0F, 0.0F};
		const glm::vec2 bPos{4.0F, 0.0F};
		makeStorageBox(aPos, "Box", "Wood", ecs::StoragePriority::Medium, /*qty=*/10);
		const EntityID boxB = makeStorageBox(bPos, "Box", "Wood", ecs::StoragePriority::Medium, /*qty=*/10);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, aPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, bPos, boxDefId, assets::CapabilityType::Storage);

		makeUmbrellaGoal(boxB, bPos, woodId);

		world->update(0.016F);

		EXPECT_EQ(findPullOption(*getTrace(colonist)), nullptr)
			<< "equal priority must not pull (strict-< gate)";
	}

	// 3. Never from higher: a High box holds Wood, a Low box wants Wood -> no pull from High into Low.
	TEST_F(AIDecisionSystemTest, NoPullFromHigherPriorityBox) {
		auto& registry = assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 highPos{2.0F, 0.0F};
		const glm::vec2 lowPos{4.0F, 0.0F};
		makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/10);
		const EntityID lowBox = makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, highPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, lowPos, boxDefId, assets::CapabilityType::Storage);

		makeUmbrellaGoal(lowBox, lowPos, woodId);

		world->update(0.016F);

		EXPECT_EQ(findPullOption(*getTrace(colonist)), nullptr)
			<< "a low box must not pull from a higher box";
	}

	// 4. Conflict, higher dest wins: a Low box holds Wood; a High box and a Critical box both want it.
	//    The selected option's destination is the Critical box (storagePriorityBias breaks the tie
	//    within tier 6), deterministically.
	TEST_F(AIDecisionSystemTest, PullConflictHigherDestinationWins) {
		auto& registry = assets::AssetRegistry::Get();
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
		addKnownEntity(colonist, lowPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, highPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, critPos, boxDefId, assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);
		makeUmbrellaGoal(critBox, critPos, woodId);

		world->update(0.016F);

		const auto* selected = getTrace(colonist)->getSelected();
		ASSERT_NE(selected, nullptr);
		EXPECT_EQ(selected->haulSourceStorageId, static_cast<uint64_t>(lowBox));
		EXPECT_EQ(selected->haulDestinationId, static_cast<uint64_t>(critBox))
			<< "the Critical destination outranks the High one within tier 6";
	}

	// 5. Don't-drain-below-min: source Low box holds 10 Wood with its OWN rule min=8 -> pull qty == 2.
	TEST_F(AIDecisionSystemTest, PullDoesNotDrainSourceBelowItsMin) {
		auto& registry = assets::AssetRegistry::Get();
		const uint32_t woodId = registerPullItem(registry, "Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		const glm::vec2 lowPos{2.0F, 0.0F};
		const glm::vec2 highPos{4.0F, 0.0F};
		makeStorageBox(lowPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/10, /*min=*/8);
		const EntityID highBox = makeStorageBox(highPos, "Box", "Wood", ecs::StoragePriority::High);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, lowPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, highPos, boxDefId, assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);

		world->update(0.016F);

		const auto* pull = findPullOption(*getTrace(colonist));
		ASSERT_NE(pull, nullptr);
		EXPECT_EQ(pull->haulQuantity, 2U) << "10 held - 8 source min = 2 drainable";
	}

	// 6. Don't-overfill: dest High box max=15 already holds 10; source Low box holds 10 -> pull qty == 5.
	TEST_F(AIDecisionSystemTest, PullDoesNotOverfillDestinationMax) {
		auto& registry = assets::AssetRegistry::Get();
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
		addKnownEntity(colonist, lowPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, highPos, boxDefId, assets::CapabilityType::Storage);

		makeUmbrellaGoal(highBox, highPos, woodId);

		world->update(0.016F);

		const auto* pull = findPullOption(*getTrace(colonist));
		ASSERT_NE(pull, nullptr);
		EXPECT_EQ(pull->haulQuantity, 5U) << "dest max 15 - 10 already held = 5 room";
	}

	// 7. Within-tier ordering: a Critical-dest pull and a Low-dest pull -> both at tier 6, the
	//    Critical-dest pull has the higher score and is Selected.
	TEST_F(AIDecisionSystemTest, PullCriticalDestinationOutscoresLowWithinTier6) {
		auto& registry = assets::AssetRegistry::Get();
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
		addKnownEntity(colonist, lowSrcPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, medDestPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, critDestPos, boxDefId, assets::CapabilityType::Storage);

		makeUmbrellaGoal(medBox, medDestPos, woodId);
		makeUmbrellaGoal(critBox, critDestPos, woodId);

		world->update(0.016F);

		const auto* medPull = findOption(*getTrace(colonist), [medBox](const EvaluatedOption& o) {
			return o.haulSourceStorageId != 0 && o.haulDestinationId == static_cast<uint64_t>(medBox);
		});
		const auto* critPull = findOption(*getTrace(colonist), [critBox](const EvaluatedOption& o) {
			return o.haulSourceStorageId != 0 && o.haulDestinationId == static_cast<uint64_t>(critBox);
		});
		ASSERT_NE(medPull, nullptr);
		ASSERT_NE(critPull, nullptr);
		EXPECT_EQ(medPull->tier, 6) << "stocking pulls stay at tier 6";
		EXPECT_EQ(critPull->tier, 6) << "stocking pulls stay at tier 6";
		EXPECT_GT(critPull->score, medPull->score) << "Critical destination outscores Medium within the tier";
		EXPECT_EQ(getTrace(colonist)->getSelected()->haulDestinationId, static_cast<uint64_t>(critBox));
	}

	// 8. Discovery-gating: the source box is NOT in any colonist's memory -> no pull option (no god-view).
	TEST_F(AIDecisionSystemTest, PullRequiresColonyToKnowSourceBox) {
		auto& registry = assets::AssetRegistry::Get();
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
		addKnownEntity(colonist, highPos, boxDefId, assets::CapabilityType::Storage);

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
	//    test (WithdrawTwoHandWoodRidesInHandsThenDepositsIntoDestBox) skipped. A full armful now has no
	//    room for more, so the scan offers no withdraw at all; the hold must still keep the delivery.
	TEST_F(AIDecisionSystemTest, TwoHandPullHoldsAgainstFreshSamePriorityPullMidCarry) {
		auto& registry = assets::AssetRegistry::Get();
		// A REAL two-hand bulk material (handsRequired==2): rides in both hands, can't stow to
		// belt/backpack, so an interruption would DROP it. registerPullItem registers a one-hand item,
		// so build Wood explicitly here.
		assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = assets::ItemCategory::RawMaterial;
		woodDef.capabilities.carryable = assets::CarryableCapability{1};
		woodDef.itemProperties = assets::ItemProperties{};
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
		addKnownEntity(colonist, srcPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, destPos, boxDefId, assets::CapabilityType::Storage);

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
		task->haulItemDefNameId = assets::AssetRegistry::Get().getDefNameId("Wood");
		task->haulQuantity = 14;
		task->haulSourceStorageId = static_cast<uint64_t>(srcBox);
		task->haulSourcePosition = srcPos;
		task->haulDestinationId = static_cast<uint64_t>(destBox);
		task->haulTargetPosition = destPos;
		task->haulFromInventory = false;
		task->targetPosition = destPos; // the deposit leg targets B; the fresh pull option targets A
		task->chainStep = 1; // past the Withdraw leg
		task->priorityTier = 6; // opportunistic stocking tier
		task->timeSinceEvaluation = 1.0F; // > kReEvalInterval (0.5) -> force a re-eval this tick

		world->update(0.016F);

		// The full armful leaves no room for more Wood, so the pull-source scan no longer offers a fresh
		// withdraw from A as a challenger: the trace shows it as no room instead.
		EXPECT_EQ(findPullOption(*getTrace(colonist)), nullptr) << "no withdraw a full armful couldn't take";
		const auto* noRoom = findOption(*getTrace(colonist), [](const EvaluatedOption& o) {
			return o.taskType == TaskType::Haul && o.status == OptionStatus::NoRoom && o.haulItemDefNameId == assets::AssetRegistry::Get().getDefNameId("Wood");
		});
		EXPECT_NE(noRoom, nullptr) << "the trace says why the pull isn't offered";

		// The colonist must HOLD the in-flight delivery: still hauling the same Wood to box B, with the
		// armful still in hands. No switch, no drop loop.
		EXPECT_EQ(task->type, TaskType::Haul) << "stays on the haul (did not switch tasks)";
		EXPECT_EQ(task->haulDestinationId, static_cast<uint64_t>(destBox)) << "still delivering to the dest box";
		EXPECT_FLOAT_EQ(task->targetPosition.x, destPos.x) << "still targeting the deposit leg, not re-pointed at A";
		EXPECT_TRUE(inventory->isHolding("Wood")) << "the two-hand armful stayed in hands (not dropped)";
		EXPECT_EQ(ecs::handHeldQuantity(*inventory, "Wood"), 14U) << "the full armful is intact -- nothing dropped";
	}

	// 9b. A two-hand carry finishes its delivery when the source is drained mid-carry (the second-haul
	//     abandonment). Same mid-carry setup as #9, but the source box is drained to its OWN min so the
	//     pull-source scan offers NOTHING, while box B still wants Wood. The colonist once walked off
	//     carrying the un-stowable armful indefinitely (dest box stalls, Wood stranded in hands). The
	//     armful's carry-in is the same task as the deposit leg it is on, so it is kept, nothing drops,
	//     and the deposit stops at B's configured max with the surplus still carried.
	TEST_F(AIDecisionSystemTest, TwoHandPullHoldsAgainstWanderWhenSourceDrainedMidCarry) {
		auto& registry = assets::AssetRegistry::Get();
		// REAL two-hand Wood (handsRequired==2): rides in both hands, can't stow -> a switch drops it.
		assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = assets::ItemCategory::RawMaterial;
		woodDef.capabilities.carryable = assets::CarryableCapability{1};
		woodDef.itemProperties = assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 2.5F;
		registry.registerTestDefinition(std::move(woodDef));
		const uint32_t woodId = registry.getDefNameId("Wood");
		registerStorageDef(registry, "Box");
		const uint32_t boxDefId = registry.getDefNameId("Box");

		// Source A (Low) is at its OWN min (held == minAmount == 8) -> 0 drainable -> NO pull offered.
		const glm::vec2 srcPos{2.0F, 0.0F};
		const glm::vec2 destPos{4.0F, 0.0F};
		const EntityID srcBox = makeStorageBox(srcPos, "Box", "Wood", ecs::StoragePriority::Low, /*qty=*/8, /*min=*/8);
		// Dest B (High) holds 13 and takes up to 20: it still wants 7 of the 14 carried.
		const EntityID destBox = makeStorageBox(destPos, "Box", "Wood", ecs::StoragePriority::High, /*qty=*/13, /*min=*/20, /*max=*/20);

		// Colonist mid-carry: a two-hand Wood armful in hands, task on its deposit leg (phase 2) to B.
		auto colonist = createColonist(srcPos);
		satisfyAllNeeds(*getNeeds(colonist)); // all needs met -> idle; the only floor option is Wander
		addKnownEntity(colonist, srcPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, destPos, boxDefId, assets::CapabilityType::Storage);

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
		task->haulItemDefNameId = assets::AssetRegistry::Get().getDefNameId("Wood");
		task->haulQuantity = 14;
		task->haulSourceStorageId = static_cast<uint64_t>(srcBox);
		task->haulSourcePosition = srcPos;
		task->haulDestinationId = static_cast<uint64_t>(destBox);
		task->haulTargetPosition = destPos;
		task->haulFromInventory = false;
		task->targetPosition = destPos;
		task->chainStep = 1;			   // past the Withdraw leg
		task->priorityTier = 6;			   // opportunistic stocking tier
		task->timeSinceEvaluation = 1.0F;  // > kReEvalInterval (0.5) -> force a re-eval this tick

		world->update(0.016F);

		// The bug's trigger must be present: the source offers no pull.
		ASSERT_EQ(findPullOption(*getTrace(colonist)), nullptr)
			<< "source drained to its min -> no pull option offered (the trigger for the Wander abandonment)";
		const auto* selected = getTrace(colonist)->getSelected();
		ASSERT_NE(selected, nullptr);
		EXPECT_EQ(selected->taskType, TaskType::Haul) << "the armful's carry-in to B outranks Wander";
		EXPECT_TRUE(selected->haulFromInventory);
		EXPECT_EQ(selected->haulDestinationId, static_cast<uint64_t>(destBox));
		EXPECT_EQ(selected->haulQuantity, 7U) << "sized to what B still takes";

		// It is the delivery the colonist is already on: kept as it is, not cleared and re-selected.
		EXPECT_EQ(task->type, TaskType::Haul);
		EXPECT_FALSE(task->haulFromInventory) << "the deposit leg was kept, not replaced by a fresh carry-in";
		EXPECT_EQ(task->chainStep, 1);
		EXPECT_EQ(task->haulDestinationId, static_cast<uint64_t>(destBox)) << "still delivering to the dest box";
		EXPECT_FLOAT_EQ(task->targetPosition.x, destPos.x) << "still targeting the deposit leg (box B)";
		EXPECT_TRUE(inventory->isHolding("Wood")) << "the two-hand armful stayed in hands (not stranded by a wander)";
		EXPECT_EQ(ecs::handHeldQuantity(*inventory, "Wood"), 14U) << "the full armful is intact -- nothing dropped";

		// Arrived at B, the deposit stops at B's max and the rest stays in hand.
		world->registerSystem<ActionSystem>(kTestRngSeed);
		world->addComponent<Action>(colonist, Action{});
		world->getComponent<Position>(colonist)->value = destPos;
		task->state = TaskState::Arrived;
		for (int frame = 0; frame < 20 && task->type == TaskType::Haul; ++frame) {
			world->update(0.25F);
		}
		EXPECT_EQ(world->getComponent<Inventory>(destBox)->getQuantity("Wood"), 20U) << "never past the configured max";
		EXPECT_EQ(ecs::handHeldQuantity(*inventory, "Wood"), 7U) << "the surplus stays carried";
	}

	// 9c. The broadening must NOT over-hold: a strictly-HIGHER-tier challenger (a critical need, tier 2)
	//     must still preempt the mid-carry two-hand haul (tier 6). Dropping the load for an emergency is
	//     acceptable; the guard only blocks same-or-lower-priority challengers. Guards the broadened
	//     condition from pinning a colonist on a haul through a genuine emergency.
	TEST_F(AIDecisionSystemTest, TwoHandPullYieldsToCriticalNeedMidCarry) {
		auto& registry = assets::AssetRegistry::Get();
		assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = assets::ItemCategory::RawMaterial;
		woodDef.capabilities.carryable = assets::CarryableCapability{1};
		woodDef.itemProperties = assets::ItemProperties{};
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
		addKnownEntity(colonist, srcPos, boxDefId, assets::CapabilityType::Storage);
		addKnownEntity(colonist, destPos, boxDefId, assets::CapabilityType::Storage);
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
		task->haulItemDefNameId = assets::AssetRegistry::Get().getDefNameId("Wood");
		task->haulQuantity = carried;
		task->haulSourceStorageId = static_cast<uint64_t>(srcBox);
		task->haulSourcePosition = srcPos;
		task->haulDestinationId = static_cast<uint64_t>(destBox);
		task->haulTargetPosition = destPos;
		task->haulFromInventory = false;
		task->targetPosition = destPos;
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
		auto& registry = assets::AssetRegistry::Get();
		auto [bushId, stickId] = registerHarvestableYielding(registry, "Flora_HystBush", "HystStick");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist)); // idle of needs: only the stocking harvest competes

		// Two equidistant harvestable sources (30 m each). The distance curve is 5 score/m, so the 50
		// margin is worth ~10 m: B at the same distance can never overcome A's stickiness.
		addKnownEntity(colonist, {30.0F, 0.0F}, bushId, assets::CapabilityType::Harvestable);  // A
		addKnownEntity(colonist, {0.0F, 30.0F}, bushId, assets::CapabilityType::Harvestable);  // B

		// A stocking goal makes both bushes Available tier-6 harvest options.
		(void)stickId;
		makeStockingHarvest({2.0F, 2.0F}, "HystStick", /*minAmount=*/100);

		// First re-eval: the system commits to one of the two equidistant bushes (deterministic via
		// tiebreakId). Capture that target; it is now the in-progress option.
		world->update(0.5F);
		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::Harvest) << "colonist commits to the stocking harvest";
		const uint64_t committedTarget = task->harvestTargetKey;
		ASSERT_NE(committedTarget, 0U) << "a concrete harvest target was selected";

		// Second re-eval with the equidistant sibling still present: the in-progress target holds.
		// Its +50 hysteresis margin is unbeatable by an equal-distance, equal-skill challenger.
		world->update(0.5F);
		EXPECT_EQ(task->harvestTargetKey, committedTarget)
			<< "an equidistant same-tier sibling must not overcome the hysteresis margin (no thrash)";

		// Now introduce a clearly-closer source (5 m vs 30 m: 25 m closer, far beyond the ~10 m margin).
		// Its distance factor (~275) beats the in-progress score (150 + 50). The next re-eval switches.
		addKnownEntity(colonist, {5.0F, 0.0F}, bushId, assets::CapabilityType::Harvestable);  // C
		world->update(0.5F);
		EXPECT_EQ(task->type, TaskType::Harvest);
		EXPECT_NE(task->harvestTargetKey, committedTarget)
			<< "a clearly-closer same-tier target overcomes hysteresis and wins the switch";
	}

	// The gather-food option (emitted when the colonist has no food and knows an edible harvestable)
	// classifies at tier 7 (idle) and, with my wander-floor fix,
	// outranks the same-tier wander option because wander's within-tier score is pinned to 0.
	TEST_F(AIDecisionSystemTest, GatherFoodClassifiesTier7AndBeatsWander) {
		auto& registry = assets::AssetRegistry::Get();
		// A harvestable yielding the already-registered edible Berry, so the gather-food path fires.
		assets::AssetDefinition berryBush;
		berryBush.defName = "Flora_BerryBush";
		berryBush.label = "Berry Bush";
		berryBush.capabilities.harvestable = assets::HarvestableCapability{};
		berryBush.capabilities.harvestable->yieldDefName = "Berry";
		berryBush.capabilities.harvestable->requiredToolType = "";
		registry.registerTestDefinition(std::move(berryBush));
		const uint32_t berryBushId = registry.getDefNameId("Flora_BerryBush");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist)); // all needs met -> idle tier; no food in inventory
		// A nearby edible harvestable: drives the gather-food option (and a reachable food source).
		addKnownEntity(colonist, {3.0F, 0.0F}, berryBushId, assets::CapabilityType::Harvestable);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);

		const auto* gatherFood = findOption(*trace, [](const EvaluatedOption& o) { return o.gathersFood; });
		const auto* wander = findOption(*trace, [](const EvaluatedOption& o) { return o.taskType == TaskType::Wander; });
		ASSERT_NE(gatherFood, nullptr) << "an empty-inventory colonist with a known edible source gathers food";
		ASSERT_NE(wander, nullptr) << "wander is always offered as the idle floor";

		EXPECT_EQ(gatherFood->tier, 7) << "gathering food is tier 7 (idle)";
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
		addKnownEntity(colonist, {3.0F, 4.0F}, kWaterDefId, assets::CapabilityType::Drinkable);

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
	// Need source selection: the Hunger option targets known EDIBLE food (WOR-605)
	// =============================================================================

	namespace {
		// A tool-less harvestable yielding the fixture's edible Berry.
		[[nodiscard]] uint32_t registerBerryBush(assets::AssetRegistry& registry) {
			assets::AssetDefinition berryBush;
			berryBush.defName = "Flora_BerryBush";
			berryBush.label = "Berry Bush";
			berryBush.capabilities.harvestable = assets::HarvestableCapability{};
			berryBush.capabilities.harvestable->yieldDefName = "Berry";
			registry.registerTestDefinition(std::move(berryBush));
			return registry.getDefNameId("Flora_BerryBush");
		}

		// The real Hunger option, not the gather-food option that shares its need type.
		[[nodiscard]] bool isHungerNeedOption(const EvaluatedOption& o) {
			return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Hunger && !o.gathersFood;
		}
	} // namespace

	// Hungry with an empty pack and a work order on offer: the nearest known harvestable yields sticks,
	// a berry bush sits a little farther off. Hunger targets the berry bush, never reading the stick
	// bush as "no food source". Critical hunger (tier 2) preempts the work order (tier 4); routine
	// hunger (tier 5) is on offer below it.
	struct HungerLevelCase {
		float hunger;
		int	  tier;
		bool  preemptsWork;
	};

	class HungerEdibleSourceTest : public AIDecisionSystemTest, public ::testing::WithParamInterface<HungerLevelCase> {};

	TEST_P(HungerEdibleSourceTest, TargetsEdibleSourceOverNearerNonEdibleHarvestable) {
		const HungerLevelCase level = GetParam();
		auto&				  registry = assets::AssetRegistry::Get();
		auto [stickBushId, stickId] = registerHarvestableYielding(registry, "Flora_WorkBush", "WorkStick");
		const uint32_t	berryBushId = registerBerryBush(registry);
		const glm::vec2 berryPos{4.0F, 0.0F};

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		setNeedValue(colonist, NeedType::Hunger, level.hunger);
		addKnownEntity(colonist, {2.0F, 0.0F}, stickBushId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, berryPos, berryBushId, assets::CapabilityType::Harvestable);

		// A work order wanting sticks: a tier-4 Harvest of the nearer bush.
		const auto station = world->createEntity();
		world->addComponent<Position>(station, Position{{6.0F, 0.0F}});
		makeStationNeed(station, "WorkStick", 2);
		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.destinationPosition = {6.0F, 0.0F};
		craft.status = GoalStatus::Blocked;
		const uint64_t craftId = GoalTaskRegistry::Get().createGoal(std::move(craft));
		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::CraftingGoalSystem;
		harvest.destinationEntity = station;
		harvest.destinationPosition = {6.0F, 0.0F};
		harvest.yieldDefNameId = stickId;
		harvest.parentGoalId = craftId;
		harvest.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(harvest));

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* work = findOption(*trace, [](const EvaluatedOption& o) { return o.taskType == TaskType::Harvest; });
		ASSERT_NE(work, nullptr) << "the work order offers a harvest";
		const auto* hunger = findOption(*trace, isHungerNeedOption);
		ASSERT_NE(hunger, nullptr);
		EXPECT_EQ(hunger->tier, level.tier);
		ASSERT_TRUE(hunger->targetPosition.has_value());
		EXPECT_EQ(*hunger->targetPosition, berryPos) << "hunger targets the edible source, not the nearer stick bush";
		if (level.preemptsWork) {
			EXPECT_EQ(hunger->status, OptionStatus::Selected) << "critical hunger preempts the work order";
			EXPECT_EQ(work->status, OptionStatus::Available);
		} else {
			EXPECT_EQ(hunger->status, OptionStatus::Available) << "routine hunger waits behind the work order";
			EXPECT_EQ(work->status, OptionStatus::Selected);
		}
	}

	INSTANTIATE_TEST_SUITE_P(
		HungerLevels, HungerEdibleSourceTest,
		::testing::Values(HungerLevelCase{5.0F, 2, true}, HungerLevelCase{40.0F, 5, false})
	);

	// Knowing only harvestables that aren't food is the one case a hungry colonist has no source:
	// Hunger and gather-food both stay NoSource rather than targeting the stick bush.
	TEST_F(AIDecisionSystemTest, HungerHasNoSourceWhenNoKnownHarvestableIsEdible) {
		auto& registry = assets::AssetRegistry::Get();
		const uint32_t stickBushId = registerHarvestableYielding(registry, "Flora_StickBush", "Stick").harvestableId;

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		setNeedValue(colonist, NeedType::Hunger, 5.0F);
		addKnownEntity(colonist, {2.0F, 0.0F}, stickBushId, assets::CapabilityType::Harvestable);

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* hunger = findOption(*trace, isHungerNeedOption);
		ASSERT_NE(hunger, nullptr);
		EXPECT_EQ(hunger->status, OptionStatus::NoSource);
		const auto* gather = findOption(*trace, [](const EvaluatedOption& o) {
			return o.gathersFood;
		});
		ASSERT_NE(gather, nullptr);
		EXPECT_EQ(gather->status, OptionStatus::NoSource);
	}

	// A remembered bush on its regrowth cooldown has nothing to give. A hungry colonist passes it over
	// for a ready bush farther off, and a work order's Harvest does the same, showing the skipped
	// source as regrowing on the trace.
	TEST_F(AIDecisionSystemTest, RegrowingSourceIsPassedOverForAReadyOne) {
		auto&			registry = assets::AssetRegistry::Get();
		const uint32_t	berryBushId = registerBerryBush(registry);
		auto [stickBushId, stickId] = registerHarvestableYielding(registry, "Flora_WorkBush", "WorkStick");
		const glm::vec2 regrowingBerries{2.0F, 0.0F};
		const glm::vec2 readyBerries{5.0F, 0.0F};
		const glm::vec2 regrowingSticks{0.0F, 2.0F};
		const glm::vec2 readySticks{0.0F, 5.0F};
		int				readinessAsked = 0;
		world->registerSystem<TimeSystem>();
		world->getSystem<AIDecisionSystem>().setSourceRegrowthCallback([&](uint32_t /*defNameId*/, glm::vec2 at, double /*now*/) {
			++readinessAsked;
			return at == regrowingBerries || at == regrowingSticks ? std::optional<double>{1000.0} : std::nullopt;
		});

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		addKnownEntity(colonist, regrowingBerries, berryBushId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, readyBerries, berryBushId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, regrowingSticks, stickBushId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, readySticks, stickBushId, assets::CapabilityType::Harvestable);

		// A work order wanting sticks, so a tier-4 Harvest outranks the tier-5 hunger.
		const auto station = world->createEntity();
		world->addComponent<Position>(station, Position{{0.0F, 0.0F}});
		makeStationNeed(station, "WorkStick", 2);
		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.status = GoalStatus::Blocked;
		const uint64_t craftId = GoalTaskRegistry::Get().createGoal(std::move(craft));
		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::CraftingGoalSystem;
		harvest.destinationEntity = station;
		harvest.yieldDefNameId = stickId;
		harvest.parentGoalId = craftId;
		harvest.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(harvest));

		world->update(0.016F);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* hunger = findOption(*trace, isHungerNeedOption);
		ASSERT_NE(hunger, nullptr);
		ASSERT_TRUE(hunger->targetPosition.has_value());
		EXPECT_EQ(*hunger->targetPosition, readyBerries) << "hunger passes over the nearer, regrowing bush";
		EXPECT_EQ(hunger->status, OptionStatus::Available);

		const auto* skipped = findOption(*trace, [&](const EvaluatedOption& o) {
			return o.taskType == TaskType::Harvest && o.targetPosition == regrowingSticks;
		});
		ASSERT_NE(skipped, nullptr);
		EXPECT_EQ(skipped->status, OptionStatus::Regrowing) << "the trace says why the nearer bush was skipped";
		const auto* selected = trace->getSelected();
		ASSERT_NE(selected, nullptr);
		EXPECT_EQ(selected->taskType, TaskType::Harvest);
		EXPECT_EQ(selected->harvestTargetKey, hashWorldEntity(readySticks, stickBushId)) << "the work harvests the ready bush";
		EXPECT_LE(readinessAsked, 4) << "asked only of the candidates that would otherwise win";
	}

	// With every edible source it knows regrowing, an idle colonist doesn't ask after each of them every
	// decision: the search waits until the first is ready or its memory changes. Neither ever hides a
	// ready source: a newly seen ready bush is taken at once, and a regrowing one once its time comes.
	TEST_F(AIDecisionSystemTest, RegrowingFoodIsNotProbedAgainUntilSomethingChanges) {
		auto&			registry = assets::AssetRegistry::Get();
		const uint32_t	berryBushId = registerBerryBush(registry);
		auto&			time = world->registerSystem<TimeSystem>();
		const glm::vec2 near{2.0F, 0.0F};
		const glm::vec2 far{6.0F, 0.0F};
		const glm::vec2 seenLater{9.0F, 0.0F};
		const std::vector<std::pair<glm::vec2, double>> readyAt{{near, 100.0}, {far, 40.0}};
		int												asked = 0;
		world->getSystem<AIDecisionSystem>().setSourceRegrowthCallback([&](uint32_t /*defNameId*/, glm::vec2 at, double /*now*/) {
			++asked;
			for (const auto& [where, when] : readyAt) {
				if (where == at && time.simulationSeconds() < when) {
					return std::optional<double>{when};
				}
			}
			return std::optional<double>{};
		});

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		addKnownEntity(colonist, near, berryBushId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, far, berryBushId, assets::CapabilityType::Harvestable);
		const auto hungerTarget = [&]() -> std::optional<glm::vec2> {
			const auto* hunger = findOption(*getTrace(colonist), isHungerNeedOption);
			return hunger != nullptr && (hunger->status == OptionStatus::Selected || hunger->status == OptionStatus::Available) ? hunger->targetPosition : std::nullopt;
		};
		const auto decide = [&] {
			getTask(colonist)->timeSinceEvaluation = 1.0F;
			world->update(0.016F);
		};

		decide();
		EXPECT_EQ(asked, 2) << "both bushes asked once";
		EXPECT_FALSE(hungerTarget().has_value());
		for (int i = 0; i < 5; ++i) {
			decide();
		}
		EXPECT_EQ(asked, 2) << "nothing changed, so nothing is asked again";
		const auto* hunger = findOption(*getTrace(colonist), isHungerNeedOption);
		ASSERT_NE(hunger, nullptr);
		EXPECT_EQ(hunger->status, OptionStatus::Regrowing) << "the trace still says what it waits on";

		time.skipTime(41.0F); // the far bush is ready at 40
		decide();
		EXPECT_EQ(hungerTarget(), far) << "taken as soon as its time comes";

		// Back to waiting on the near bush alone, then a ready bush comes into view.
		getNeeds(colonist)->get(NeedType::Hunger).value = 40.0F;
		world->getComponent<Memory>(colonist)->forgetWorldEntity(far, berryBushId);
		decide();
		EXPECT_FALSE(hungerTarget().has_value());
		const int askedBefore = asked;
		decide();
		EXPECT_EQ(asked, askedBefore);
		addKnownEntity(colonist, seenLater, berryBushId, assets::CapabilityType::Harvestable);
		decide();
		EXPECT_EQ(hungerTarget(), seenLater) << "a newly remembered ready bush is taken at once";
	}

	// The wait holds with the real VisionSystem looking on: a storage box and a loose pile beside the
	// colonist live only in the ECS, and seeing them again each vision tick must not read as a change
	// to what it remembers, or the wait would drop and every regrowing bush be asked again each decision.
	TEST_F(AIDecisionSystemTest, RegrowingFoodWaitHoldsBesideAPileAndABox) {
		auto&			registry = assets::AssetRegistry::Get();
		const uint32_t	berryBushId = registerBerryBush(registry);
		const uint32_t	boxId = registry.registerSyntheticDefinition(
			 "Test_WaitBox", static_cast<uint16_t>(1U << static_cast<size_t>(assets::CapabilityType::Storage))
		 );
		const uint32_t pileId = registry.registerSyntheticDefinition(
			"Test_WaitPile", static_cast<uint16_t>(1U << static_cast<size_t>(assets::CapabilityType::Carryable))
		);
		const glm::vec2 bushAt{4.0F, 0.0F};
		const glm::vec2 boxAt{1.0F, 0.0F};
		const glm::vec2 pileAt{0.0F, 1.0F};

		assets::PlacementExecutor		  executor(registry);
		assets::AsyncChunkPlacementResult chunk;
		chunk.coord = world::worldToChunk({bushAt.x, bushAt.y});
		chunk.spatialIndex.insert({"Flora_BerryBush", bushAt});
		executor.storeChunkResult(std::move(chunk));
		const std::unordered_set<world::ChunkCoordinate> processed{world::worldToChunk({bushAt.x, bushAt.y})};
		auto&											 vision = world->registerSystem<VisionSystem>();
		vision.setPlacementData(&executor, &processed);
		vision.setUpdateInterval(1);

		world->registerSystem<TimeSystem>();
		int asked = 0;
		world->getSystem<AIDecisionSystem>().setSourceRegrowthCallback([&](uint32_t /*defNameId*/, glm::vec2 /*at*/, double /*now*/) {
			++asked;
			return std::optional<double>{1000.0};
		});

		for (const auto& [defName, at] : {std::pair{"Test_WaitBox", boxAt}, std::pair{"Test_WaitPile", pileAt}}) {
			const auto entity = world->createEntity();
			world->addComponent<Position>(entity, Position{at});
			world->addComponent<Appearance>(entity, Appearance{defName, 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		}
		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		const auto decide = [&] {
			getTask(colonist)->timeSinceEvaluation = 1.0F;
			world->update(0.016F);
		};

		decide();
		const Memory* memory = world->getComponent<Memory>(colonist);
		ASSERT_TRUE(memory->knowsWorldEntity(bushAt, berryBushId));
		ASSERT_TRUE(memory->knowsWorldEntity(boxAt, boxId));
		ASSERT_TRUE(memory->knowsWorldEntity(pileAt, pileId));
		ASSERT_EQ(asked, 1) << "the regrowing bush is asked once";
		for (int i = 0; i < 5; ++i) {
			decide();
		}
		EXPECT_EQ(asked, 1) << "five vision ticks over an unchanged box and pile leave the wait standing";
		const auto* hunger = findOption(*getTrace(colonist), isHungerNeedOption);
		ASSERT_NE(hunger, nullptr);
		EXPECT_EQ(hunger->status, OptionStatus::Regrowing);
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
			assets::PriorityConfig::Get().clear();

			auto& registry = assets::AssetRegistry::Get();

			// Register Berry as edible entity for testing (unified entity/item model)
			assets::AssetDefinition berryDef;
			berryDef.defName = "Berry";
			berryDef.label = "Berry";
			berryDef.itemProperties = assets::ItemProperties{};
			berryDef.itemProperties->stackSize = 20;
			berryDef.itemProperties->edible = assets::EdibleCapability{0.3F, assets::CapabilityQuality::Normal, true};
			registry.registerTestDefinition(std::move(berryDef));

			// Register BOTH systems to test their interaction
			// This is critical - the bug only manifests when both systems run
			auto& recipeRegistry = assets::RecipeRegistry::Get();
			world->registerSystem<AIDecisionSystem>(registry, recipeRegistry, kTestRngSeed);
			world->registerSystem<ActionSystem>(kTestRngSeed);
		}

		void TearDown() override {
			// Clean up test definitions
			assets::AssetRegistry::Get().clearDefinitions();
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
			world->addComponent<SourceReachMemo>(entity, SourceReachMemo{});
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

	/// A hungry colonist whose only known food is a bush it cut a moment ago neither starts a harvest that
	/// would pay nothing nor stalls: it takes its next option, and harvests the bush once its regrowth time
	/// has passed on the game clock. The bush, its regrowth, and the clock are the game's own pieces.
	TEST_F(AIActionIntegrationTest, RegrowingOnlyFoodIsHarvestedOnceReady) {
		auto&					registry = assets::AssetRegistry::Get();
		assets::AssetDefinition bush;
		bush.defName = "Flora_BerryBush";
		bush.label = "Berry Bush";
		bush.capabilities.harvestable = assets::HarvestableCapability{};
		bush.capabilities.harvestable->yieldDefName = "Berry";
		bush.capabilities.harvestable->amountMin = 3;
		bush.capabilities.harvestable->amountMax = 3;
		bush.capabilities.harvestable->durability = 50.0F; // 5 s, so the harvest spans frames at 10x
		bush.capabilities.harvestable->destructive = false;
		bush.capabilities.harvestable->regrowthTime = 60.0F;
		registry.registerTestDefinition(std::move(bush));
		const uint32_t bushId = registry.getDefNameId("Flora_BerryBush");

		const glm::vec2		  bushAt{0.0F, 0.0F};
		assets::PlacementExecutor executor(registry);
		assets::AsyncChunkPlacementResult chunk;
		chunk.coord = world::worldToChunk({bushAt.x, bushAt.y});
		chunk.spatialIndex.insert({"Flora_BerryBush", bushAt});
		executor.storeChunkResult(std::move(chunk));

		auto& time = world->registerSystem<TimeSystem>();
		time.setSpeed(GameSpeed::VeryFast);
		std::vector<EntityID> pendingRemoval;
		world->getSystem<AIDecisionSystem>().setSourceRegrowthCallback([&](uint32_t defNameId, glm::vec2 at, double now) {
			return worldSourceRegrowingUntil(executor, defNameId, at, now);
		});
		world->getSystem<ActionSystem>().setTakeSourceCallback(
			[&](const std::string& defName, float x, float y, ActionSystem::SourceTake take, float seconds) {
				return takeWorldSource(executor, *world, pendingRemoval, defName, {x, y}, take, seconds);
			}
		);
		const auto bushReady = [&] { return !worldSourceRegrowingUntil(executor, bushId, bushAt, time.simulationSeconds()).has_value(); };

		// Someone just cut it.
		ASSERT_EQ(
			takeWorldSource(executor, *world, pendingRemoval, "Flora_BerryBush", bushAt, ActionSystem::SourceTake::Regrow, 60.0F),
			ActionSystem::SourceState::Ready
		);

		// Standing at the bush, so a hunger task arrives at once and its harvest starts.
		auto colonist = createColonist(bushAt);
		world->getComponent<Memory>(colonist)->rememberWorldEntity(bushAt, bushId, registry.getCapabilityMask(bushId));
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		// 0.1 s frames at 10x: one game second a frame.
		for (int frame = 0; frame < 55; ++frame) {
			world->update(0.1F);
			ASSERT_NE(getAction(colonist)->type, ActionType::Harvest) << "no harvest of a regrowing bush, frame " << frame;
		}
		EXPECT_TRUE(getTask(colonist)->isActive()) << "it took another option rather than standing idle";
		const auto& options = world->getComponent<DecisionTrace>(colonist)->options;
		EXPECT_TRUE(std::ranges::any_of(options, [](const EvaluatedOption& o) {
			return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Hunger && o.status == OptionStatus::Regrowing;
		})) << "the trace shows the food it is waiting on";

		bool harvested = false;
		for (int frame = 0; frame < 20 && !harvested; ++frame) {
			world->update(0.1F);
			harvested = getAction(colonist)->type == ActionType::Harvest;
		}
		ASSERT_TRUE(harvested) << "the bush is harvested once it is ready";
		EXPECT_GE(time.simulationSeconds(), 60.0) << "and not before its regrowth time";

		for (int frame = 0; frame < 20 && bushReady(); ++frame) {
			world->update(0.1F);
		}
		EXPECT_FALSE(bushReady()) << "the harvest took the bush, starting its next regrowth";
		EXPECT_TRUE(
			world->getComponent<Inventory>(colonist)->getQuantity("Berry") > 0 || getNeeds(colonist)->hunger().value > 40.0F
		) << "and paid its berries";
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

			auto& registry = assets::AssetRegistry::Get();
			assets::AssetDefinition axeDef;
			axeDef.defName = "Axe";
			axeDef.label = "Axe";
			axeDef.handsRequired = 1;
			axeDef.category = assets::ItemCategory::Tool;
			axeDef.itemProperties = assets::ItemProperties{};
			axeDef.itemProperties->stackSize = 1;
			axeDef.itemProperties->massKg = 1.0F;
			registry.registerTestDefinition(std::move(axeDef));

			assets::ActionTypeDef harvestAction;
			harvestAction.defName = "Harvest";
			harvestAction.needsHands = true;
			assets::ActionTypeRegistry::Get().registerTestAction(std::move(harvestAction));

			// Capture drops through the real callback the handler fires.
			world->getSystem<AIDecisionSystem>().setDropResourceCallback(
				[this](const std::string& defName, float /*x*/, float /*y*/, uint32_t quantity, glm::vec2) {
					++dropCalls;
					droppedDef = defName;
					droppedQty = quantity;
				}
			);
		}

		std::string droppedDef;
		uint32_t	droppedQty = 0;
		int			dropCalls = 0;

		void TearDown() override {
			assets::ActionTypeRegistry::Get().clear();
			AIDecisionSystemTest::TearDown();
		}

		// Put the colonist mid-Haul, holding the Axe in one hand (chainStep=1).
		Task* armMidHaulCarryingAxe(EntityID colonist) {
			auto* task = getTask(colonist);
			task->type = TaskType::Haul;
			task->chainStep = 1;
			task->haulItemDefNameId = assets::AssetRegistry::Get().getDefNameId("Axe");
			task->state = TaskState::Moving;
			return task;
		}

		// Drive the real handler for a switch to a hands-needing Harvest task.
		void interruptForHarvest(EntityID colonist, Task& task) {
			auto* inventory = world->getComponent<Inventory>(colonist);
			auto* position = world->getComponent<Position>(colonist);
			callHandleChainInterruption(colonist, task, *inventory, *position, TaskType::Harvest, NeedType::Count);
		}
	};

	// A switch to Build reads Build's own action, which needs the hands: the carried item is stowed as for
	// any other hands-on work, not kept in hand on an unknown action.
	TEST_F(ChainInterruptionFixture, ASwitchToBuildFreesTheHands) {
		assets::ActionTypeDef buildAction;
		buildAction.defName = "Build";
		buildAction.needsHands = true;
		assets::ActionTypeRegistry::Get().registerTestAction(std::move(buildAction));
		auto  colonist = createColonist({5.0F, 5.0F});
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->pickUp("Axe", 1);
		auto* task = armMidHaulCarryingAxe(colonist);
		callHandleChainInterruption(colonist, *task, *inventory, *world->getComponent<Position>(colonist), TaskType::Build, NeedType::Count);
		EXPECT_FALSE(inventory->isHolding("Axe"));
		EXPECT_TRUE(inventory->belt[0].has_value());
	}

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
		EXPECT_EQ(droppedQty, 1U);
	}

	// A two-hand armful is one stack mirrored across both hands: an interrupted haul drops it once,
	// all 14 units, not once per hand.
	TEST_F(ChainInterruptionFixture, InterruptedTwoHandArmfulDropsOnceWithWholeQuantity) {
		assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = assets::ItemCategory::RawMaterial;
		woodDef.itemProperties = assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 2.5F;
		assets::AssetRegistry::Get().registerTestDefinition(std::move(woodDef));

		auto  colonist = createColonist({5.0F, 5.0F});
		auto* inventory = world->getComponent<Inventory>(colonist);
		ASSERT_NE(inventory, nullptr);
		inventory->leftHand = ItemStack{"Wood", 14};
		inventory->rightHand = ItemStack{"Wood", 14};

		auto* task = armMidHaulCarryingAxe(colonist);
		task->haulItemDefNameId = assets::AssetRegistry::Get().getDefNameId("Wood");
		interruptForHarvest(colonist, *task);

		EXPECT_FALSE(inventory->isHolding("Wood")) << "Both hands freed";
		EXPECT_EQ(dropCalls, 1) << "Armful dropped once, not once per hand";
		EXPECT_EQ(droppedDef, "Wood");
		EXPECT_EQ(droppedQty, 14U);
	}

	TEST_F(AIDecisionSystemTest, TaskFirstActionNeedsHandsMapping) {
		// Unit test: Verify ActionTypeRegistry returns expected needsHands values.
		// The getFirstActionDefName() helper maps TaskType→ActionDefName, and
		// ActionTypeRegistry provides the needsHands property from XML config.

		auto& actionRegistry = assets::ActionTypeRegistry::Get();

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
		auto& registry = assets::AssetRegistry::Get();

		// A light, one-hand, carryable RawMaterial with a 40 stack. Light enough (0.1 kg) that
		// carry weight (350/trip at 35 kg) never binds, so the destination's addableCount is the
		// sole cap and the slot-vs-item distinction is unambiguous.
		assets::AssetDefinition pelletDef;
		pelletDef.defName = "Pellet";
		pelletDef.label = "Pellet";
		pelletDef.handsRequired = 1;
		pelletDef.category = assets::ItemCategory::RawMaterial;
		pelletDef.capabilities.carryable = assets::CarryableCapability{1};
		pelletDef.itemProperties = assets::ItemProperties{};
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
		addKnownEntity(colonist, {2.0F, 0.0F}, pelletId, assets::CapabilityType::Carryable);

		// A 5-slot storage with an empty Inventory, plus the StorageGoalSystem-style Haul goal
		// pointing at it. The value under test is that the PROPOSED haul is sized by the box's room
		// in items (200), not its 5 slots.
		auto	  storage = world->createEntity();
		world->addComponent<Position>(storage, Position{{4.0F, 0.0F}});
		Inventory storageInv;
		storageInv.maxCapacity = 5;
		world->addComponent<Inventory>(storage, storageInv);
		world->addComponent<StorageConfiguration>(storage, StorageConfiguration::createAcceptAll({assets::ItemCategory::RawMaterial}));

		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = storage;
		goal.destinationPosition = {4.0F, 0.0F};
		goal.acceptedCategory = assets::ItemCategory::RawMaterial;
		goal.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::Haul) << "colonist takes the storage haul when all needs are met";
		EXPECT_EQ(assets::AssetRegistry::Get().getDefName(task->haulItemDefNameId), "Pellet");
		EXPECT_EQ(task->haulDestinationId, static_cast<uint64_t>(storage));
		EXPECT_EQ(task->haulQuantity, 200U)
			<< "sized by the storage's addableCount (5 slots x 40 stack = 200), not the 5-slot count";
		EXPECT_GT(task->haulQuantity, 5U) << "not capped at the free-slot count";
	}

	// A storage haul whose pickup would lift nothing is not on offer, or the colonist re-picks it from
	// point-blank range and repeats a pickup that collects nothing (the action ends a haul whose pickup
	// came up empty). Two ways to have no room: a pack at its weight cap, and hands busy with an armful
	// of another two-hand good. The trace says which haul is waiting and why.
	TEST_F(AIDecisionSystemTest, StorageHaulNeedsRoomToCarryNow) {
		auto& registry = assets::AssetRegistry::Get();
		const auto registerItem = [&](const char* name, uint32_t hands, float massKg) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.handsRequired = hands;
			def.category = assets::ItemCategory::RawMaterial;
			def.capabilities.carryable = assets::CarryableCapability{1};
			def.itemProperties = assets::ItemProperties{};
			def.itemProperties->stackSize = 40;
			def.itemProperties->massKg = massKg;
			registry.registerTestDefinition(std::move(def));
		};
		registerItem("Pellet", 1, 0.1F);
		registerItem("Ballast", 1, 1.5F);
		registerItem("Plank", 2, 1.0F);
		registerItem("Log", 2, 1.0F);
		const uint32_t pelletId = registry.getDefNameId("Pellet");
		const uint32_t plankId = registry.getDefNameId("Plank");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, {2.0F, 0.0F}, pelletId, assets::CapabilityType::Carryable);
		addKnownEntity(colonist, {0.0F, 2.0F}, plankId, assets::CapabilityType::Carryable);

		auto storage = world->createEntity();
		world->addComponent<Position>(storage, Position{{4.0F, 0.0F}});
		Inventory storageInv;
		storageInv.maxCapacity = 5;
		world->addComponent<Inventory>(storage, storageInv);
		// It takes Pellet and Plank only, so the Ballast and Logs the colonist carries are not stocked into it.
		StorageConfiguration storageConfig;
		storageConfig.addRule(StorageRule{.defName = "Pellet", .category = assets::ItemCategory::RawMaterial});
		storageConfig.addRule(StorageRule{.defName = "Plank", .category = assets::ItemCategory::RawMaterial});
		world->addComponent<StorageConfiguration>(storage, std::move(storageConfig));
		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = storage;
		goal.destinationPosition = {4.0F, 0.0F};
		goal.acceptedCategory = assets::ItemCategory::RawMaterial;
		goal.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		const auto statusOf = [&](const char* item) {
			const auto* option = findOption(*getTrace(colonist), [&](const EvaluatedOption& o) {
				return o.taskType == TaskType::Haul && o.haulItemDefNameId == assets::AssetRegistry::Get().getDefNameId(item);
			});
			return option != nullptr ? std::optional<OptionStatus>{option->status} : std::nullopt;
		};

		// Pack at its weight cap (30 x 1.5 kg > 35 kg), hands holding a Log armful.
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Ballast", 30);
		inventory->leftHand = ItemStack{"Log", 1};
		inventory->rightHand = ItemStack{"Log", 1};
		world->update(0.016F);
		EXPECT_EQ(statusOf("Pellet"), OptionStatus::NoRoom) << "a full pack takes no Pellet";
		EXPECT_EQ(statusOf("Plank"), OptionStatus::NoRoom) << "hands busy with Logs take no Plank";
		EXPECT_NE(getTask(colonist)->type, TaskType::Haul);

		// Room again: both hauls are offered.
		inventory->removeItem("Ballast", 30);
		inventory->leftHand.reset();
		inventory->rightHand.reset();
		getTask(colonist)->timeSinceEvaluation = 1.0F;
		world->update(0.016F);
		EXPECT_TRUE(statusOf("Pellet") == OptionStatus::Available || statusOf("Pellet") == OptionStatus::Selected);
		EXPECT_TRUE(statusOf("Plank") == OptionStatus::Available || statusOf("Plank") == OptionStatus::Selected);
		EXPECT_EQ(getTask(colonist)->type, TaskType::Haul) << "and the colonist takes one";
	}

	// Goods a colonist carries that a box wants are delivered to it: a pack full of Ballast goes into a
	// box that takes raw materials.
	TEST_F(AIDecisionSystemTest, CarriedGoodsABoxWantsAreDelivered) {
		auto&					registry = assets::AssetRegistry::Get();
		assets::AssetDefinition ballast;
		ballast.defName = "Ballast";
		ballast.label = "Ballast";
		ballast.handsRequired = 1;
		ballast.category = assets::ItemCategory::RawMaterial;
		ballast.capabilities.carryable = assets::CarryableCapability{1};
		ballast.itemProperties = assets::ItemProperties{};
		ballast.itemProperties->stackSize = 40;
		ballast.itemProperties->massKg = 1.5F;
		registry.registerTestDefinition(std::move(ballast));
		world->registerSystem<ActionSystem>(kTestRngSeed);

		auto colonist = createColonist({0.0F, 0.0F});
		world->addComponent<Action>(colonist, Action{});
		satisfyAllNeeds(*getNeeds(colonist));
		auto* inventory = world->getComponent<Inventory>(colonist);
		inventory->addItem("Ballast", 20);

		const glm::vec2 boxAt{4.0F, 0.0F};
		auto			storage = world->createEntity();
		world->addComponent<Position>(storage, Position{boxAt});
		Inventory storageInv;
		storageInv.maxCapacity = 5;
		world->addComponent<Inventory>(storage, storageInv);
		world->addComponent<StorageConfiguration>(storage, StorageConfiguration::createAcceptAll({assets::ItemCategory::RawMaterial}));
		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = storage;
		goal.destinationPosition = boxAt;
		goal.acceptedCategory = assets::ItemCategory::RawMaterial;
		goal.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		world->update(0.016F);
		auto* task = getTask(colonist);
		ASSERT_EQ(task->type, TaskType::Haul);
		EXPECT_TRUE(task->haulFromInventory);
		EXPECT_EQ(assets::AssetRegistry::Get().getDefName(task->haulItemDefNameId), "Ballast");
		EXPECT_EQ(task->haulDestinationId, static_cast<uint64_t>(storage));
		EXPECT_EQ(task->haulQuantity, 20U);

		world->getComponent<Position>(colonist)->value = boxAt;
		task->state = TaskState::Arrived;
		for (int frame = 0; frame < 20 && task->type == TaskType::Haul; ++frame) {
			world->update(0.25F);
		}
		EXPECT_EQ(world->getComponent<Inventory>(storage)->getQuantity("Ballast"), 20U);
		EXPECT_EQ(inventory->getQuantity("Ballast"), 0U);
	}

	// A storage that is genuinely full (no free slot AND every stack at its cap) offers no haul:
	// addableCount is 0, so evaluateHaulOptions emits no option for it and the colonist falls
	// through to another task (here, Wander).
	TEST_F(AIDecisionSystemTest, FullStorageOffersNoHaulOption) {
		auto& registry = assets::AssetRegistry::Get();

		assets::AssetDefinition pelletDef;
		pelletDef.defName = "Pellet";
		pelletDef.label = "Pellet";
		pelletDef.handsRequired = 1;
		pelletDef.category = assets::ItemCategory::RawMaterial;
		pelletDef.capabilities.carryable = assets::CarryableCapability{1};
		pelletDef.itemProperties = assets::ItemProperties{};
		pelletDef.itemProperties->stackSize = 40;
		pelletDef.itemProperties->massKg = 0.1F;
		registry.registerTestDefinition(std::move(pelletDef));
		const uint32_t pelletId = registry.getDefNameId("Pellet");

		auto colonist = createColonist({0.0F, 0.0F});
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		addKnownEntity(colonist, {2.0F, 0.0F}, pelletId, assets::CapabilityType::Carryable);

		// 2-slot storage filled to its 40 cap on both slots: no free slot, no stack headroom.
		auto	  storage = world->createEntity();
		world->addComponent<Position>(storage, Position{{4.0F, 0.0F}});
		Inventory storageInv;
		storageInv.maxCapacity = 2;
		storageInv.addItem("Pellet", 80);
		ASSERT_EQ(storageInv.addableCount("Pellet"), 0U);
		world->addComponent<Inventory>(storage, storageInv);
		world->addComponent<StorageConfiguration>(storage, StorageConfiguration::createAcceptAll({assets::ItemCategory::RawMaterial}));

		GoalTask goal;
		goal.type = TaskType::Haul;
		goal.owner = GoalOwner::StorageGoalSystem;
		goal.destinationEntity = storage;
		goal.destinationPosition = {4.0F, 0.0F};
		goal.acceptedCategory = assets::ItemCategory::RawMaterial;
		goal.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(goal));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_NE(task->type, TaskType::Haul) << "a full storage offers no haul; addableCount is 0";
	}

	// A build site takes only what its manifest still lacks. Material credited straight onto the
	// site (as /api/dev/give does) leaves construction's Haul goal counter open until its next
	// refresh; that stale goal must offer no delivery, or the deposit records 0 and repeats forever.
	TEST_F(AIDecisionSystemTest, NoBuildSiteDeliveryForSatisfiedItem) {
		auto&					registry = assets::AssetRegistry::Get();
		assets::AssetDefinition plank;
		plank.defName = "Plank";
		plank.label = "Plank";
		plank.handsRequired = 1;
		plank.capabilities.carryable = assets::CarryableCapability{1};
		plank.itemProperties = assets::ItemProperties{};
		plank.itemProperties->stackSize = 50;
		plank.itemProperties->massKg = 0.1F;
		registry.registerTestDefinition(std::move(plank));

		auto colonist = createColonist({0.0F, 0.0F});
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);
		world->getComponent<Inventory>(colonist)->addItem("Plank", 3);

		auto site = world->createEntity();
		world->addComponent<Position>(site, Position{{4.0F, 0.0F}});
		StructureBlueprint blueprint;
		blueprint.phase = StructureBlueprint::BuildPhase::AwaitingMaterials;
		blueprint.required = {{"Plank", 4}};
		blueprint.delivered = {{"Plank", 4}};
		world->addComponent<StructureBlueprint>(site, std::move(blueprint));

		GoalTask haul;
		haul.type = TaskType::Haul;
		haul.owner = GoalOwner::ConstructionGoalSystem;
		haul.destinationEntity = site;
		haul.destinationPosition = {4.0F, 0.0F};
		haul.acceptedDefNameIds = {registry.getDefNameId("Plank")};
		haul.parentGoalId = 999;
		haul.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(haul));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_NE(task->type, TaskType::Haul) << "the site already has every Plank it needs";
	}

	// A construction clear Harvest is served by any harvestable inside its footprint, whatever it
	// yields, and by nothing outside it. A bush nearer the colonist but outside is not offered; a bush
	// and a tree inside are, though they yield different items.
	TEST_F(AIDecisionSystemTest, ClearHarvestOffersOnlyHarvestablesInsideItsFootprint) {
		auto& registry = assets::AssetRegistry::Get();

		assets::AssetDefinition bushDef;
		bushDef.defName = "Flora_TestBush";
		bushDef.label = "Test Bush";
		assets::HarvestableCapability cap;
		cap.yieldDefName = "Berry";
		cap.destructive = false;
		cap.regrowthTime = 60.0F;
		bushDef.capabilities.harvestable = cap;
		registry.registerTestDefinition(std::move(bushDef));
		const uint32_t bushId = registry.getDefNameId("Flora_TestBush");
		assets::AssetDefinition treeDef;
		treeDef.defName = "Flora_TestTree";
		treeDef.label = "Test Tree";
		treeDef.capabilities.harvestable = assets::HarvestableCapability{};
		treeDef.capabilities.harvestable->yieldDefName = "Wood";
		registry.registerTestDefinition(std::move(treeDef));
		const uint32_t treeId = registry.getDefNameId("Flora_TestTree");

		auto colonist = createColonist({0.0F, 0.0F});
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 100.0F);
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		const glm::vec2 outside{1.0F, 0.0F};
		const glm::vec2 inside{20.0F, 0.0F};
		const glm::vec2 treeInside{22.0F, 0.0F};
		addKnownEntity(colonist, outside, bushId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, inside, bushId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, treeInside, treeId, assets::CapabilityType::Harvestable);

		GoalTask clear;
		clear.type = TaskType::Harvest;
		clear.owner = GoalOwner::ConstructionGoalSystem;
		clear.destinationPosition = {20.0F, 0.0F};
		clear.clearFootprint = FootprintBounds{{16.0F, -4.0F}, {24.0F, 4.0F}};
		clear.status = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(clear));

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::Harvest);
		EXPECT_EQ(task->harvestTargetKey, hashWorldEntity(inside, bushId)) << "only a blocker inside the footprint serves a clear";
		std::vector<uint64_t> offered;
		for (const auto& option : getTrace(colonist)->options) {
			if (option.taskType == TaskType::Harvest) {
				offered.push_back(option.harvestTargetKey);
			}
		}
		std::sort(offered.begin(), offered.end());
		std::vector<uint64_t> expected{hashWorldEntity(inside, bushId), hashWorldEntity(treeInside, treeId)};
		std::sort(expected.begin(), expected.end());
		EXPECT_EQ(offered, expected) << "both blockers inside, and nothing else";
	}

	// Two defs that yield the same item both serve a harvest goal for it, and a third that yields
	// something else isn't looked at: the goal visits only the entries of the defs that give its item.
	TEST_F(AIDecisionSystemTest, HarvestGoalSeesEveryDefThatYieldsItsItem) {
		auto&	   registry = assets::AssetRegistry::Get();
		const auto registerFlora = [&](const char* name, const char* yield) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.capabilities.harvestable = assets::HarvestableCapability{};
			def.capabilities.harvestable->yieldDefName = yield;
			registry.registerTestDefinition(std::move(def));
			return registry.getDefNameId(name);
		};
		assets::AssetDefinition wood;
		wood.defName = "Wood";
		wood.label = "Wood";
		wood.itemProperties = assets::ItemProperties{};
		wood.category = assets::ItemCategory::RawMaterial;
		wood.itemProperties->stackSize = 40;
		wood.itemProperties->massKg = 0.1F;
		registry.registerTestDefinition(std::move(wood));
		const uint32_t woodId = registry.getDefNameId("Wood");
		const uint32_t oakId = registerFlora("Flora_Oak", "Wood");
		const uint32_t pineId = registerFlora("Flora_Pine", "Wood");
		const uint32_t bushId = registerFlora("Flora_Bush", "Berry");

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, {3.0F, 0.0F}, oakId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, {0.0F, 3.0F}, pineId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, {0.0F, -3.0F}, pineId, assets::CapabilityType::Harvestable);
		for (int i = 0; i < 10; ++i) {
			addKnownEntity(colonist, {-3.0F - static_cast<float>(i), 0.0F}, bushId, assets::CapabilityType::Harvestable);
		}

		makeStockingHarvest({0.0F, 0.0F}, "Wood");

#ifdef DEVELOPMENT_BUILD
		const uint64_t visitsBefore = workSourceVisits();
#endif
		world->update(0.016F);

		std::vector<uint32_t> cut;
		for (const auto& option : getTrace(colonist)->options) {
			if (option.taskType == TaskType::Harvest) {
				EXPECT_EQ(option.harvestYieldDefNameId, woodId);
				cut.push_back(*option.targetDefNameId);
			}
		}
		std::sort(cut.begin(), cut.end());
		std::vector<uint32_t> expected{oakId, pineId, pineId};
		std::sort(expected.begin(), expected.end());
		EXPECT_EQ(cut, expected) << "the oak and both pines, no bush";
#ifdef DEVELOPMENT_BUILD
		EXPECT_EQ(workSourceVisits() - visitsBefore, 3U) << "the bushes are never visited";
#endif
	}

	// A category goal takes every carryable of its category plus the items it names, each pile once
	// however the goal names it (in its category, named too, or named twice), and visits no other
	// item. A goal naming one item twice offers each pile of it once too.
	TEST_F(AIDecisionSystemTest, HaulGoalsOfferEachPileOnce) {
		auto&	   registry = assets::AssetRegistry::Get();
		const auto registerItem = [&](const char* name, assets::ItemCategory category) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.handsRequired = 1;
			def.category = category;
			def.capabilities.carryable = assets::CarryableCapability{1};
			def.itemProperties = assets::ItemProperties{};
			def.itemProperties->stackSize = 40;
			def.itemProperties->massKg = 0.1F;
			registry.registerTestDefinition(std::move(def));
			return registry.getDefNameId(name);
		};
		const uint32_t pelletId = registerItem("Pellet", assets::ItemCategory::RawMaterial);
		const uint32_t breadId = registerItem("Bread", assets::ItemCategory::Food);
		const uint32_t trinketId = registerItem("Trinket", assets::ItemCategory::Tool);

		auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, {2.0F, 0.0F}, pelletId, assets::CapabilityType::Carryable);
		addKnownEntity(colonist, {3.0F, 0.0F}, pelletId, assets::CapabilityType::Carryable);
		addKnownEntity(colonist, {0.0F, 2.0F}, breadId, assets::CapabilityType::Carryable);
		for (int i = 0; i < 10; ++i) {
			addKnownEntity(colonist, {-2.0F - static_cast<float>(i), 0.0F}, trinketId, assets::CapabilityType::Carryable);
		}

		const auto makeBox = [&](glm::vec2 at, std::vector<StorageRule> rules) {
			auto box = world->createEntity();
			world->addComponent<Position>(box, Position{at});
			world->addComponent<Inventory>(box, Inventory::createForStorage());
			StorageConfiguration config;
			for (StorageRule& rule : rules) {
				config.addRule(std::move(rule));
			}
			world->addComponent<StorageConfiguration>(box, std::move(config));
			return box;
		};
		const glm::vec2 categoryAt{4.0F, 4.0F};
		const glm::vec2 namedAt{-4.0F, 4.0F};
		GoalTask category;
		category.type = TaskType::Haul;
		category.owner = GoalOwner::StorageGoalSystem;
		category.destinationEntity = makeBox(categoryAt, {StorageRule{.defName = "*", .category = assets::ItemCategory::RawMaterial},
														  StorageRule{.defName = "Bread", .category = assets::ItemCategory::Food}});
		category.destinationPosition = categoryAt;
		category.acceptedCategory = assets::ItemCategory::RawMaterial;
		category.acceptedDefNameIds = {breadId, pelletId, breadId};
		category.status = GoalStatus::Available;
		const uint64_t categoryGoal = GoalTaskRegistry::Get().createGoal(std::move(category));
		GoalTask named;
		named.type = TaskType::Haul;
		named.owner = GoalOwner::StorageGoalSystem;
		named.destinationEntity = makeBox(namedAt, {StorageRule{.defName = "Pellet", .category = assets::ItemCategory::RawMaterial}});
		named.destinationPosition = namedAt;
		named.acceptedDefNameIds = {pelletId, pelletId};
		named.status = GoalStatus::Available;
		const uint64_t namedGoal = GoalTaskRegistry::Get().createGoal(std::move(named));

#ifdef DEVELOPMENT_BUILD
		const uint64_t visitsBefore = workSourceVisits();
#endif
		world->update(0.016F);

		const auto& options = getTrace(colonist)->options;
		const auto	offered = [&](uint64_t goal, const char* item) {
			 return std::count_if(options.begin(), options.end(), [&](const EvaluatedOption& o) {
				 return o.taskType == TaskType::Haul && o.haulGoalId == goal && o.haulItemDefNameId == assets::AssetRegistry::Get().getDefNameId(item);
			 });
		};
		EXPECT_EQ(offered(categoryGoal, "Pellet"), 2) << "in the category and named: each pile once";
		EXPECT_EQ(offered(categoryGoal, "Bread"), 1) << "named twice: one option";
		EXPECT_EQ(offered(categoryGoal, "Trinket"), 0) << "another category";
		EXPECT_EQ(offered(namedGoal, "Pellet"), 2) << "named twice: each pile once";
		EXPECT_EQ(offered(namedGoal, "Bread"), 0);
#ifdef DEVELOPMENT_BUILD
		EXPECT_EQ(workSourceVisits() - visitsBefore, 2U + 1U + 2U) << "the trinkets are never visited";
#endif
	}

	// A craft fetch must not be offered when the colonist is at/over its carry-weight cap: the
	// Pickup clamps to cargoUnitsThatFit and adds 0, the staged count never rises, and the AI would
	// otherwise re-issue the identical fetch every tick -- an infinite "fetch -> collect 0 -> fetch"
	// loop that never completes the craft (observed in-game with an over-loaded colonist). The fetch
	// branch skips when no unit fits, so the colonist falls through to another task instead of spinning.
	TEST_F(AIDecisionSystemTest, OverweightColonistOffersNoCraftFetch) {
		auto& registry = assets::AssetRegistry::Get();

		// The recipe material to fetch: a one-hand carryable RawMaterial.
		assets::AssetDefinition stoneDef;
		stoneDef.defName = "SmallStone";
		stoneDef.label = "Small Stone";
		stoneDef.handsRequired = 1;
		stoneDef.category = assets::ItemCategory::RawMaterial;
		stoneDef.capabilities.carryable = assets::CarryableCapability{1};
		stoneDef.itemProperties = assets::ItemProperties{};
		stoneDef.itemProperties->stackSize = 100;
		stoneDef.itemProperties->massKg = 1.5F;
		registry.registerTestDefinition(std::move(stoneDef));

		// Heavy ballast UNRELATED to the recipe: the colonist is carrying a load of this (e.g. cargo
		// hauled earlier), so it's at the carry cap when the craft fetch comes up. It must NOT be the
		// fetched material, or the colonist would just deliver it from inventory.
		assets::AssetDefinition ballastDef;
		ballastDef.defName = "Ballast";
		ballastDef.label = "Ballast";
		ballastDef.handsRequired = 1;
		ballastDef.category = assets::ItemCategory::RawMaterial;
		ballastDef.capabilities.carryable = assets::CarryableCapability{1};
		ballastDef.itemProperties = assets::ItemProperties{};
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
		addKnownEntity(colonist, {2.0F, 0.0F}, stoneId, assets::CapabilityType::Carryable);

		// Craft goal needs 2 SmallStone; its child craft-haul goal is Available to fetch.
		const auto station = world->createEntity();
		world->addComponent<Position>(station, Position{{4.0F, 0.0F}});
		makeStationNeed(station, "SmallStone", 2);

		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.destinationPosition = {4.0F, 0.0F};
		craft.status = GoalStatus::Blocked;
		const uint64_t craftId = GoalTaskRegistry::Get().createGoal(std::move(craft));

		GoalTask haul;
		haul.type = TaskType::Haul;
		haul.owner = GoalOwner::CraftingGoalSystem;
		haul.destinationEntity = station;
		haul.destinationPosition = {4.0F, 0.0F};
		haul.acceptedDefNameIds = {stoneId};
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
		auto& registry = assets::AssetRegistry::Get();

		// The yield the harvestable produces: a one-hand carryable RawMaterial.
		assets::AssetDefinition stickDef;
		stickDef.defName = "Stick";
		stickDef.label = "Stick";
		stickDef.handsRequired = 1;
		stickDef.category = assets::ItemCategory::RawMaterial;
		stickDef.capabilities.carryable = assets::CarryableCapability{1};
		stickDef.itemProperties = assets::ItemProperties{};
		stickDef.itemProperties->stackSize = 100;
		stickDef.itemProperties->massKg = 1.5F;
		registry.registerTestDefinition(std::move(stickDef));

		// A tool-less harvestable (a woody bush) whose yield is the Stick above. requiredToolType
		// empty so the tool gate passes and only the carry gate decides.
		assets::AssetDefinition bushDef;
		bushDef.defName = "Flora_WoodyBush";
		bushDef.label = "Woody Bush";
		bushDef.capabilities.harvestable = assets::HarvestableCapability{};
		bushDef.capabilities.harvestable->yieldDefName = "Stick";
		bushDef.capabilities.harvestable->requiredToolType = "";
		registry.registerTestDefinition(std::move(bushDef));

		// Heavy ballast UNRELATED to the yield: the colonist is at its carry cap, so a Stick can't fit.
		assets::AssetDefinition ballastDef;
		ballastDef.defName = "Ballast";
		ballastDef.label = "Ballast";
		ballastDef.handsRequired = 1;
		ballastDef.category = assets::ItemCategory::RawMaterial;
		ballastDef.capabilities.carryable = assets::CarryableCapability{1};
		ballastDef.itemProperties = assets::ItemProperties{};
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
		addKnownEntity(colonist, {2.0F, 0.0F}, bushId, assets::CapabilityType::Harvestable);

		// A craft wanting Stick, and its Harvest child, available.
		const auto station = world->createEntity();
		world->addComponent<Position>(station, Position{{4.0F, 0.0F}});
		makeStationNeed(station, "Stick", 2);
		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.destinationPosition = {4.0F, 0.0F};
		craft.status = GoalStatus::Blocked;

		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::CraftingGoalSystem;
		harvest.destinationEntity = station;
		harvest.destinationPosition = {4.0F, 0.0F};
		harvest.acceptedDefNameIds = {stickId};
		harvest.yieldDefNameId = stickId;
		harvest.parentGoalId = GoalTaskRegistry::Get().createGoal(std::move(craft));
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

	// =============================================================================
	// Direct player control
	// =============================================================================

	// Under player control the AI must NOT pick a task even when a need is actionable and a
	// fulfilling source is known; releasing control resumes normal selection.
	TEST_F(AIDecisionSystemTest, PlayerControlSuppressesTaskSelection) {
		auto colonist = createColonist({0.0F, 0.0F});
		addKnownEntity(colonist, {8.0F, 3.0F}, kWaterDefId, assets::CapabilityType::Drinkable);
		setNeedValue(colonist, NeedType::Hunger, 100.0F);
		setNeedValue(colonist, NeedType::Thirst, 40.0F); // actionable: normally picks a FulfillNeed task
		setNeedValue(colonist, NeedType::Energy, 100.0F);
		setNeedValue(colonist, NeedType::Bladder, 100.0F);

		world->getSystem<AIDecisionSystem>().enterControl(colonist);
		EXPECT_NE(world->getComponent<PlayerControlled>(colonist), nullptr);

		world->update(0.016F);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::None); // selection suppressed: colonist stands and waits

		// Release: the AI resumes and selects the thirst task on the next tick.
		world->getSystem<AIDecisionSystem>().releaseControl(colonist);
		EXPECT_EQ(world->getComponent<PlayerControlled>(colonist), nullptr);

		world->update(0.016F);
		task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->type, TaskType::FulfillNeed);
		EXPECT_EQ(task->needToFulfill, NeedType::Thirst);
	}

	// A player move order sets a Moving task pointed at the goal, and the control gate keeps it
	// (doesn't re-select) across a tick. No NavigationSystem is wired (headless), so the goal is
	// taken as-is rather than snapped.
	TEST_F(AIDecisionSystemTest, PlayerMoveOrderSetsMovingTaskToGoal) {
		auto colonist = createColonist({0.0F, 0.0F});
		auto& ai = world->getSystem<AIDecisionSystem>();
		ai.enterControl(colonist);

		const glm::vec2 goal{5.0F, 7.0F};
		const auto routed = ai.issuePlayerMoveOrder(colonist, goal);
		ASSERT_TRUE(routed.has_value());
		EXPECT_FLOAT_EQ(routed->x, goal.x);
		EXPECT_FLOAT_EQ(routed->y, goal.y);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->state, TaskState::Moving);
		EXPECT_EQ(task->type, TaskType::None);
		EXPECT_FLOAT_EQ(task->targetPosition.x, goal.x);
		EXPECT_FLOAT_EQ(task->targetPosition.y, goal.y);

		auto* movementTarget = world->getComponent<MovementTarget>(colonist);
		ASSERT_NE(movementTarget, nullptr);
		EXPECT_TRUE(movementTarget->active);
		EXPECT_FLOAT_EQ(movementTarget->target.x, goal.x);
		EXPECT_FLOAT_EQ(movementTarget->target.y, goal.y);

		// The control gate preserves the player order: a tick doesn't re-select it away.
		world->update(0.016F);
		task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_EQ(task->state, TaskState::Moving);
		EXPECT_FLOAT_EQ(task->targetPosition.x, goal.x);
		EXPECT_FLOAT_EQ(task->targetPosition.y, goal.y);
	}

	// Taking control mid-action abandons the in-progress Action too, not just the Task. Otherwise
	// ActionSystem (which starts a new action only when the current one is inactive) would later run
	// the stale action against whatever task the colonist picks up after release, applying a
	// mismatched effect. Uses a non-interruptable Drink -- the case the normal task-switch path is
	// structurally forbidden from abandoning, which taking control abandons unconditionally.
	TEST_F(AIDecisionSystemTest, EnterControlClearsInProgressAction) {
		auto colonist = createColonist({0.0F, 0.0F});
		Action drinking;
		drinking.type = ActionType::Drink;
		drinking.state = ActionState::InProgress;
		drinking.interruptable = false;
		world->addComponent<Action>(colonist, drinking);

		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		task->type = TaskType::FulfillNeed;
		task->state = TaskState::Arrived;

		world->getSystem<AIDecisionSystem>().enterControl(colonist);

		auto* action = world->getComponent<Action>(colonist);
		ASSERT_NE(action, nullptr);
		EXPECT_FALSE(action->isActive()); // stale action cleared: won't fire against a later task

		task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		EXPECT_FALSE(task->isActive());
	}

	// =============================================================================
	// Nav holds that must end
	// =============================================================================

	// A Wander that nav stopped (no believed route to its point: CantFindWayTo, movement off) takes
	// the next re-eval's fresh point instead of staying pinned to the unreachable one.
	TEST_F(AIDecisionSystemTest, StoppedWanderRepicksFreshTarget) {
		const auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));

		world->update(0.016F);
		auto* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::Wander);
		const glm::vec2 unreachable = task->targetPosition;

		// What requestNavPath's Blocked outcome leaves on the task and its movement.
		task->navState = NavState::CantFindWayTo;
		getMovementTarget(colonist)->active = false;

		world->update(0.016F); // a stopped colonist re-decides at once, not on the periodic interval
		EXPECT_EQ(task->type, TaskType::Wander);
		EXPECT_NE(task->targetPosition, unreachable) << "re-picked a fresh wander point";
		EXPECT_TRUE(getMovementTarget(colonist)->active);
		EXPECT_EQ(task->navState, NavState::Traveling);
	}

	namespace {
		// Open grassland with one pond at (40, 40); its rim is at most 10.5 m from the center.
		constexpr worldgen::PondNetwork2D::Pond kSlidePond{
			.cx = 40.0, .cy = 40.0, .radius = 8.0F, .phaseA = 0.0F, .phaseB = 0.0F, .depth = 120};

		// Open land cut by two straight rivers, 6 m wide, along y = +20 and y = -20, running past both
		// ends of everything the nav meshes (the shared region is chunks -2..2, x from -1024 to 1536 m):
		// three land components, the band between them (where the colonist stands), the north bank and
		// the south bank.
		constexpr float kRiverY = 20.0F;
		constexpr float kRiverHalfWidth = 3.0F;

		class TwoRiverSampler : public world::IWorldSampler {
		  public:
			[[nodiscard]] world::ChunkSampleResult sampleChunk(world::ChunkCoordinate) const override {
				world::ChunkSampleResult r = world::makeUniformChunkSampleResult(
					world::BiomeWeights::single(world::Biome::TemperateGrassland), 1.0F);
				// Across every loaded chunk: reach is unbounded, so banks joined round a river's end on meshed
				// ground are joined, and these tests need them apart.
				for (const float y : {kRiverY, -kRiverY}) {
					r.riverSegments.push_back({.x0 = -2048.0, .y0 = y, .x1 = 2560.0, .y1 = y, .halfWidth0 = kRiverHalfWidth,
											   .halfWidth1 = kRiverHalfWidth, .s0 = 0.0, .s1 = 4608.0});
				}
				return r;
			}
			[[nodiscard]] float	   sampleElevation(world::WorldPosition) const override { return 1.0F; }
			[[nodiscard]] uint64_t getWorldSeed() const override { return 7U; }
		};

		// The ready radius-2 chunk region around the origin. NavigationSystem writes nothing observable
		// in it and these fixtures wire no placement data, so each sampler's region is generated once
		// per process and shared by every test that wires it.
		std::unique_ptr<world::ChunkManager> generateOriginChunks(std::unique_ptr<world::IWorldSampler> sampler) {
			auto chunks = std::make_unique<world::ChunkManager>(std::move(sampler));
			chunks->setLoadRadius(2);
			chunks->setUnloadRadius(4);
			chunks->update({0.0F, 0.0F});
			chunks->finishPendingGeneration();
			return chunks;
		}

		world::ChunkManager* openLandChunks() {
			static const std::unique_ptr<world::ChunkManager> chunks =
				generateOriginChunks(std::make_unique<world::SyntheticWorldSampler>(7U, world::SyntheticMap{}));
			return chunks.get();
		}

		world::ChunkManager* slidePondChunks() {
			static const std::unique_ptr<world::ChunkManager> chunks =
				generateOriginChunks(std::make_unique<world::SyntheticWorldSampler>(7U, world::SyntheticMap{.pond = kSlidePond}));
			return chunks.get();
		}

		world::ChunkManager* twoRiverChunks() {
			static const std::unique_ptr<world::ChunkManager> chunks = generateOriginChunks(std::make_unique<TwoRiverSampler>());
			return chunks.get();
		}
	} // namespace

	// The running game's first decision can happen before its chunks are ready, so before any ground
	// is meshed. This wires a real NavigationSystem (all-land chunks unless a fixture picks another
	// shared region, a viewport region at the origin, an empty construction world) plus MovementSystem
	// and PhysicsSystem, so a test can hold the chunks back (withholdChunks) to let the AI decide with
	// no mesh, then hand them over and settle the NavigationSystem alone (finishPendingBuilds: no sim
	// time passes, and the AI sees the mesh on the next tick), and then check the colonist walks a route.
	class AIDecisionFirstMeshTest : public AIDecisionSystemTest {
	  protected:
		void SetUp() override {
			AIDecisionSystemTest::SetUp();

			nav = &world->registerSystem<NavigationSystem>();
			nav->setChunkManager(chunkRegion());
			nav->setConstructionWorld(&construction);
			nav->setViewportRect({0.0F, 0.0F}, {60.0F, 60.0F});
			world->getSystem<AIDecisionSystem>().setNavigationSystem(nav);
			world->registerSystem<MovementSystem>();
			world->registerSystem<PhysicsSystem>();
		}

		// The shared region the fixture's NavigationSystem builds over; a fixture that needs a different
		// world returns that sampler's shared region.
		[[nodiscard]] virtual world::ChunkManager* chunkRegion() const { return openLandChunks(); }

		// The chunks aren't ready yet: nothing can be meshed until settleWithChunks hands them over.
		void withholdChunks() { nav->setChunkManager(nullptr); }

		// The chunks are ready: hand them to the NavigationSystem and settle it.
		void settleWithChunks() {
			nav->setChunkManager(chunkRegion());
			nav->finishPendingBuilds();
		}

		[[nodiscard]] bool followingRoute(EntityID colonist) {
			const auto* navPath = world->getComponent<NavPath>(colonist);
			return getMovementTarget(colonist)->active && navPath != nullptr && navPath->valid;
		}

		// Lets an idle colonist pick a wander and walk it until it is actually moving.
		[[nodiscard]] bool walkUntilMoving(EntityID colonist) {
			for (int i = 0; i < 20; ++i) {
				world->update(kTick);
				if (glm::length(world->getComponent<Velocity>(colonist)->value) > 0.5F) {
					return true;
				}
			}
			return false;
		}

		static constexpr float kTick = 0.1F;
		static constexpr int   kTicksAfterMesh = 20; // 2 s, four kReEvalInterval (0.5 s) periods

		// The base TearDown resets the world, and its NavigationSystem waits out any patches in
		// flight, before this member goes away.
		construction::ConstructionWorld		 construction;
		NavigationSystem*							 nav = nullptr;
	};

	// With no mesh (the chunks aren't ready yet) an idle colonist (needs met, no known sources) has
	// nothing it can navigate to, so selectTaskFromTrace parks the no-option hold: a Wander with
	// movement off. Once the mesh lands the re-eval offers a real, reachable Wander, and the parked one
	// has to give way to it.
	TEST_F(AIDecisionFirstMeshTest, IdleHoldTakesLiveWanderOnceMeshLands) {
		const auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		withholdChunks();

		world->update(kTick);
		ASSERT_FALSE(nav->hasMesh()) << "the first decision must run before any mesh exists";
		const Task* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->type, TaskType::Wander);
		ASSERT_FALSE(getMovementTarget(colonist)->active) << "nothing moves without a mesh";

		// The hold keeps the periodic throttle: a tick later the trace hasn't been rebuilt.
		const float firstEvaluation = getTrace(colonist)->lastEvaluationTime;
		world->update(kTick);
		EXPECT_FLOAT_EQ(getTrace(colonist)->lastEvaluationTime, firstEvaluation)
			<< "a parked colonist must not re-decide every frame";

		settleWithChunks();
		ASSERT_TRUE(nav->hasMesh()) << "navmesh never built";

		bool walking = false;
		for (int i = 0; i < kTicksAfterMesh && !walking; ++i) {
			world->update(kTick);
			walking = task->type == TaskType::Wander && followingRoute(colonist);
		}
		EXPECT_TRUE(walking) << "still parked " << kTicksAfterMesh << " ticks after the mesh landed (\"" << task->reason
							 << "\")";
	}

	// A colonist that knows an edible harvestable picks the gather-food option on its first decision,
	// before any mesh exists (the chunks aren't ready yet), and requestNavPath holds that route. When
	// the mesh lands the re-eval
	// re-selects the same option and keeps the task as it is, so the held route has to be requested
	// again or the colonist never moves.
	TEST_F(AIDecisionFirstMeshTest, HeldGatherFoodRouteRequestedOnceMeshLands) {
		auto& registry = assets::AssetRegistry::Get();
		assets::AssetDefinition berryBush;
		berryBush.defName = "Flora_BerryBush";
		berryBush.label = "Berry Bush";
		berryBush.capabilities.harvestable = assets::HarvestableCapability{};
		berryBush.capabilities.harvestable->yieldDefName = "Berry";
		registry.registerTestDefinition(std::move(berryBush));

		const glm::vec2 start{0.0F, 0.0F};
		const glm::vec2 bush{6.0F, 2.0F};
		const auto colonist = createColonist(start);
		satisfyAllNeeds(*getNeeds(colonist)); // idle with an empty inventory: gathering food is the pick
		addKnownEntity(colonist, bush, registry.getDefNameId("Flora_BerryBush"), assets::CapabilityType::Harvestable);
		withholdChunks();

		world->update(kTick);
		ASSERT_FALSE(nav->hasMesh()) << "the first decision must run before any mesh exists";
		const Task* task = getTask(colonist);
		ASSERT_NE(task, nullptr);
		ASSERT_EQ(task->reason, "Gathering food (inventory empty)");
		ASSERT_EQ(task->state, TaskState::Moving);
		ASSERT_FALSE(getMovementTarget(colonist)->active) << "the route is held until a mesh exists";
		EXPECT_EQ(task->navState, NavState::AwaitingMesh) << "the held route shows as waiting, not traveling";

		settleWithChunks();
		ASSERT_TRUE(nav->hasMesh()) << "navmesh never built";

		bool routed = false;
		for (int i = 0; i < kTicksAfterMesh && !routed; ++i) {
			world->update(kTick);
			routed = followingRoute(colonist);
		}
		EXPECT_TRUE(routed) << "no route " << kTicksAfterMesh << " ticks after the mesh landed";
		EXPECT_EQ(task->reason, "Gathering food (inventory empty)") << "the same task, now routed";
		EXPECT_EQ(task->navState, NavState::Traveling);
		const auto* navPath = world->getComponent<NavPath>(colonist);
		ASSERT_NE(navPath, nullptr) << "no route was ever attached";
		ASSERT_FALSE(navPath->waypoints.empty());
		EXPECT_LT(glm::distance(navPath->waypoints.back(), bush), 0.5F) << "the route ends at the bush";

		for (int i = 0; i < 5; ++i) {
			world->update(kTick);
		}
		const glm::vec2 now = world->getComponent<Position>(colonist)->value;
		EXPECT_LT(glm::distance(now, bush), glm::distance(start, bush) - 0.5F) << "the colonist walks toward the bush";
	}

	TEST_F(AIDecisionFirstMeshTest, ControlledColonistWalksToMoveOrder) {
		nav->finishPendingBuilds();
		ASSERT_TRUE(nav->hasMesh()) << "navmesh never built";
		const glm::vec2 start{0.0F, 0.0F};
		const glm::vec2 goal{8.0F, 3.0F};
		const auto		colonist = createColonist(start);
		satisfyAllNeeds(*getNeeds(colonist));

		auto& ai = world->getSystem<AIDecisionSystem>();
		ai.enterControl(colonist);
		ASSERT_TRUE(ai.issuePlayerMoveOrder(colonist, goal).has_value());

		for (int i = 0; i < 20; ++i) { // 2 s at 2 m/s
			world->update(kTick);
		}
		const glm::vec2 now = world->getComponent<Position>(colonist)->value;
		EXPECT_LT(glm::distance(now, goal), glm::distance(start, goal) - 2.0F) << "the move order went nowhere";
	}

	TEST_F(AIDecisionFirstMeshTest, TakingControlOfWalkingColonistStopsIt) {
		nav->finishPendingBuilds();
		ASSERT_TRUE(nav->hasMesh()) << "navmesh never built";
		const auto colonist = createColonist({0.0F, 0.0F});
		satisfyAllNeeds(*getNeeds(colonist));
		ASSERT_TRUE(walkUntilMoving(colonist)) << "the colonist never set off on its wander";

		world->getSystem<AIDecisionSystem>().enterControl(colonist);
		const glm::vec2 heldAt = world->getComponent<Position>(colonist)->value;
		world->update(kTick);

		EXPECT_EQ(glm::length(world->getComponent<Velocity>(colonist)->value), 0.0F) << "still carrying a stride";
		EXPECT_LT(glm::distance(world->getComponent<Position>(colonist)->value, heldAt), 1e-4F)
			<< "the colonist coasted after the player took control";
	}

	// =============================================================================
	// Need actions started in place
	// =============================================================================

	// The whole frame pipeline a need action runs through: AI, nav, movement, physics, and actions,
	// over a built mesh with a pond. A ground-fallback need action (Sleep, Eat from inventory, Toilet)
	// starts in place with the movement target switched off, so whatever velocity the colonist was
	// walking at when it decided must not carry it anywhere for the action's duration.
	class NeedActionInPlaceTest : public AIDecisionFirstMeshTest {
	  protected:
		void SetUp() override {
			AIDecisionFirstMeshTest::SetUp();
			world->registerSystem<ActionSystem>(kTestRngSeed);
			nav->finishPendingBuilds();
			ASSERT_TRUE(nav->hasMesh()) << "navmesh never built";
		}

		[[nodiscard]] world::ChunkManager* chunkRegion() const override { return slidePondChunks(); }

		EntityID createActingColonist(glm::vec2 position) {
			const auto colonist = createColonist(position);
			world->addComponent<Action>(colonist, Action{});
			satisfyAllNeeds(*getNeeds(colonist));
			return colonist;
		}

		struct Stillness {
			float maxDrift = 0.0F;
			float maxSpeed = 0.0F;
			int	  ticks = 0;
		};

		// Steps the world until the colonist's action ends, measuring how far it strays from `spot`.
		Stillness stepThroughAction(EntityID colonist, glm::vec2 spot) {
			const auto* action = world->getComponent<Action>(colonist);
			Stillness	result;
			while (action->isActive() && result.ticks < 200) {
				world->update(kTick);
				++result.ticks;
				result.maxDrift =
					std::max(result.maxDrift, glm::distance(world->getComponent<Position>(colonist)->value, spot));
				result.maxSpeed = std::max(result.maxSpeed, glm::length(world->getComponent<Velocity>(colonist)->value));
			}
			return result;
		}
	};

	TEST_F(NeedActionInPlaceTest, SleepStartedMidStrideStaysPut) {
		const auto colonist = createActingColonist({0.0F, 0.0F});
		ASSERT_TRUE(walkUntilMoving(colonist)) << "the colonist never set off on its wander";

		getNeeds(colonist)->get(NeedType::Energy).value = 20.0F;
		world->update(kTick);
		const Task*	  task = getTask(colonist);
		const Action* action = world->getComponent<Action>(colonist);
		ASSERT_EQ(task->needToFulfill, NeedType::Energy);
		ASSERT_TRUE(action->isActive());
		ASSERT_EQ(action->type, ActionType::Sleep);
		const glm::vec2 bedSpot = task->targetPosition;

		const Stillness still = stepThroughAction(colonist, bedSpot);
		EXPECT_FALSE(action->isActive()) << "the sleep never finished";
		EXPECT_EQ(still.maxSpeed, 0.0F) << "the sleeper kept a walking velocity";
		EXPECT_LT(still.maxDrift, 1e-4F) << "the sleeper slid away from where it lay down";
		EXPECT_FLOAT_EQ(getNeeds(colonist)->energy().value, 50.0F) << "ground sleep restores 30, every time";
	}

	TEST_F(NeedActionInPlaceTest, ToiletStartedMidStrideLeavesBioPileUnderColonist) {
		const auto colonist = createActingColonist({0.0F, 0.0F});
		ASSERT_TRUE(walkUntilMoving(colonist)) << "the colonist never set off on its wander";

		getNeeds(colonist)->get(NeedType::Digestion).value = 20.0F;
		world->update(kTick);
		const Task*	  task = getTask(colonist);
		const Action* action = world->getComponent<Action>(colonist);
		ASSERT_TRUE(action->isActive());
		ASSERT_EQ(action->type, ActionType::Toilet);
		const glm::vec2 squatSpot = task->targetPosition;

		const Stillness still = stepThroughAction(colonist, squatSpot);
		EXPECT_FALSE(action->isActive()) << "the toilet action never finished";
		EXPECT_EQ(still.maxSpeed, 0.0F) << "the colonist kept a walking velocity";
		EXPECT_LT(still.maxDrift, 1e-4F) << "the colonist slid away mid-action";

		std::optional<glm::vec2> bioPile;
		for (auto [entity, position, appearance] : world->view<Position, Appearance>()) {
			if (appearance.defName == "Misc_BioPile") {
				bioPile = position.value;
			}
		}
		ASSERT_TRUE(bioPile.has_value()) << "no BioPile spawned";
		EXPECT_LT(glm::distance(*bioPile, squatSpot), 1e-4F) << "the BioPile isn't where the colonist squatted";
		EXPECT_LT(glm::distance(*bioPile, world->getComponent<Position>(colonist)->value), 1e-4F)
			<< "the BioPile isn't under the colonist when it gets up";
	}

	// A critical sleeper that slides into water is snapped back ashore, which clears its task, so it
	// re-decides; still sliding, it goes straight back in. The sleep must hold one decision throughout.
	TEST_F(NeedActionInPlaceTest, CriticalSleeperDoesNotReDecideEveryTick) {
		const glm::vec2 shore{28.0F, 40.0F}; // land, a couple of metres west of the pond
		ASSERT_TRUE(nav->isOnMesh(shore));
		const auto colonist = createActingColonist(shore);
		world->getComponent<Velocity>(colonist)->value = {2.0F, 0.0F}; // mid-stride, heading for the water

		getNeeds(colonist)->get(NeedType::Energy).value = 5.0F;
		world->update(kTick);
		const Task*	  task = getTask(colonist);
		const Action* action = world->getComponent<Action>(colonist);
		ASSERT_EQ(task->needToFulfill, NeedType::Energy);
		ASSERT_TRUE(action->isActive());
		ASSERT_EQ(action->type, ActionType::Sleep);

		int		  decisions = 0;
		glm::vec2 decidedSpot = task->targetPosition;
		for (int i = 0; i < 200 && action->isActive(); ++i) {
			world->update(kTick);
			if (task->targetPosition != decidedSpot) {
				++decisions;
				decidedSpot = task->targetPosition;
			}
		}
		EXPECT_FALSE(action->isActive()) << "the sleep never finished";
		EXPECT_LE(decisions, 1) << "the sleeper re-decided " << decisions << " times during one sleep";
	}

	// Known sources across a river are out of reach while a farther one on the colonist's side isn't.
	// A need takes the nearest REACHABLE source, however many unreachable ones are nearer (WOR-607).
	class AIDecisionRiverTest : public AIDecisionFirstMeshTest {
	  protected:
		[[nodiscard]] world::ChunkManager* chunkRegion() const override { return twoRiverChunks(); }

		// A colonist between the rivers with the mesh built, both banks proven unreachable and
		// kReachable proven reachable, so a failure is the AI's choice and not the fixture.
		EntityID makeColonistBetweenRivers() {
			const auto colonist = createColonist(kStart);
			satisfyAllNeeds(*getNeeds(colonist));
			nav->finishPendingBuilds();
			EXPECT_TRUE(nav->hasMesh()) << "navmesh never built";
			const auto*						  memory = world->getComponent<Memory>(colonist);
			const geometry::nav::BeliefFilter belief{&memory->knownSegments, &memory->knownOpenings};
			EXPECT_FALSE(isReachable(*nav, kStart, northBank(0.0F), kAgentRadius, belief)) << "the north bank must be unreachable";
			EXPECT_FALSE(isReachable(*nav, kStart, southBank(0.0F), kAgentRadius, belief)) << "the south bank must be unreachable";
			EXPECT_TRUE(isReachable(*nav, kStart, kReachable, kAgentRadius, belief));
			return colonist;
		}

		[[nodiscard]] static glm::vec2 northBank(float x) { return {x, kRiverY + 4.0F}; } // 24 m out
		[[nodiscard]] static glm::vec2 southBank(float x) { return {x, -kRiverY - 4.0F}; }

		[[nodiscard]] static const EvaluatedOption* gatherFoodOption(const DecisionTrace& trace) {
			return findOption(trace, [](const EvaluatedOption& o) {
				return o.gathersFood;
			});
		}

		// Hunger and gather-food both target kReachable, Hunger being the chosen task.
		void expectFoodGoesToReachable(EntityID colonist) {
			const auto* trace = getTrace(colonist);
			ASSERT_NE(trace, nullptr);
			const auto* hunger = findOption(*trace, isHungerNeedOption);
			ASSERT_NE(hunger, nullptr);
			EXPECT_EQ(hunger->status, OptionStatus::Selected);
			ASSERT_TRUE(hunger->targetPosition.has_value());
			EXPECT_EQ(*hunger->targetPosition, kReachable);
			const auto* gather = gatherFoodOption(*trace);
			ASSERT_NE(gather, nullptr);
			EXPECT_EQ(gather->status, OptionStatus::Available);
			ASSERT_TRUE(gather->targetPosition.has_value());
			EXPECT_EQ(*gather->targetPosition, kReachable);
		}

		static constexpr glm::vec2 kStart{0.0F, 0.0F};
		static constexpr glm::vec2 kReachable{40.0F, 0.0F}; // between the rivers, farther than either bank
		static constexpr float	   kAgentRadius = 0.3F;
	};

	// Berry patches across both rivers: the six nearest known bushes are unreachable, four on the north
	// bank and two on the south, so the walk passes several sources on each of two components.
	TEST_F(AIDecisionRiverTest, FoodSkipsEveryUnreachableNearerSource) {
		const uint32_t berryBushId = registerBerryBush(assets::AssetRegistry::Get());
		const auto	   colonist = makeColonistBetweenRivers();
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		for (const float x : {0.0F, 2.0F, -2.0F, 4.0F}) {
			addKnownEntity(colonist, northBank(x), berryBushId, assets::CapabilityType::Harvestable);
		}
		for (const float x : {0.0F, 3.0F}) {
			addKnownEntity(colonist, southBank(x), berryBushId, assets::CapabilityType::Harvestable);
		}
		addKnownEntity(colonist, kReachable, berryBushId, assets::CapabilityType::Harvestable);

		world->update(kTick);
		expectFoodGoesToReachable(colonist);
	}

	TEST_F(AIDecisionRiverTest, ThirstSkipsUnreachableNearestWaterForReachableFartherOne) {
		const auto colonist = makeColonistBetweenRivers();
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1); // no gather-food option in the way
		setNeedValue(colonist, NeedType::Thirst, 40.0F);
		addKnownEntity(colonist, northBank(0.0F), kWaterDefId, assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, kReachable, kWaterDefId, assets::CapabilityType::Drinkable);

		world->update(kTick);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* thirst = findOption(*trace, [](const EvaluatedOption& o) {
			return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Thirst;
		});
		ASSERT_NE(thirst, nullptr);
		EXPECT_EQ(thirst->status, OptionStatus::Selected);
		ASSERT_TRUE(thirst->targetPosition.has_value());
		EXPECT_EQ(*thirst->targetPosition, kReachable);
	}

	// A bed and a toilet the colonist knows but can't reach don't strand Energy and Bladder: they
	// take the ground fallback, as when no source is known.
	TEST_F(AIDecisionRiverTest, UnreachableBedAndToiletFallBackToGround) {
		const auto colonist = makeColonistBetweenRivers();
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		setNeedValue(colonist, NeedType::Energy, 20.0F);
		setNeedValue(colonist, NeedType::Bladder, 20.0F);
		addKnownEntity(colonist, northBank(0.0F), kBedDefId, assets::CapabilityType::Sleepable);
		addKnownEntity(colonist, southBank(0.0F), kToiletDefId, assets::CapabilityType::Toilet);

		world->update(kTick);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		for (const NeedType need : {NeedType::Energy, NeedType::Bladder}) {
			const auto* option = findOption(*trace, [need](const EvaluatedOption& o) {
				return o.taskType == TaskType::FulfillNeed && o.needType == need;
			});
			ASSERT_NE(option, nullptr);
			EXPECT_NE(option->status, OptionStatus::NoSource) << static_cast<int>(need);
			ASSERT_TRUE(option->targetPosition.has_value());
			// Wherever the ground fallback picks, it's on the colonist's side of both rivers.
			EXPECT_LT(std::abs(option->targetPosition->y), kRiverY - kRiverHalfWidth)
				<< static_cast<int>(need) << " targets ground the colonist can reach, not the source across the river";
		}
	}

	// Every known food source out of reach: the colonist has nothing it can eat, so Hunger is NoSource.
	TEST_F(AIDecisionRiverTest, HungerHasNoSourceWhenEveryKnownFoodIsUnreachable) {
		const uint32_t berryBushId = registerBerryBush(assets::AssetRegistry::Get());
		const auto	   colonist = makeColonistBetweenRivers();
		setNeedValue(colonist, NeedType::Hunger, 40.0F);
		addKnownEntity(colonist, northBank(0.0F), berryBushId, assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, southBank(0.0F), berryBushId, assets::CapabilityType::Harvestable);

		world->update(kTick);

		auto* trace = getTrace(colonist);
		ASSERT_NE(trace, nullptr);
		const auto* hunger = findOption(*trace, isHungerNeedOption);
		ASSERT_NE(hunger, nullptr);
		EXPECT_EQ(hunger->status, OptionStatus::NoSource);
		const auto* gather = gatherFoodOption(*trace);
		ASSERT_NE(gather, nullptr);
		EXPECT_EQ(gather->status, OptionStatus::NoSource);
	}

	// A berry bush standing in a wall the colonist knows is out of reach (a known wall blocks unless the
	// colonist also knows a door through it), so the memo holds "unreachable" for it. These pin the two
	// ways that verdict has to be dropped, or the colonist goes without food it could now reach: its
	// region's mesh swapping in without the wall, and the colonist learning of a door.
	class AIDecisionWallTest : public AIDecisionFirstMeshTest {
	  protected:
		void SetUp() override {
			assets::ConstructionRegistry::Get().clear();
			const std::filesystem::path root =
				std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
			ASSERT_TRUE(assets::ConstructionRegistry::Get().load((root / "assets" / "config" / "construction").string()));
			AIDecisionFirstMeshTest::SetUp();
		}

		void TearDown() override {
			AIDecisionFirstMeshTest::TearDown();
			assets::ConstructionRegistry::Get().clear();
		}

		// A built wall across x = 8 m with the bush at its middle, the colonist knowing every segment of
		// it and the bush, hungry with an empty pack, and the mesh built with the wall in it.
		EntityID makeHungryColonistBesideWall(bool withDoor) {
			const auto commit = construction.commitSegment({8000, -2000}, {8000, 2000}, "Wood", "Standard",
														   construction::kInvalidFoundation);
			EXPECT_TRUE(commit.ok());
			wallSegment = commit.id;
			for (const auto id : commit.createdSegments) {
				construction.setSegmentState(id, construction::FoundationState::Built);
			}
			if (withDoor) {
				door = construction.addOpening(wallSegment, 0.5F, "Door", "Wood");
				EXPECT_NE(door, construction::kInvalidOpening);
				EXPECT_TRUE(construction.setOpeningState(door, construction::FoundationState::Built));
			}

			const auto colonist = createColonist(kStart);
			satisfyAllNeeds(*getNeeds(colonist));
			setNeedValue(colonist, NeedType::Hunger, 40.0F);
			auto* memory = world->getComponent<Memory>(colonist);
			for (const auto id : commit.createdSegments) {
				memory->rememberSegment(static_cast<std::uint64_t>(id));
			}
			addKnownEntity(colonist, kBush, registerBerryBush(assets::AssetRegistry::Get()), assets::CapabilityType::Harvestable);

			nav->finishPendingBuilds();
			EXPECT_TRUE(nav->hasMesh()) << "navmesh never built";
			EXPECT_FALSE(canReachBush(colonist)) << "the bush in the known wall must start out unreachable";
			return colonist;
		}

		[[nodiscard]] bool canReachBush(EntityID colonist) {
			const auto*						  memory = world->getComponent<Memory>(colonist);
			const geometry::nav::BeliefFilter belief{&memory->knownSegments, &memory->knownOpenings};
			return isReachable(*nav, world->getComponent<Position>(colonist)->value, kBush, 0.3F, belief);
		}

		[[nodiscard]] OptionStatus hungerStatus(EntityID colonist) {
			const auto* hunger = findOption(*getTrace(colonist), isHungerNeedOption);
			return hunger != nullptr ? hunger->status : OptionStatus::Satisfied;
		}

		// Runs decisions until Hunger is the chosen task, for at most kTicksAfterMesh ticks.
		[[nodiscard]] bool hungerSelectedWithin(EntityID colonist) {
			for (int i = 0; i < kTicksAfterMesh; ++i) {
				world->update(kTick);
				if (hungerStatus(colonist) == OptionStatus::Selected) {
					return true;
				}
			}
			return false;
		}

		static constexpr glm::vec2 kStart{0.0F, 0.0F};
		static constexpr glm::vec2 kBush{8.0F, 0.0F};
		construction::SegmentId	   wallSegment = construction::kInvalidSegment;
		construction::OpeningId	   door = construction::kInvalidOpening;
	};

	TEST_F(AIDecisionWallTest, MeshSwapWithoutTheWallMakesTheBushReachableAgain) {
		const auto colonist = makeHungryColonistBesideWall(false);
		world->update(kTick);
		ASSERT_EQ(hungerStatus(colonist), OptionStatus::NoSource) << "the walled-in bush can't be the target";

		ASSERT_TRUE(construction.removeSegment(wallSegment));
		nav->finishPendingBuilds();
		ASSERT_TRUE(canReachBush(colonist)) << "the rebuilt mesh never opened the bush up";
		EXPECT_TRUE(hungerSelectedWithin(colonist)) << "the colonist kept the stale verdict and left the bush alone";
	}

	TEST_F(AIDecisionWallTest, LearningOfADoorMakesTheBushReachable) {
		const auto colonist = makeHungryColonistBesideWall(true);
		world->update(kTick);
		ASSERT_EQ(hungerStatus(colonist), OptionStatus::NoSource) << "an unknown door doesn't open a known wall";

		world->getComponent<Memory>(colonist)->rememberOpening(static_cast<std::uint64_t>(door));
		ASSERT_TRUE(canReachBush(colonist)) << "knowing the door opens the bush up";
		EXPECT_TRUE(hungerSelectedWithin(colonist)) << "the colonist kept the stale verdict and left the bush alone";
	}

	// Two known waters, one near another colonist 21 m away, one 30 m away the other way. All loaded
	// ground is one mesh, so both are routable and the nearer one wins. (While each colonist and the
	// view had a region of its own, water in another region read unreachable and was skipped.)
	TEST_F(AIDecisionFirstMeshTest, NeedTakesNearerSourceNearAnotherColonist) {
		const glm::vec2 start{50.0F, 0.0F};
		const glm::vec2 nearOther{71.0F, 0.0F}; // by the other colonist, 21 m away
		const glm::vec2 farther{20.0F, 0.0F};	// 30 m away
		const auto		farColonist = world->createEntity();
		world->addComponent<Position>(farColonist, Position{{100.0F, 0.0F}});
		world->addComponent<Colonist>(farColonist, Colonist{"B"});

		const auto colonist = createColonist(start);
		satisfyAllNeeds(*getNeeds(colonist));
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		setNeedValue(colonist, NeedType::Thirst, 40.0F);
		addKnownEntity(colonist, nearOther, kWaterDefId, assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, farther, kWaterDefId, assets::CapabilityType::Drinkable);

		nav->finishPendingBuilds();
		ASSERT_TRUE(nav->requestPath(start, nearOther, 0.3F).route.has_value()) << "one mesh: a route to the other colonist's side";
		ASSERT_TRUE(nav->requestPath(start, farther, 0.3F).route.has_value());

		world->update(kTick);

		const auto* thirst = findOption(*getTrace(colonist), [](const EvaluatedOption& o) {
			return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Thirst;
		});
		ASSERT_NE(thirst, nullptr);
		EXPECT_EQ(thirst->status, OptionStatus::Selected);
		ASSERT_TRUE(thirst->targetPosition.has_value());
		EXPECT_EQ(*thirst->targetPosition, nearOther) << "the nearer water wins";
	}

	// A loose pile of wood remembered inside a known wall can't be reached. It holds back no cut for the
	// site that wants the wood: the tree beside the colonist is cut ahead of a farther site's work in the
	// same tier, as distance puts it, and the pile, once found unreachable, stays without effect.
	TEST_F(AIDecisionWallTest, AnUnreachablePileHoldsNoCutBack) {
		auto& registry = assets::AssetRegistry::Get();
		const auto registerItem = [&](const char* name, uint8_t hands) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.handsRequired = hands;
			def.category = assets::ItemCategory::RawMaterial;
			def.capabilities.carryable = assets::CarryableCapability{1};
			def.itemProperties = assets::ItemProperties{};
			def.itemProperties->stackSize = 40;
			def.itemProperties->massKg = 1.0F;
			registry.registerTestDefinition(std::move(def));
		};
		const auto registerPlant = [&](const char* name, const char* yield) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.capabilities.harvestable = assets::HarvestableCapability{};
			def.capabilities.harvestable->yieldDefName = yield;
			def.capabilities.harvestable->amountMin = 2;
			def.capabilities.harvestable->amountMax = 2;
			registry.registerTestDefinition(std::move(def));
		};
		registerItem("Wood", 2);
		registerItem("Stick", 1);
		registerPlant("Oak", "Wood");
		registerPlant("StickBush", "Stick");
		const auto site = [&](glm::vec2 at, const char* item) {
			const EntityID blueprint = world->createEntity();
			world->addComponent<Position>(blueprint, Position{at});
			StructureBlueprint bp;
			bp.phase = StructureBlueprint::BuildPhase::AwaitingMaterials;
			bp.required = {{item, 10}};
			world->addComponent<StructureBlueprint>(blueprint, std::move(bp));
			GoalTask umbrella;
			umbrella.type = TaskType::Build;
			umbrella.owner = GoalOwner::ConstructionGoalSystem;
			umbrella.destinationEntity = blueprint;
			umbrella.destinationPosition = at;
			umbrella.acceptedDefNameIds = {registry.getDefNameId(item)};
			umbrella.status = GoalStatus::Blocked;
			const uint64_t umbrellaId = GoalTaskRegistry::Get().createGoal(std::move(umbrella));
			GoalTask cut;
			cut.type = TaskType::Harvest;
			cut.owner = GoalOwner::ConstructionGoalSystem;
			cut.destinationEntity = blueprint;
			cut.destinationPosition = at;
			cut.yieldDefNameId = registry.getDefNameId(item);
			cut.parentGoalId = umbrellaId;
			GoalTaskRegistry::Get().createGoal(std::move(cut));
		};
		site({0.0F, 4.0F}, "Wood");
		site({0.0F, -4.0F}, "Stick");

		const auto commit = construction.commitSegment({8000, -2000}, {8000, 2000}, "Wood", "Standard", construction::kInvalidFoundation);
		ASSERT_TRUE(commit.ok());
		for (const auto id : commit.createdSegments) {
			construction.setSegmentState(id, construction::FoundationState::Built);
		}
		const auto colonist = createColonist(kStart);
		satisfyAllNeeds(*getNeeds(colonist));
		auto* memory = world->getComponent<Memory>(colonist);
		for (const auto id : commit.createdSegments) {
			memory->rememberSegment(static_cast<std::uint64_t>(id));
		}
		addKnownEntity(colonist, {2.0F, 1.0F}, registry.getDefNameId("Oak"), assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, {-6.0F, 0.0F}, registry.getDefNameId("StickBush"), assets::CapabilityType::Harvestable);
		addKnownEntity(colonist, kBush, registry.getDefNameId("Wood"), assets::CapabilityType::Carryable);
		nav->finishPendingBuilds();
		ASSERT_TRUE(nav->hasMesh());
		ASSERT_FALSE(canReachBush(colonist)) << "the pile in the wall must be unreachable";

		for (int decision = 0; decision < 3; ++decision) {
			getTask(colonist)->clear();
			world->update(kTick);
			const auto* chosen = getTrace(colonist)->getSelected();
			ASSERT_NE(chosen, nullptr);
			EXPECT_EQ(chosen->taskType, TaskType::Harvest) << "decision " << decision;
			EXPECT_EQ(chosen->harvestYieldDefNameId, registry.getDefNameId("Wood")) << "decision " << decision;
		}
	}

	namespace {
		// One river north of the origin, running in from far west and ending kFordX metres east: the
		// ford. Its far bank is kAcross, 10 m off; kBehind is 40 m off on the colonist's side.
		constexpr float kFordRiverY = 6.0F;

		template <int kFordX> class FordSampler : public world::IWorldSampler {
		  public:
			[[nodiscard]] world::ChunkSampleResult sampleChunk(world::ChunkCoordinate) const override {
				world::ChunkSampleResult r = world::makeUniformChunkSampleResult(world::BiomeWeights::single(world::Biome::TemperateGrassland), 1.0F);
				r.riverSegments.push_back({.x0 = -2048.0, .y0 = kFordRiverY, .x1 = static_cast<double>(kFordX), .y1 = kFordRiverY,
										   .halfWidth0 = kRiverHalfWidth, .halfWidth1 = kRiverHalfWidth, .s0 = 0.0,
										   .s1 = 2048.0 + static_cast<double>(kFordX)});
				return r;
			}
			[[nodiscard]] float	   sampleElevation(world::WorldPosition) const override { return 1.0F; }
			[[nodiscard]] uint64_t getWorldSeed() const override { return 7U; }
		};

		template <int kFordX> world::ChunkManager* fordChunks() {
			static const std::unique_ptr<world::ChunkManager> chunks = generateOriginChunks(std::make_unique<FordSampler<kFordX>>());
			return chunks.get();
		}
	} // namespace

	// A hungry colonist picks its bush by the walk, inside its own reach square, whatever the camera
	// does: a bush across a river is as near as the way round its end, and a way round outside the
	// square is no way at all.
	template <int kFordX> class AIDecisionFordTest : public AIDecisionFirstMeshTest {
	  protected:
		[[nodiscard]] world::ChunkManager* chunkRegion() const override { return fordChunks<kFordX>(); }

		EntityID hungryColonistKnowing(std::initializer_list<glm::vec2> bushes) {
			const uint32_t berryBushId = registerBerryBush(assets::AssetRegistry::Get());
			const auto	   colonist	   = createColonist(kStart);
			satisfyAllNeeds(*getNeeds(colonist));
			setNeedValue(colonist, NeedType::Hunger, 40.0F);
			for (const glm::vec2 bush : bushes) {
				addKnownEntity(colonist, bush, berryBushId, assets::CapabilityType::Harvestable);
			}
			nav->finishPendingBuilds();
			EXPECT_TRUE(nav->hasMesh());
			return colonist;
		}

		// Where the Hunger option sends the colonist at its next decision, if anywhere.
		std::optional<glm::vec2> hungerTarget(EntityID colonist) {
			getTask(colonist)->clear();
			world->update(kTick);
			const auto* hunger = findOption(*getTrace(colonist), isHungerNeedOption);
			if (hunger == nullptr || hunger->status != OptionStatus::Selected) {
				return std::nullopt;
			}
			return hunger->targetPosition;
		}

		static constexpr glm::vec2 kStart{0.0F, 0.0F};
		static constexpr glm::vec2 kAcross{0.0F, 10.0F};
		static constexpr glm::vec2 kBehind{0.0F, -40.0F};
	};
	using AIDecisionFarFordTest	  = AIDecisionFordTest<600>; // the way round is outside the square
	using AIDecisionNearFordTest  = AIDecisionFordTest<30>;	 // about 70 m round
	using AIDecisionCloseFordTest = AIDecisionFordTest<8>;	 // about 30 m round

	// The way round is 1.2 km: the bush behind is taken, and the same with the camera over the ford
	// (its ground meshed and a player's route round it there), since the camera changes no reach.
	TEST_F(AIDecisionFarFordTest, BushBehindWinsWhateverTheCamera) {
		const auto colonist = hungryColonistKnowing({kAcross, kBehind});
		EXPECT_EQ(hungerTarget(colonist), std::optional<glm::vec2>(kBehind));

		nav->setViewportRect({600.0F, kFordRiverY}, {60.0F, 60.0F});
		nav->finishPendingBuilds();
		ASSERT_TRUE(nav->inSimArea({600.0F, kFordRiverY}));
		ASSERT_TRUE(nav->requestPath(kStart, {620.0F, 20.0F}, 0.3F).route.has_value()) << "a player's route round the ford";
		EXPECT_EQ(hungerTarget(colonist), std::optional<glm::vec2>(kBehind));
	}

	// The way round is longer than the 40 m walk back: the bush behind is taken.
	TEST_F(AIDecisionNearFordTest, BushBehindWinsOverALongerWayRound) {
		const auto colonist = hungryColonistKnowing({kAcross, kBehind});
		EXPECT_EQ(hungerTarget(colonist), std::optional<glm::vec2>(kBehind));
	}

	// The only food is across, with a way round inside the square: it is taken.
	TEST_F(AIDecisionNearFordTest, OnlyFoodAcrossIsTakenTheWayRound) {
		const auto colonist = hungryColonistKnowing({kAcross});
		EXPECT_EQ(hungerTarget(colonist), std::optional<glm::vec2>(kAcross));
	}

	// The way round is shorter than the 40 m walk back: the bush across is taken.
	TEST_F(AIDecisionCloseFordTest, BushAcrossWinsOverALongerWalkBack) {
		const auto colonist = hungryColonistKnowing({kAcross, kBehind});
		EXPECT_EQ(hungerTarget(colonist), std::optional<glm::vec2>(kAcross));
	}

	// The ford is 300 m off. The only food is across: the colonist takes it by the way round, however
	// far. A player's order across routes round the ford the same way, and the colonist arrives.
	using AIDecisionMidFordTest = AIDecisionFordTest<300>;
	TEST_F(AIDecisionMidFordTest, OnlyFoodAcrossIsTakenRoundAFarFord) {
		const auto colonist = hungryColonistKnowing({kAcross});
		EXPECT_EQ(hungerTarget(colonist), std::optional<glm::vec2>(kAcross));
	}

	TEST_F(AIDecisionMidFordTest, PlayerOrderAcrossTheRiverWalksRoundTheFord) {
		const auto colonist = hungryColonistKnowing({kAcross});
		auto& ai = world->getSystem<AIDecisionSystem>();
		ai.enterControl(colonist);
		ASSERT_EQ(ai.issuePlayerMoveOrder(colonist, kAcross), std::optional<glm::vec2>(kAcross));
		const NavPath* route = world->getComponent<NavPath>(colonist);
		ASSERT_TRUE(route != nullptr && route->valid) << "no route round the ford";
		const glm::vec2& at = world->getComponent<Position>(colonist)->value;
		float			 east = 0.0F;
		for (int i = 0; i < 4000 && glm::distance(at, kAcross) > 0.3F; ++i) {
			world->update(kTick);
			east = std::max(east, at.x);
		}
		EXPECT_LT(glm::distance(at, kAcross), 0.3F) << "never arrived";
		EXPECT_GT(east, 300.0F) << "it went round the ford";
	}

	// The only food is 10 m across the river and 1.5 km round its end. The colonist sets off and arrives,
	// or, where that is past what the searches spend, its trace says the search gave up: never "no source".
	using AIDecisionLongFordTest = AIDecisionFordTest<750>;
	TEST_F(AIDecisionLongFordTest, OnlyFoodRoundALongRiverIsReachedOrTheSearchSaysItGaveUp) {
		const auto		 colonist = hungryColonistKnowing({kAcross});
		const glm::vec2& at		  = world->getComponent<Position>(colonist)->value;
		bool			 noSource = false;
		bool			 gaveUp	  = false;
		float			 east	  = 0.0F;
		float			 lastDecision = -1.0F;
		for (int i = 0; i < 20000 && glm::distance(at, kAcross) > 1.5F && !gaveUp; ++i) {
			world->update(kTick);
			east = std::max(east, at.x);
			const auto* trace = getTrace(colonist);
			if (trace->lastEvaluationTime == lastDecision) {
				continue;
			}
			lastDecision		= trace->lastEvaluationTime;
			const auto* hunger = findOption(*trace, isHungerNeedOption);
			ASSERT_NE(hunger, nullptr);
			noSource = noSource || hunger->status == OptionStatus::NoSource;
			gaveUp	 = hunger->status == OptionStatus::SearchGaveUp;
		}
		EXPECT_FALSE(noSource) << "the trace said no source";
		EXPECT_TRUE(gaveUp || glm::distance(at, kAcross) <= 1.5F) << "neither arrived nor gave up; reached x " << east;
		if (!gaveUp) {
			EXPECT_GT(east, 750.0F) << "it went round the river's end";
		}
	}

	// A goal-driven harvest, end to end with the real AI, MovementSystem and ActionSystem: the colonist
	// walks to the stand point beside the tree's collider (read from the placement data), the harvest
	// starts there and completes. A wide tree and a narrow one, on camera and off, from the west and from
	// the north.
	class AIDecisionHarvestStandTest : public AIDecisionFirstMeshTest {
	  protected:
		void SetUp() override {
			AIDecisionFirstMeshTest::SetUp();
			world->registerSystem<TimeSystem>();
			world->registerSystem<ActionSystem>(kTestRngSeed);
			world->getSystem<ActionSystem>().setTakeSourceCallback(
				[this](const std::string& defName, float x, float y, ActionSystem::SourceTake take, float seconds) {
					return takeWorldSource(trees, *world, pendingRemoval, defName, {x, y}, take, seconds);
				}
			);
			world->getSystem<AIDecisionSystem>().setSourceRegrowthCallback([this](uint32_t defNameId, glm::vec2 at, double now) {
				return worldSourceRegrowingUntil(trees, defNameId, at, now);
			});
			assets::RecipeDef recipe;
			recipe.defName = "Recipe_BerryNeed";
			recipe.label   = recipe.defName;
			recipe.inputs.push_back({"Berry", 0, 3});
			assets::RecipeRegistry::Get().registerTestRecipe(recipe);
		}

		// A berry tree whose collider is a square of this half-extent.
		static std::string registerTree(float halfExtent) {
			assets::AssetDefinition tree;
			tree.defName					 = halfExtent > 0.5F ? "Test_WideBerryTree" : "Test_NarrowBerryTree";
			tree.label						 = tree.defName;
			tree.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {halfExtent, halfExtent}});
			tree.capabilities.harvestable	 = assets::HarvestableCapability{};
			tree.capabilities.harvestable->yieldDefName = "Berry";
			tree.capabilities.harvestable->amountMin	= 2;
			tree.capabilities.harvestable->amountMax	= 2;
			tree.capabilities.harvestable->durability	= 10.0F;
			tree.capabilities.harvestable->destructive	= false;
			tree.capabilities.harvestable->regrowthTime = 600.0F;
			std::string name = tree.defName;
			assets::AssetRegistry::Get().registerTestDefinition(std::move(tree));
			return name;
		}

		// Every tree in one chunk's placement.
		void placeTrees(const std::vector<std::pair<std::string, glm::vec2>>& at) {
			assets::AsyncChunkPlacementResult placed;
			placed.coord = world::worldToChunk({at.front().second.x, at.front().second.y});
			for (const auto& [defName, p] : at) {
				EXPECT_EQ(world::worldToChunk({p.x, p.y}), placed.coord);
				placed.spatialIndex.insert({defName, p});
			}
			processed.insert(placed.coord);
			trees.storeChunkResult(std::move(placed));
			nav->setPlacementData(&trees, &processed);
		}

		// One harvest of the tree at `tree` by a colonist starting at `from`, with the view over the tree
		// or far away. Checks where it stood when the harvest started and that it completed.
		// clearSpot: the stand point is clear of every collider; false for a tree in a thicket, whose stand
		// point is inside a neighbour and which the harvest starts within reach of (ActionSystem, 0.4 m).
		void harvestFrom(const std::string& defName, float halfExtent, glm::vec2 tree, glm::vec2 from, bool onCamera, bool clearSpot = true) {
			SCOPED_TRACE(defName + (onCamera ? " on camera" : " off camera") + " from (" + std::to_string(from.x) + ", " +
						 std::to_string(from.y) + ")");
			nav->setViewportRect(onCamera ? tree : glm::vec2{-600.0F, -600.0F}, {30.0F, 30.0F});
			for (int i = 0; i < 3; ++i) {
				world->update(kTick);
			}
			nav->finishPendingBuilds();
			ASSERT_FALSE(nav->isValidPosition(tree)) << "the tree's collider is placed";

			GoalTaskRegistry::Get().clear();
			const EntityID station = world->createEntity();
			world->addComponent<Position>(station, Position{from});
			world->addComponent<WorkQueue>(station).addJob("Recipe_BerryNeed", 1);
			world->addComponent<Inventory>(station, Inventory::createForStorage());
			GoalTask craft;
			craft.type				  = TaskType::Craft;
			craft.owner				  = GoalOwner::CraftingGoalSystem;
			craft.destinationEntity	  = station;
			craft.destinationPosition = from;
			craft.status			  = GoalStatus::Blocked;
			GoalTask cut;
			cut.type				= TaskType::Harvest;
			cut.destinationEntity	= station;
			cut.destinationPosition = from;
			cut.yieldDefNameId		= assets::AssetRegistry::Get().getDefNameId("Berry");
			cut.status				= GoalStatus::Available;
			cut.owner				= GoalOwner::CraftingGoalSystem;
			cut.parentGoalId		= GoalTaskRegistry::Get().createGoal(std::move(craft));
			GoalTaskRegistry::Get().createGoal(std::move(cut));

			const EntityID colonist = createColonist(from);
			world->addComponent<Action>(colonist, Action{});
			satisfyAllNeeds(*getNeeds(colonist));
			addKnownEntity(colonist, tree, assets::AssetRegistry::Get().getDefNameId(defName), assets::CapabilityType::Harvestable);

			std::optional<glm::vec2> startedAt;
			bool					 harvested = false;
			const uint32_t			 treeId	   = assets::AssetRegistry::Get().getDefNameId(defName);
			const auto&				 time	   = world->getSystem<TimeSystem>();
			for (int i = 0; i < 600 && !harvested; ++i) {
				world->update(kTick);
				const Action& action = *world->getComponent<Action>(colonist);
				if (!startedAt.has_value() && action.isActive() && action.type == ActionType::Harvest) {
					startedAt = world->getComponent<Position>(colonist)->value;
				}
				harvested = worldSourceRegrowingUntil(trees, treeId, tree, time.simulationSeconds()).has_value();
			}
			ASSERT_TRUE(startedAt.has_value()) << "the harvest never started";
			EXPECT_TRUE(harvested) << "the harvest never completed";
			if (clearSpot) {
				EXPECT_TRUE(nav->isValidPosition(*startedAt)) << "it stood inside a collider";
			}
			const glm::vec2 off = glm::abs(*startedAt - tree);
			EXPECT_GT(std::max(off.x, off.y), halfExtent) << "it stood inside the collider";
			EXPECT_LT(glm::distance(*startedAt, tree), halfExtent * 1.5F + 1.0F) << "it stood away from the tree";
			EXPECT_GT(glm::dot(*startedAt - tree, from - tree), 0.0F) << "it went round to the far side";
			world->destroyEntity(colonist);
			world->destroyEntity(station);
		}

		assets::PlacementExecutor				   trees{assets::AssetRegistry::Get()};
		std::unordered_set<world::ChunkCoordinate> processed;
		std::vector<EntityID>					   pendingRemoval;
	};

	TEST_F(AIDecisionHarvestStandTest, WideTreeIsHarvestedFromItsStandPoint) {
		const std::string tree = registerTree(1.0F);
		const glm::vec2	  a{20.0F, 20.0F};
		const glm::vec2	  b{60.0F, 20.0F};
		const glm::vec2	  c{20.0F, 60.0F};
		const glm::vec2	  d{60.0F, 60.0F};
		placeTrees({{tree, a}, {tree, b}, {tree, c}, {tree, d}});
		harvestFrom(tree, 1.0F, a, a + glm::vec2{-12.0F, 0.0F}, true);
		harvestFrom(tree, 1.0F, b, b + glm::vec2{0.0F, 12.0F}, true);
		harvestFrom(tree, 1.0F, c, c + glm::vec2{-12.0F, 0.0F}, false);
		harvestFrom(tree, 1.0F, d, d + glm::vec2{0.0F, 12.0F}, false);
	}

	TEST_F(AIDecisionHarvestStandTest, NarrowTreeIsHarvestedFromItsStandPoint) {
		const std::string tree = registerTree(0.2F);
		const glm::vec2	  a{20.0F, 20.0F};
		const glm::vec2	  b{60.0F, 20.0F};
		const glm::vec2	  c{20.0F, 60.0F};
		const glm::vec2	  d{60.0F, 60.0F};
		placeTrees({{tree, a}, {tree, b}, {tree, c}, {tree, d}});
		harvestFrom(tree, 0.2F, a, a + glm::vec2{-12.0F, 0.0F}, true);
		harvestFrom(tree, 0.2F, b, b + glm::vec2{0.0F, 12.0F}, true);
		harvestFrom(tree, 0.2F, c, c + glm::vec2{-12.0F, 0.0F}, false);
		harvestFrom(tree, 0.2F, d, d + glm::vec2{0.0F, 12.0F}, false);
	}

	// A tree ringed by other trunks, so every spot just outside its collider is inside a neighbour's: it
	// is still harvested, from a spot inside a neighbour, on camera and off.
	TEST_F(AIDecisionHarvestStandTest, TreeInAThicketIsStillHarvested) {
		const std::string tree = registerTree(0.2F);
		assets::AssetDefinition neighbour;
		neighbour.defName					  = "Test_ThicketTrunk";
		neighbour.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {0.2F, 0.2F}});
		assets::AssetRegistry::Get().registerTestDefinition(std::move(neighbour));
		const glm::vec2								 a{20.0F, 20.0F};
		const glm::vec2								 b{60.0F, 20.0F};
		std::vector<std::pair<std::string, glm::vec2>> placed{{tree, a}, {tree, b}};
		for (const glm::vec2 centre : {a, b}) {
			for (int k = 0; k < 12; ++k) {
				const float angle = 6.2831853F * static_cast<float>(k) / 12.0F;
				placed.emplace_back("Test_ThicketTrunk", centre + 0.7F * glm::vec2{std::cos(angle), std::sin(angle)});
			}
		}
		placeTrees(placed);
		harvestFrom(tree, 0.2F, a, a + glm::vec2{-12.0F, 0.0F}, true, false);
		harvestFrom(tree, 0.2F, b, b + glm::vec2{-12.0F, 0.0F}, false, false);
	}

	// The harvest it is on keeps its stand point: carried round to the far side of the tree mid-walk, the
	// colonist would work the stand point out again on that side, which would read as another task. It
	// keeps the task (same instance) and the target, and the harvest completes.
	TEST_F(AIDecisionHarvestStandTest, HarvestInProgressKeepsItsStandPoint) {
		const std::string tree = registerTree(1.0F);
		const glm::vec2	  at{20.0F, 20.0F};
		placeTrees({{tree, at}});
		nav->setViewportRect(at, {30.0F, 30.0F});
		nav->finishPendingBuilds();

		GoalTaskRegistry::Get().clear();
		const EntityID station = world->createEntity();
		world->addComponent<Position>(station, Position{at + glm::vec2{-12.0F, 0.0F}});
		world->addComponent<WorkQueue>(station).addJob("Recipe_BerryNeed", 1);
		world->addComponent<Inventory>(station, Inventory::createForStorage());
		GoalTask craft;
		craft.type				  = TaskType::Craft;
		craft.owner				  = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity	  = station;
		craft.destinationPosition = at;
		craft.status			  = GoalStatus::Blocked;
		GoalTask cut;
		cut.type				= TaskType::Harvest;
		cut.destinationEntity	= station;
		cut.destinationPosition = at;
		cut.yieldDefNameId		= assets::AssetRegistry::Get().getDefNameId("Berry");
		cut.status				= GoalStatus::Available;
		cut.owner				= GoalOwner::CraftingGoalSystem;
		cut.parentGoalId		= GoalTaskRegistry::Get().createGoal(std::move(craft));
		GoalTaskRegistry::Get().createGoal(std::move(cut));

		const EntityID colonist = createColonist(at + glm::vec2{-12.0F, 0.0F});
		world->addComponent<Action>(colonist, Action{});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, at, assets::AssetRegistry::Get().getDefNameId(tree), assets::CapabilityType::Harvestable);
		for (int i = 0; i < 20 && getTask(colonist)->type != TaskType::Harvest; ++i) {
			world->update(kTick);
		}
		ASSERT_EQ(getTask(colonist)->type, TaskType::Harvest);
		const uint64_t	instance = getTask(colonist)->instanceId;
		const glm::vec2 standAt	 = getTask(colonist)->targetPosition;
		ASSERT_LT(standAt.x, at.x) << "the stand point is on the west side";

		world->getComponent<Position>(colonist)->value = at + glm::vec2{3.0F, 1.5F}; // round the far side
		for (int i = 0; i < 10; ++i) {
			getTask(colonist)->timeSinceEvaluation = 1.0F;
			world->update(kTick);
			ASSERT_EQ(getTask(colonist)->instanceId, instance) << "re-selected as a new task at tick " << i;
			ASSERT_EQ(getTask(colonist)->targetPosition, standAt) << "the stand point moved at tick " << i;
		}
		const auto& time = world->getSystem<TimeSystem>();
		bool		harvested = false;
		for (int i = 0; i < 600 && !harvested; ++i) {
			world->update(kTick);
			harvested = worldSourceRegrowingUntil(trees, assets::AssetRegistry::Get().getDefNameId(tree), at, time.simulationSeconds()).has_value();
		}
		EXPECT_TRUE(harvested);
	}

	// A footprint cleared in forest at the game's density (a tree every ~3.5 m, colliders by age class):
	// each tree in it is harvested by one decision, and the stranded-colonist recovery never fires (a
	// colonist standing at a stand point, or in a neighbour's collider, is on ground a mover may stand
	// on).
	TEST_F(AIDecisionHarvestStandTest, FootprintClearInDenseForestTakesOneDecisionPerTree) {
		assets::AssetDefinition def;
		def.defName														= "Test_DenseOak";
		def.label														= def.defName;
		def.ageClasses													= {{.name = "sapling", .weight = 0.15F}, {.name = "mature", .weight = 0.6F}, {.name = "veteran", .weight = 0.25F}};
		const float halfExtents[]										= {0.125F, 0.225F, 0.36F};
		for (std::uint8_t a = 0; a < 3; ++a) {
			def.setCollision(a, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {halfExtents[a], halfExtents[a]}});
		}
		def.capabilities.harvestable							   = assets::HarvestableCapability{};
		def.capabilities.harvestable->yieldDefName				   = "Berry";
		def.capabilities.harvestable->amountMin					   = 1;
		def.capabilities.harvestable->amountMax					   = 1;
		def.capabilities.harvestable->durability				   = 5.0F;
		assets::AssetRegistry::Get().registerTestDefinition(std::move(def));
		const uint32_t oakId = assets::AssetRegistry::Get().getDefNameId("Test_DenseOak");

		std::vector<std::pair<std::string, glm::vec2>> placed;
		std::vector<glm::vec2>						   inFootprint;
		const glm::vec2								   centre{40.0F, 40.0F};
		for (float x = centre.x - 14.0F; x <= centre.x + 14.0F; x += 3.54F) {
			for (float y = centre.y - 14.0F; y <= centre.y + 14.0F; y += 3.54F) {
				const glm::vec2 p{x, y};
				placed.emplace_back("Test_DenseOak", p);
				if (std::abs(p.x - centre.x) < 4.5F && std::abs(p.y - centre.y) < 4.5F) {
					inFootprint.push_back(p);
				}
			}
		}
		{
			assets::AsyncChunkPlacementResult result;
			result.coord = world::worldToChunk({centre.x, centre.y});
			for (std::size_t k = 0; k < placed.size(); ++k) {
				assets::PlacedEntity e;
				e.defName		  = placed[k].first;
				e.position		  = placed[k].second;
				e.traits.ageClass = static_cast<std::uint8_t>(k % 7 == 0 ? 0 : k % 4 == 0 ? 2 : 1);
				result.spatialIndex.insert(e);
			}
			processed.insert(result.coord);
			trees.storeChunkResult(std::move(result));
			nav->setPlacementData(&trees, &processed);
		}
		ASSERT_GE(inFootprint.size(), 4U);
		nav->setViewportRect(centre, {30.0F, 30.0F});
		for (int i = 0; i < 4; ++i) {
			world->update(kTick);
		}
		nav->finishPendingBuilds();

		GoalTaskRegistry::Get().clear();
		GoalTask clear;
		clear.type				  = TaskType::Harvest;
		clear.owner				  = GoalOwner::ConstructionGoalSystem;
		clear.destinationPosition = centre;
		clear.clearFootprint	  = FootprintBounds{centre - glm::vec2{4.5F, 4.5F}, centre + glm::vec2{4.5F, 4.5F}};
		clear.status			  = GoalStatus::Available;
		GoalTaskRegistry::Get().createGoal(std::move(clear));

		const EntityID colonist = createColonist(centre + glm::vec2{-16.5F, 0.0F});
		world->addComponent<Action>(colonist, Action{});
		satisfyAllNeeds(*getNeeds(colonist));
		for (const glm::vec2 p : inFootprint) {
			addKnownEntity(colonist, p, oakId, assets::CapabilityType::Harvestable);
		}

		std::set<uint64_t> harvestTasks;
		int				   harvestsStarted = 0;
		bool			   wasHarvesting   = false;
		const auto		   left			   = [&] {
			  int standing = 0;
			  for (const glm::vec2 p : inFootprint) {
				  standing += trees.getChunkIndex(world::worldToChunk({p.x, p.y}))->queryRect(p.x - 0.01F, p.y - 0.01F, p.x + 0.01F, p.y + 0.01F).empty() ? 0 : 1;
			  }
			  return standing;
		};
		for (int i = 0; i < 6000 && left() > 0; ++i) {
			world->update(kTick);
			for (const EntityID dead : pendingRemoval) {
				world->destroyEntity(dead);
			}
			pendingRemoval.clear();
			const glm::vec2 here = world->getComponent<Position>(colonist)->value;
			ASSERT_FALSE(nav->inSimArea(here) && !nav->isOnMesh(here)) << "the stranded recovery would fire at (" << here.x << ", " << here.y
																	   << "), tick " << i;
			const Task& task = *getTask(colonist);
			if (task.type == TaskType::Harvest && task.instanceId != 0) {
				harvestTasks.insert(task.instanceId);
			}
			const Action& action	 = *world->getComponent<Action>(colonist);
			const bool	  harvesting = action.isActive() && action.type == ActionType::Harvest;
			harvestsStarted += harvesting && !wasHarvesting ? 1 : 0;
			wasHarvesting = harvesting;
		}
		EXPECT_EQ(left(), 0) << "the footprint was never cleared";
		EXPECT_EQ(harvestsStarted, static_cast<int>(inFootprint.size()));
		EXPECT_EQ(harvestTasks.size(), inFootprint.size()) << "a harvest was decided more than once";
	}

	// A cut carried to its station 200 m away: the berry tree 100 m west, the station whose job needs
	// its yield 100 m east. The harvest's deposit leg routes and the berries arrive.
	TEST_F(AIDecisionHarvestStandTest, CutCarriedToAStationTwoHundredMetresAwayIsDelivered) {
		const std::string tree = registerTree(0.2F);
		const glm::vec2	  treeAt{-100.0F, 20.0F};
		placeTrees({{tree, treeAt}});
		nav->finishPendingBuilds();

		GoalTaskRegistry::Get().clear();
		const glm::vec2 stationAt{100.0F, 20.0F};
		const EntityID	station = world->createEntity();
		world->addComponent<Position>(station, Position{stationAt});
		world->addComponent<WorkQueue>(station).addJob("Recipe_BerryNeed", 1);
		world->addComponent<Inventory>(station, Inventory::createForStorage());
		GoalTask craft;
		craft.type				  = TaskType::Craft;
		craft.owner				  = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity	  = station;
		craft.destinationPosition = stationAt;
		craft.status			  = GoalStatus::Blocked;
		GoalTask cut;
		cut.type				= TaskType::Harvest;
		cut.destinationEntity	= station;
		cut.destinationPosition = stationAt;
		cut.yieldDefNameId		= assets::AssetRegistry::Get().getDefNameId("Berry");
		cut.status				= GoalStatus::Available;
		cut.owner				= GoalOwner::CraftingGoalSystem;
		cut.parentGoalId		= GoalTaskRegistry::Get().createGoal(std::move(craft));
		GoalTaskRegistry::Get().createGoal(std::move(cut));

		const EntityID colonist = createColonist({0.0F, 20.0F});
		world->addComponent<Action>(colonist, Action{});
		satisfyAllNeeds(*getNeeds(colonist));
		addKnownEntity(colonist, treeAt, assets::AssetRegistry::Get().getDefNameId(tree), assets::CapabilityType::Harvestable);
		const auto& store = *world->getComponent<Inventory>(station);
		for (int i = 0; i < 4000 && store.getQuantity("Berry") == 0; ++i) {
			world->update(kTick);
		}
		EXPECT_GT(store.getQuantity("Berry"), 0U) << "the cut never reached the station";
	}

	// A need whose way there detours 60 m from it: water 20 m north behind a long wall the colonist
	// knows, its only end 60 m east. The task survives the detour (one decision) and he drinks.
	TEST_F(AIDecisionWallTest, NeedSurvivesADetourFarFromItsTarget) {
		world->registerSystem<TimeSystem>();
		world->registerSystem<ActionSystem>(kTestRngSeed);
		assets::AssetDefinition water;
		water.defName = "Water";
		water.label	  = "Water";
		assets::AssetRegistry::Get().registerTestDefinition(water);
		const auto commit = construction.commitSegment({-200000, 10000}, {60000, 10000}, "Wood", "Standard", construction::kInvalidFoundation);
		ASSERT_TRUE(commit.ok());
		const glm::vec2 waterAt{0.0F, 20.0F};
		const auto		colonist = createColonist({0.0F, 0.0F});
		world->addComponent<Action>(colonist, Action{});
		satisfyAllNeeds(*getNeeds(colonist));
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		setNeedValue(colonist, NeedType::Thirst, 20.0F);
		for (const auto id : commit.createdSegments) {
			construction.setSegmentState(id, construction::FoundationState::Built);
			world->getComponent<Memory>(colonist)->rememberSegment(static_cast<std::uint64_t>(id));
		}
		addKnownEntity(colonist, waterAt, assets::AssetRegistry::Get().getDefNameId("Water"), assets::CapabilityType::Drinkable);
		nav->finishPendingBuilds();

		std::set<uint64_t> drinkTasks;
		float			   farthest = 0.0F;
		for (int i = 0; i < 3000 && getNeeds(colonist)->get(NeedType::Thirst).value < 50.0F; ++i) {
			world->update(kTick);
			const Task& task = *getTask(colonist);
			if (task.type == TaskType::FulfillNeed && task.needToFulfill == NeedType::Thirst) {
				drinkTasks.insert(task.instanceId);
			}
			farthest = std::max(farthest, glm::distance(world->getComponent<Position>(colonist)->value, waterAt));
		}
		EXPECT_GT(getNeeds(colonist)->get(NeedType::Thirst).value, 50.0F) << "never drank";
		EXPECT_EQ(drinkTasks.size(), 1U) << "the need was decided again on the detour";
		EXPECT_GT(farthest, 50.0F) << "it never went round";
	}

	namespace {
		// A river ending 600 m east at y = 262, so its end (the ford) is in chunk (1, 0) and everything
		// else of interest in chunk (0, 0).
		class RiverEndSampler : public world::IWorldSampler {
		  public:
			[[nodiscard]] world::ChunkSampleResult sampleChunk(world::ChunkCoordinate) const override {
				world::ChunkSampleResult r = world::makeUniformChunkSampleResult(world::BiomeWeights::single(world::Biome::TemperateGrassland), 1.0F);
				r.riverSegments.push_back({.x0 = -2048.0, .y0 = 262.0, .x1 = 600.0, .y1 = 262.0, .halfWidth0 = kRiverHalfWidth,
										   .halfWidth1 = kRiverHalfWidth, .s0 = 0.0, .s1 = 2648.0});
				return r;
			}
			[[nodiscard]] float	   sampleElevation(world::WorldPosition) const override { return 1.0F; }
			[[nodiscard]] uint64_t getWorldSeed() const override { return 7U; }
		};
	} // namespace

	// The only food is a bush across the river whose walk round goes through chunk (1, 0). While that
	// chunk is loaded but its ground not yet meshed, the walk is "not now": the bush is passed over for
	// that decision and nothing is remembered, and it's taken as soon as the ground lands. With the
	// camera having meshed the chunk first it's taken at once: the same choice either way.
	class AIDecisionArrivingGroundTest : public AIDecisionFirstMeshTest {
	  protected:
		[[nodiscard]] world::ChunkManager* chunkRegion() const override { return chunks.get(); }

		void stream(glm::vec2 camera) {
			std::vector<world::WorldPosition> anchors;
			nav->appendAnchors(anchors);
			chunks->update({camera.x, camera.y}, anchors);
			chunks->finishPendingGeneration();
		}

		EntityID hungryColonistAcrossFromTheBush() {
			const uint32_t bush		= registerBerryBush(assets::AssetRegistry::Get());
			const auto	   colonist = createColonist(kAt);
			satisfyAllNeeds(*getNeeds(colonist));
			setNeedValue(colonist, NeedType::Hunger, 40.0F);
			addKnownEntity(colonist, kBush, bush, assets::CapabilityType::Harvestable);
			return colonist;
		}

		std::optional<glm::vec2> hungerTarget(EntityID colonist) {
			getTask(colonist)->clear();
			world->update(kTick);
			const auto* hunger = findOption(*getTrace(colonist), isHungerNeedOption);
			return hunger != nullptr && hunger->status == OptionStatus::Selected ? hunger->targetPosition : std::nullopt;
		}

		std::unique_ptr<world::ChunkManager> chunks = [] {
			auto manager = std::make_unique<world::ChunkManager>(std::make_unique<RiverEndSampler>());
			manager->setLoadRadius(0);
			manager->setUnloadRadius(0);
			manager->update({256.0F, 256.0F});
			manager->finishPendingGeneration();
			return manager;
		}();
		static constexpr glm::vec2 kAt{500.0F, 250.0F};
		static constexpr glm::vec2 kBush{500.0F, 272.0F};
	};

	TEST_F(AIDecisionArrivingGroundTest, BushBehindArrivingGroundIsTakenOnceItLands) {
		const auto colonist = hungryColonistAcrossFromTheBush();
		nav->finishPendingBuilds();
		ASSERT_FALSE(nav->inSimArea({700.0F, 250.0F})) << "chunk (1, 0) isn't loaded yet";
		stream({256.0F, 256.0F}); // the colonist's anchor loads chunk (1, 0)
		ASSERT_NE(chunks->getChunk({1, 0}), nullptr);
		ASSERT_FALSE(nav->reachFrom(kAt, 0.3F).settled);

		EXPECT_EQ(hungerTarget(colonist), std::nullopt) << "the walk round waits on chunk (1, 0)";
		std::optional<glm::vec2> target;
		for (int i = 0; i < 5 && !target.has_value(); ++i) {
			target = hungerTarget(colonist);
		}
		EXPECT_EQ(target, std::optional<glm::vec2>(kBush)) << "not taken once the ground landed: the miss was remembered";
	}

	// A route that fails while chunk (1, 0) is still arriving is stamped unsettled, so once the ground
	// lands the next decision neither records it nor keeps the failed task: the bush is taken.
	TEST_F(AIDecisionArrivingGroundTest, RouteThatFailedWhileGroundArrivedIsNotHeld) {
		const auto colonist = hungryColonistAcrossFromTheBush();
		nav->finishPendingBuilds();
		stream({256.0F, 256.0F});
		ASSERT_FALSE(nav->reachFrom(kAt, 0.3F).settled);
		ASSERT_TRUE(world->getSystem<AIDecisionSystem>().issuePlayerMoveOrder(colonist, kBush).has_value());
		ASSERT_EQ(getTask(colonist)->navState, NavState::CantFindWayTo) << "the walk round needs chunk (1, 0)";

		nav->finishPendingBuilds();
		ASSERT_TRUE(nav->reachFrom(kAt, 0.3F).settled);
		std::optional<glm::vec2> target;
		for (int i = 0; i < kTicksAfterMesh && !target.has_value(); ++i) {
			world->update(kTick);
			const auto* hunger = findOption(*getTrace(colonist), isHungerNeedOption);
			if (hunger != nullptr && hunger->status == OptionStatus::Selected) {
				target = hunger->targetPosition;
			}
		}
		EXPECT_FALSE(world->getComponent<SourceReachMemo>(colonist)->knownUnreachable(hashWorldEntity(kBush, 0)))
			<< "the failure was recorded";
		EXPECT_EQ(target, std::optional<glm::vec2>(kBush));
	}

	TEST_F(AIDecisionArrivingGroundTest, BushBehindGroundTheCameraMeshedIsTakenAtOnce) {
		const auto colonist = hungryColonistAcrossFromTheBush();
		stream({768.0F, 256.0F}); // the camera over chunk (1, 0) first
		stream({256.0F, 256.0F});
		nav->finishPendingBuilds();
		ASSERT_TRUE(nav->reachFrom(kAt, 0.3F).settled);
		EXPECT_EQ(hungerTarget(colonist), std::optional<glm::vec2>(kBush));
	}

	// The old regions' hold, flipped: the nearest water the colonist knows is in a chunk nothing loads
	// (3 km off), the other on meshed ground 35 m off. Reach never says "maybe" for ground no pending
	// work will mesh, so the far water is never chosen; he goes to the near water and drinks.
	TEST_F(AIDecisionFirstMeshTest, WaterInGroundNothingLoadsIsNeverChosen) {
		world->registerSystem<TimeSystem>();
		world->registerSystem<ActionSystem>(kTestRngSeed);
		assets::AssetDefinition water;
		water.defName = "Water";
		water.label	  = "Water";
		assets::AssetRegistry::Get().registerTestDefinition(water);
		const uint32_t	waterId = assets::AssetRegistry::Get().getDefNameId("Water");
		const glm::vec2 start{40.0F, 0.0F};
		const glm::vec2 far{3000.0F, 0.0F};
		const glm::vec2 near{5.0F, 0.0F};
		const auto		colonist = createColonist(start);
		world->addComponent<Action>(colonist, Action{});
		satisfyAllNeeds(*getNeeds(colonist));
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		setNeedValue(colonist, NeedType::Thirst, 20.0F);
		addKnownEntity(colonist, far, waterId, assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, near, waterId, assets::CapabilityType::Drinkable);
		nav->finishPendingBuilds();
		ASSERT_FALSE(nav->inSimArea(far));
		EXPECT_FALSE(nav->walkDistance(nav->reachFrom(start, 0.3F), far).has_value()) << "reach said maybe";

		bool tookFar = false;
		for (int i = 0; i < 600 && getNeeds(colonist)->get(NeedType::Thirst).value < 50.0F; ++i) {
			world->update(kTick);
			tookFar = tookFar || getTask(colonist)->targetPosition == far;
			EXPECT_NE(getTask(colonist)->navState, NavState::AwaitingMesh) << "held for ground at tick " << i;
		}
		EXPECT_FALSE(tookFar);
		EXPECT_GT(getNeeds(colonist)->get(NeedType::Thirst).value, 50.0F) << "never drank";
	}

	// Water in a chunk that is loaded but not yet meshed is "maybe": the colonist goes for it, waits for
	// its ground (the waiting goal is an anchor, so it comes), then routes, walks and drinks. A wait
	// something always ends.
	TEST_F(AIDecisionArrivingGroundTest, WaterOnGroundStillArrivingIsDrunkOnceItLands) {
		world->registerSystem<TimeSystem>();
		world->registerSystem<ActionSystem>(kTestRngSeed);
		assets::AssetDefinition water;
		water.defName = "Water";
		water.label	  = "Water";
		assets::AssetRegistry::Get().registerTestDefinition(water);
		const glm::vec2 waterAt{560.0F, 250.0F};
		const auto		colonist = createColonist(kAt);
		world->addComponent<Action>(colonist, Action{});
		satisfyAllNeeds(*getNeeds(colonist));
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		setNeedValue(colonist, NeedType::Thirst, 20.0F);
		addKnownEntity(colonist, waterAt, assets::AssetRegistry::Get().getDefNameId("Water"), assets::CapabilityType::Drinkable);
		nav->finishPendingBuilds();
		stream({256.0F, 256.0F});
		ASSERT_FALSE(nav->inSimArea(waterAt)) << "its ground is still to come";

		for (int i = 0; i < 1200 && getNeeds(colonist)->get(NeedType::Thirst).value < 50.0F; ++i) {
			stream({256.0F, 256.0F});
			world->update(kTick);
		}
		EXPECT_GT(getNeeds(colonist)->get(NeedType::Thirst).value, 50.0F) << "never drank: navState "
																		   << static_cast<int>(getTask(colonist)->navState);
	}

	// Two water points a metre apart, ground landing elsewhere every few ticks (the camera wandering,
	// which moves the walks' key each time): the colonist picks one and keeps it. Same position, same
	// mesh near him, same pick.
	TEST_F(AIDecisionArrivingGroundTest, TargetHoldsWhileGroundLandsElsewhere) {
		world->registerSystem<TimeSystem>();
		world->registerSystem<ActionSystem>(kTestRngSeed);
		assets::AssetDefinition water;
		water.defName = "Water";
		water.label	  = "Water";
		assets::AssetRegistry::Get().registerTestDefinition(water);
		const uint32_t waterId	= assets::AssetRegistry::Get().getDefNameId("Water");
		const auto	   colonist = createColonist(kAt);
		world->addComponent<Action>(colonist, Action{});
		satisfyAllNeeds(*getNeeds(colonist));
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		setNeedValue(colonist, NeedType::Thirst, 20.0F);
		addKnownEntity(colonist, kAt + glm::vec2{-30.0F, 0.0F}, waterId, assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, kAt + glm::vec2{-30.0F, 1.0F}, waterId, assets::CapabilityType::Drinkable);
		stream({256.0F, 256.0F});
		nav->finishPendingBuilds();

		int						 changes = 0;
		std::optional<glm::vec2> last;
		for (int i = 0; i < 600 && getNeeds(colonist)->get(NeedType::Thirst).value < 50.0F; ++i) {
			if (i % 5 == 0) {
				stream({256.0F + 512.0F * static_cast<float>(3 + i / 5), 256.0F}); // a far chunk's ground lands
			}
			world->update(kTick);
			const Task& task = *getTask(colonist);
			if (task.type == TaskType::FulfillNeed && task.needToFulfill == NeedType::Thirst &&
				(!last.has_value() || glm::distance(*last, task.targetPosition) > 0.01F)) {
				++changes;
				last = task.targetPosition;
			}
		}
		EXPECT_EQ(changes, 1) << "it flipped between the two water points";
		EXPECT_GT(getNeeds(colonist)->get(NeedType::Thirst).value, 50.0F);
	}

	// Hauls with the real AI, MovementSystem and ActionSystem: stone from piles to stations. A haul is
	// chosen only when its deposit leg can route, so a deposit 200 m from the pickup is delivered.
	class AIDecisionHaulLegTest : public AIDecisionFirstMeshTest {
	  protected:
		void SetUp() override {
			AIDecisionFirstMeshTest::SetUp();
			world->registerSystem<TimeSystem>();
			world->registerSystem<ActionSystem>(kTestRngSeed);
			assets::AssetDefinition stone;
			stone.defName					= "Stone";
			stone.label						= "Stone";
			stone.handsRequired				= 1;
			stone.category					= assets::ItemCategory::RawMaterial;
			stone.capabilities.carryable	= assets::CarryableCapability{5};
			stone.itemProperties			= assets::ItemProperties{};
			stone.itemProperties->stackSize = 40;
			stone.itemProperties->massKg	= 0.5F;
			assets::AssetRegistry::Get().registerTestDefinition(std::move(stone));
			stoneId = assets::AssetRegistry::Get().getDefNameId("Stone");

			colonist = createColonist({0.0F, 0.0F});
			world->addComponent<Action>(colonist, Action{});
			satisfyAllNeeds(*getNeeds(colonist));
		}

		// A pile of `count` stone the colonist knows.
		void pile(glm::vec2 at, uint32_t count) {
			const EntityID e = world->createEntity();
			world->addComponent<Position>(e, Position{at});
			makeLoosePile(*world, e, stoneId, at, count);
			addKnownEntity(colonist, at, stoneId, assets::CapabilityType::Carryable);
		}

		// A station whose job needs `count` stone, with its craft root and haul child.
		EntityID station(glm::vec2 at, uint32_t count) {
			const EntityID	  e = world->createEntity();
			const std::string recipeName = "Recipe_Stone" + std::to_string(++stations);
			world->addComponent<Position>(e, Position{at});
			assets::RecipeDef recipe;
			recipe.defName = recipeName;
			recipe.label   = recipeName;
			recipe.inputs.push_back({"Stone", 0, count});
			assets::RecipeRegistry::Get().registerTestRecipe(recipe);
			world->addComponent<WorkQueue>(e).addJob(recipeName, 1);
			world->addComponent<Inventory>(e, Inventory::createForStorage());
			GoalTask craft;
			craft.type				  = TaskType::Craft;
			craft.owner				  = GoalOwner::CraftingGoalSystem;
			craft.destinationEntity	  = e;
			craft.destinationPosition = at;
			craft.status			  = GoalStatus::Blocked;
			GoalTask haul;
			haul.type				 = TaskType::Haul;
			haul.owner				 = GoalOwner::CraftingGoalSystem;
			haul.destinationEntity	 = e;
			haul.destinationPosition = at;
			haul.acceptedDefNameIds	 = {stoneId};
			haul.parentGoalId		 = GoalTaskRegistry::Get().createGoal(std::move(craft));
			GoalTaskRegistry::Get().createGoal(std::move(haul));
			return e;
		}

		[[nodiscard]] uint32_t at(EntityID e) { return world->getComponent<Inventory>(e)->getQuantity("Stone"); }
		[[nodiscard]] bool	   delivering(EntityID to) {
			  const Task& task = *getTask(colonist);
			  return task.type == TaskType::Haul && task.haulDestinationId == static_cast<uint64_t>(to);
		}

		// Runs until `done`, for at most `ticks` updates.
		template <typename Done> void runUntil(Done&& done, int ticks = 4000) {
			nav->finishPendingBuilds();
			for (int i = 0; i < ticks && !done(); ++i) {
				world->update(kTick);
			}
		}

		uint32_t stoneId  = 0;
		int		 stations = 0;
		EntityID colonist = kInvalidEntity;
	};

	// Pile 100 m west, station 100 m east: both in the square round the colonist when it chose, the
	// deposit 200 m from the pickup. The stone arrives and the delivery is done.
	TEST_F(AIDecisionHaulLegTest, DepositTwoHundredMetresFromThePickupIsDelivered) {
		pile({-100.0F, 0.0F}, 3);
		const EntityID to = station({100.0F, 0.0F}, 3);
		runUntil([&] { return at(to) == 3; });
		EXPECT_EQ(at(to), 3U) << "the delivery was let go";
		world->update(kTick);
		EXPECT_FALSE(delivering(to));
	}

	// Stone from two piles in one square to one station: it all arrives.
	TEST_F(AIDecisionHaulLegTest, HaulFromTwoPilesInOneSquareIsDelivered) {
		pile({-60.0F, 0.0F}, 2);
		pile({-80.0F, 10.0F}, 1);
		const EntityID to = station({60.0F, 0.0F}, 3);
		runUntil([&] { return at(to) == 3; });
		EXPECT_EQ(at(to), 3U);
	}

	// A route that failed is held as a verdict while the colonist stays in the square it failed in, so a
	// colonist wandering about one spot never picks the failed target again: here, 40 decisions from
	// points up to 50 m round the spot. Leaving the square drops the verdict, so it may pick it once more
	// (at most once per anchor: after the next failure the verdict holds again).
	TEST_F(AIDecisionFirstMeshTest, WanderingColonistDoesNotRepickAFailedRoute) {
		constexpr glm::vec2 kSpot{0.0F, 0.0F};
		constexpr glm::vec2 kNear{5.0F, 0.0F};
		constexpr glm::vec2 kFar{-20.0F, 0.0F};
		const auto			colonist = createColonist(kSpot);
		satisfyAllNeeds(*getNeeds(colonist));
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		setNeedValue(colonist, NeedType::Thirst, 30.0F);
		addKnownEntity(colonist, kNear, kWaterDefId, assets::CapabilityType::Drinkable);
		addKnownEntity(colonist, kFar, kWaterDefId, assets::CapabilityType::Drinkable);
		nav->finishPendingBuilds();

		const auto thirstTarget = [&]() -> std::optional<glm::vec2> {
			const auto* thirst = findOption(*getTrace(colonist), [](const EvaluatedOption& o) {
				return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Thirst;
			});
			return thirst != nullptr && thirst->status == OptionStatus::Selected ? thirst->targetPosition : std::nullopt;
		};
		const auto decideAt = [&](glm::vec2 at) {
			world->getComponent<Position>(colonist)->value = at;
			const float last = getTrace(colonist)->lastEvaluationTime;
			for (int i = 0; i < kTicksAfterMesh && getTrace(colonist)->lastEvaluationTime == last; ++i) {
				world->getComponent<Position>(colonist)->value = at;
				world->update(kTick);
			}
			return thirstTarget();
		};
		// The route to the near water fails where it was picked, under the colonist's anchor there.
		const auto failRoute = [&](glm::vec2 at) {
			Task* task = getTask(colonist);
			ASSERT_EQ(task->targetPosition, kNear);
			task->navState = NavState::CantFindWayTo;
			task->blockedReachStamp = SourceReachMemo::Anchor{.at = at, .structural = nav->verdictStamp(at),
															  .beliefVersion = world->getComponent<Memory>(colonist)->beliefVersion}
										  .stamp();
			getMovementTarget(colonist)->active = false;
		};

		getTask(colonist)->clear();
		ASSERT_EQ(decideAt(kSpot), std::optional<glm::vec2>(kNear));
		failRoute(kSpot);
		EXPECT_EQ(decideAt(kSpot), std::optional<glm::vec2>(kFar)) << "the failure is recorded";

		int repicks = 0;
		for (int k = 0; k < 40; ++k) {
			const float		angle = 2.399963F * static_cast<float>(k); // golden angle, spread round the spot
			const float		r	  = 50.0F * std::sqrt(static_cast<float>(k + 1) / 40.0F);
			const glm::vec2 at	  = kSpot + r * glm::vec2{std::cos(angle), std::sin(angle)};
			getTask(colonist)->clear();
			repicks += decideAt(at) == std::optional<glm::vec2>(kNear) ? 1 : 0;
		}
		EXPECT_EQ(repicks, 0) << "wandering round the spot picked the failed route again";

		getTask(colonist)->clear();
		decideAt({200.0F, 0.0F}); // out of the anchor's square: the anchor moves here
		getTask(colonist)->clear();
		EXPECT_EQ(decideAt(kSpot), std::optional<glm::vec2>(kNear)) << "back, under a new anchor, it may try once more";
		failRoute(kSpot);
		EXPECT_EQ(decideAt(kSpot), std::optional<glm::vec2>(kFar));
	}

	// A failed route to water 600 m off, far past the ground the anchor's stamp covers, is forgotten once
	// a wall beside the water goes: the stamp of the ground between the colonist and the water moved. A
	// wall built well off that ground changes nothing.
	TEST_F(AIDecisionWallTest, FailedRouteToAFarTargetClearsWhenAWallBesideItGoes) {
		constexpr glm::vec2 kSpot{0.0F, 0.0F};
		constexpr glm::vec2 kWater{600.0F, 0.0F};
		const auto			buildWallAt = [&](geometry::Vec2i64 a, geometry::Vec2i64 b) {
			   const auto commit = construction.commitSegment(a, b, "Wood", "Standard", construction::kInvalidFoundation);
			   EXPECT_TRUE(commit.ok());
			   for (const auto id : commit.createdSegments) {
				   construction.setSegmentState(id, construction::FoundationState::Built);
			   }
			   return commit.createdSegments;
		};
		const auto besideWater = buildWallAt({604000, -3000}, {604000, 3000});

		const auto colonist = createColonist(kSpot);
		satisfyAllNeeds(*getNeeds(colonist));
		world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
		setNeedValue(colonist, NeedType::Thirst, 30.0F);
		addKnownEntity(colonist, kWater, kWaterDefId, assets::CapabilityType::Drinkable);
		nav->finishPendingBuilds();
		// A wall edit is gathered by the system's update and lands at the join.
		const auto landWallEdits = [&] {
			world->getComponent<Position>(colonist)->value = kSpot;
			world->update(kTick);
			nav->finishPendingBuilds();
		};

		const auto thirstTarget = [&]() -> std::optional<glm::vec2> {
			const auto* thirst = findOption(*getTrace(colonist), [](const EvaluatedOption& o) {
				return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Thirst;
			});
			return thirst != nullptr && thirst->status == OptionStatus::Selected ? thirst->targetPosition : std::nullopt;
		};
		const auto decide = [&] {
			const float last = getTrace(colonist)->lastEvaluationTime;
			for (int i = 0; i < kTicksAfterMesh && getTrace(colonist)->lastEvaluationTime == last; ++i) {
				world->getComponent<Position>(colonist)->value = kSpot;
				world->update(kTick);
			}
			return thirstTarget();
		};

		getTask(colonist)->clear();
		ASSERT_EQ(decide(), std::optional<glm::vec2>(kWater));
		Task* task = getTask(colonist);
		task->navState			= NavState::CantFindWayTo;
		task->blockedReachStamp = SourceReachMemo::Anchor{.at = kSpot, .structural = nav->verdictStamp(kSpot),
														  .beliefVersion = world->getComponent<Memory>(colonist)->beliefVersion}
									  .stamp();
		getMovementTarget(colonist)->active = false;
		EXPECT_EQ(decide(), std::nullopt) << "the failure is recorded";

		const std::uint64_t epochBefore = nav->reachFrom(kSpot, 0.3F).structuralEpoch;
		buildWallAt({300000, 600000}, {304000, 600000});
		landWallEdits();
		ASSERT_NE(nav->reachFrom(kSpot, 0.3F).structuralEpoch, epochBefore);
		getTask(colonist)->clear();
		EXPECT_EQ(decide(), std::nullopt) << "a wall off the ground between them cleared the failure";

		const std::uint64_t spanBefore = nav->spanStamp(kSpot, kWater);
		for (const auto id : besideWater) {
			ASSERT_TRUE(construction.removeSegment(id));
		}
		landWallEdits();
		ASSERT_NE(nav->spanStamp(kSpot, kWater), spanBefore);
		getTask(colonist)->clear();
		EXPECT_EQ(decide(), std::optional<glm::vec2>(kWater)) << "the failure outlived the wall beside the water";
	}

	// Water in a room whose walls the colonist knows, with no door it knows: its reach walks the
	// structural mesh under its belief, so the room reads unreachable and is never chosen, and no route
	// to it is ever asked for. Another water source sits outside.
	class AIDecisionWalledWaterTest : public AIDecisionWallTest {
	  protected:
		EntityID makeThirstyColonistWithWalledWater() {
			const std::array<geometry::Vec2i64, 4> corners{{{6000, -2000}, {10000, -2000}, {10000, 2000}, {6000, 2000}}};
			std::vector<construction::SegmentId>	 walls;
			for (size_t i = 0; i < corners.size(); ++i) {
				const auto commit = construction.commitSegment(corners[i], corners[(i + 1) % corners.size()], "Wood",
															   "Standard", construction::kInvalidFoundation);
				EXPECT_TRUE(commit.ok());
				for (const auto id : commit.createdSegments) {
					construction.setSegmentState(id, construction::FoundationState::Built);
					walls.push_back(id);
				}
				if (i == 1) {
					eastWall = commit.createdSegments;
				}
			}

			const auto colonist = createColonist(kStart);
			satisfyAllNeeds(*getNeeds(colonist));
			world->getComponent<Inventory>(colonist)->addItem("Berry", 1);
			setNeedValue(colonist, NeedType::Thirst, 40.0F);
			auto* memory = world->getComponent<Memory>(colonist);
			for (const auto id : walls) {
				memory->rememberSegment(static_cast<std::uint64_t>(id));
			}
			addKnownEntity(colonist, kInRoom, kWaterDefId, assets::CapabilityType::Drinkable);
			addKnownEntity(colonist, kOutside, kWaterDefId, assets::CapabilityType::Drinkable);

			nav->finishPendingBuilds();
			EXPECT_TRUE(nav->hasMesh()) << "navmesh never built";
			const geometry::nav::BeliefFilter belief{&memory->knownSegments, &memory->knownOpenings};
			EXPECT_FALSE(isReachable(*nav, kStart, kInRoom, 0.3F, belief)) << "the known walls close the room";
			EXPECT_FALSE(nav->requestPath(kStart, kInRoom, 0.3F, belief).route.has_value()) << "and the route agrees";
			return colonist;
		}

		// A wall far from everything, so the region's mesh rebuilds without anything else changing.
		void rebuildRegionMesh() {
			const std::uint64_t before = nav->generation();
			const auto commit = construction.commitSegment({30000, 30000}, {32000, 30000}, "Wood", "Standard",
														   construction::kInvalidFoundation);
			ASSERT_TRUE(commit.ok());
			for (const auto id : commit.createdSegments) {
				construction.setSegmentState(id, construction::FoundationState::Built);
			}
			// Until the rebuilds settle: the commit and the state change can each launch one.
			nav->finishPendingBuilds();
			ASSERT_NE(nav->generation(), before);
		}

		// Runs `ticks` updates and counts the decisions that chose each water.
		void decide(EntityID colonist, int ticks, int& choseRoom, int& choseOutside) {
			float lastDecision = getTrace(colonist)->lastEvaluationTime;
			for (int i = 0; i < ticks; ++i) {
				world->update(kTick);
				const auto* trace = getTrace(colonist);
				if (trace->lastEvaluationTime == lastDecision) {
					continue;
				}
				lastDecision = trace->lastEvaluationTime;
				const auto* thirst = findOption(*trace, [](const EvaluatedOption& o) {
					return o.taskType == TaskType::FulfillNeed && o.needType == NeedType::Thirst;
				});
				if (thirst != nullptr && thirst->status == OptionStatus::Selected) {
					choseRoom += *thirst->targetPosition == kInRoom ? 1 : 0;
					choseOutside += *thirst->targetPosition == kOutside ? 1 : 0;
				}
			}
		}

		// A tree placed far from the colonist with the view over it, so its flora enters the nav mesh: a
		// nav-mesh patch that leaves the structural mesh alone.
		void patchFloraFarAway() {
			assets::AssetDefinition tree;
			tree.defName					 = "Test_FarTree";
			tree.setCollision(0, {.type = assets::CollisionShapeType::Rect, .halfExtentsMeters = {0.4F, 0.4F}});
			assets::AssetRegistry::Get().registerTestDefinition(tree);
			assets::AsyncChunkPlacementResult placed;
			placed.coord = world::worldToChunk({kFarTree.x, kFarTree.y});
			assets::PlacedEntity entity;
			entity.defName	= "Test_FarTree";
			entity.position = kFarTree;
			placed.spatialIndex.insert(entity);
			farProcessed.insert(placed.coord);
			farFlora.storeChunkResult(std::move(placed));
			nav->setPlacementData(&farFlora, &farProcessed);
			nav->setViewportRect(kFarTree, {30.0F, 30.0F});
		}

		// Ground arriving in chunks only the camera loads, far from the colonist: the chunk region
		// widened by a ring the colonist's anchor doesn't reach.
		void meshCameraOnlyGround() {
			wider = std::make_unique<world::ChunkManager>(std::make_unique<world::SyntheticWorldSampler>(7U, world::SyntheticMap{}));
			wider->setLoadRadius(3);
			wider->setUnloadRadius(5);
			wider->update({0.0F, 0.0F});
			wider->finishPendingGeneration();
			nav->setChunkManager(wider.get());
			nav->finishPendingBuilds();
		}

		static constexpr glm::vec2 kInRoom{8.0F, 0.0F};
		static constexpr glm::vec2 kOutside{-30.0F, 0.0F};
		static constexpr glm::vec2 kFarTree{305.0F, 0.0F};

		// The NavigationSystem reads these until the base TearDown resets the world.
		assets::PlacementExecutor							farFlora{assets::AssetRegistry::Get()};
		std::unordered_set<world::ChunkCoordinate>			farProcessed;
		std::unique_ptr<world::ChunkManager>				wider;
		std::vector<construction::SegmentId>				eastWall;
	};

	// The walled-in water reads unreachable, so the colonist goes for the next water and never for that
	// one, and a change to the mesh in its reach square asks the room again and finds it still closed.
	TEST_F(AIDecisionWalledWaterTest, WalledWaterSendsTheNeedToTheNextSource) {
		const auto colonist = makeThirstyColonistWithWalledWater();

		int choseRoom = 0;
		int choseOutside = 0;
		decide(colonist, 20, choseRoom, choseOutside);
		EXPECT_EQ(choseRoom, 0) << "the walled-in water is never chosen";
		EXPECT_GE(choseOutside, 1) << "the colonist goes for the water it can reach";
		EXPECT_EQ(getTask(colonist)->targetPosition, kOutside);

		const glm::vec2 here		 = world->getComponent<Position>(colonist)->value;
		const auto		reachBefore	 = nav->reachFrom(here, 0.3F);
		rebuildRegionMesh();
		const auto reachAfter = nav->reachFrom(here, 0.3F);
		EXPECT_EQ(reachAfter.startCell, reachBefore.startCell);
		EXPECT_NE(reachAfter.structuralEpoch, reachBefore.structuralEpoch) << "a wall in the square moves the key";

		choseRoom	 = 0;
		choseOutside = 0;
		decide(colonist, 20, choseRoom, choseOutside);
		EXPECT_EQ(choseRoom, 0) << "asked again, the room is still closed";
		EXPECT_EQ(getTask(colonist)->targetPosition, kOutside);
	}

	// The room's verdict is dropped when a wall that could open the room goes: the colonist found the
	// room closed at the start, walked 92 m east (the water outside is 130 m off from there, the room
	// 92 m) and the room's east wall, the far side from where it was found, is pulled down. The colonist
	// still lists the wall, so no belief moved; the structural change drops the verdict, and it goes in.
	TEST_F(AIDecisionWalledWaterTest, WallRemovedAfterWalkingOnDropsTheVerdict) {
		const auto colonist = makeThirstyColonistWithWalledWater();
		int		   choseRoom	= 0;
		int		   choseOutside = 0;
		decide(colonist, 10, choseRoom, choseOutside);
		ASSERT_EQ(choseRoom, 0);
		ASSERT_GE(choseOutside, 1);

		world->getComponent<Position>(colonist)->value = {100.0F, 0.0F};
		getTask(colonist)->clear();
		choseRoom = 0;
		choseOutside = 0;
		decide(colonist, 10, choseRoom, choseOutside);
		EXPECT_EQ(choseRoom, 0) << "the room is closed";
		EXPECT_GE(choseOutside, 1);
		choseRoom = 0;

		const std::uint64_t beliefBefore = world->getComponent<Memory>(colonist)->beliefVersion;
		for (const auto id : eastWall) {
			ASSERT_TRUE(construction.removeSegment(id));
		}
		nav->finishPendingBuilds();
		ASSERT_EQ(world->getComponent<Memory>(colonist)->beliefVersion, beliefBefore) << "the colonist still lists the wall";
		decide(colonist, 10, choseRoom, choseOutside);
		EXPECT_GE(choseRoom, 1) << "the verdict outlived the wall";
	}

	// A work target behind the known walls reads unreachable like a need source, so the colonist never
	// takes the cut; with a wall down it takes it.
	TEST_F(AIDecisionWalledWaterTest, WalledWorkTargetIsTakenOnlyOnceTheRoomOpens) {
		const auto colonist = makeThirstyColonistWithWalledWater();
		satisfyAllNeeds(*getNeeds(colonist));
		auto& registry = assets::AssetRegistry::Get();
		addKnownEntity(colonist, kInRoom, registerBerryBush(registry), assets::CapabilityType::Harvestable);
		// A station whose job needs berries, and the Harvest child that cuts for it. The colonist's own
		// berry is its provisions, never the station's.
		assets::RecipeDef recipe;
		recipe.defName = "Recipe_BerryNeed";
		recipe.label = recipe.defName;
		recipe.inputs.push_back({"Berry", 0, 3});
		assets::RecipeRegistry::Get().registerTestRecipe(recipe);
		const EntityID station = world->createEntity();
		world->addComponent<WorkQueue>(station).addJob(recipe.defName, 1);
		world->addComponent<Inventory>(station, Inventory::createForStorage());
		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.destinationPosition = kStart;
		craft.status = GoalStatus::Blocked;
		GoalTask cut;
		cut.type = TaskType::Harvest;
		cut.destinationEntity = station;
		cut.destinationPosition = kStart;
		cut.yieldDefNameId = registry.getDefNameId("Berry");
		cut.status = GoalStatus::Available;
		cut.owner = GoalOwner::CraftingGoalSystem;
		cut.parentGoalId = GoalTaskRegistry::Get().createGoal(std::move(craft));
		GoalTaskRegistry::Get().createGoal(std::move(cut));

		const auto cutsChosen = [&] {
			int	  chosen	   = 0;
			float lastDecision = getTrace(colonist)->lastEvaluationTime;
			for (int i = 0; i < 20; ++i) {
				world->update(kTick);
				const auto* trace = getTrace(colonist);
				if (trace->lastEvaluationTime == lastDecision) {
					continue;
				}
				lastDecision		 = trace->lastEvaluationTime;
				const auto* selected = trace->getSelected();
				chosen += selected != nullptr && selected->taskType == TaskType::Harvest ? 1 : 0;
			}
			return chosen;
		};
		EXPECT_EQ(cutsChosen(), 0) << "the walled-in cut is never taken";
		EXPECT_NE(getTask(colonist)->type, TaskType::Harvest);

		for (const auto id : eastWall) {
			ASSERT_TRUE(construction.removeSegment(id));
		}
		nav->finishPendingBuilds();
		EXPECT_GE(cutsChosen(), 1) << "the same cut, the room open, is never taken";
		EXPECT_EQ(getTask(colonist)->type, TaskType::Harvest);
	}

	// Ground arriving in chunks only the camera holds, far from the colonist, re-requests nobody (the
	// route generation stays) and leaves a failed route's anchor where it was; its walks are asked again
	// (any structural job moves their key) and find the same: the room closed, the water outside.
	TEST_F(AIDecisionWalledWaterTest, CameraOnlyGroundKeepsTheVerdicts) {
		const auto colonist = makeThirstyColonistWithWalledWater();
		world->update(kTick);
		ASSERT_EQ(getTask(colonist)->targetPosition, kOutside);
		const glm::vec2		here	   = world->getComponent<Position>(colonist)->value;
		const std::uint64_t anchor	   = nav->verdictStamp(here);
		const std::uint64_t generation = nav->generation();

		meshCameraOnlyGround();
		ASSERT_TRUE(nav->inSimArea({1700.0F, 0.0F})) << "the camera's ring is meshed";
		EXPECT_EQ(nav->verdictStamp(here), anchor) << "the failed-route anchor moved";
		EXPECT_EQ(nav->generation(), generation) << "the route generation moved";

		int choseRoom = 0;
		int choseOutside = 0;
		decide(colonist, 10, choseRoom, choseOutside);
		EXPECT_EQ(choseRoom, 0);
		EXPECT_EQ(getTask(colonist)->targetPosition, kOutside);
	}

	// Flora entering the nav mesh far away (under the camera as it pans) changes nothing this
	// colonist's verdicts depend on, since reach reads the structural mesh and flora landing leaves the
	// route generation where it is: it keeps to the water outside.
	TEST_F(AIDecisionWalledWaterTest, FloraPatchFarAwayKeepsTheVerdicts) {
		const auto colonist = makeThirstyColonistWithWalledWater();
		world->update(kTick);
		ASSERT_EQ(getTask(colonist)->targetPosition, kOutside);

		const std::uint64_t before = nav->generation();
		patchFloraFarAway();
		world->update(kTick);
		nav->finishPendingBuilds();
		ASSERT_FALSE(nav->isValidPosition(kFarTree)) << "the far tree is placed";
		ASSERT_EQ(nav->generation(), before) << "the flora patch moved the route generation";

		world->update(kTick);
		EXPECT_EQ(getTask(colonist)->targetPosition, kOutside);
	}

#ifdef DEVELOPMENT_BUILD
	// Each update reads more boxes than the table starts with; over many updates the table stays the size
	// one update's reads need instead of growing with every read since the session began.
	TEST_F(AIDecisionSystemTest, BoxRoomCacheStaysSizedToOneUpdate) {
		auto&		   registry = assets::AssetRegistry::Get();
		const uint32_t berry = registry.getDefNameId("Berry");
		std::vector<EntityID> boxes;
		for (int i = 0; i < 200; ++i) {
			const EntityID box = world->createEntity();
			world->addComponent<Inventory>(box, Inventory::createForStorage());
			boxes.push_back(box);
		}
		BoxRoomCache cache;
		size_t		 afterFirst = 0;
		for (int update = 0; update < 100; ++update) {
			cache.invalidate();
			for (EntityID box : boxes) {
				ASSERT_TRUE(cache.get(*world, registry, box, berry).has_value());
			}
			afterFirst = update == 0 ? cache.capacity() : afterFirst;
		}
		EXPECT_EQ(cache.capacity(), afterFirst);
		EXPECT_LE(cache.capacity(), 4 * boxes.size());
		EXPECT_EQ(cache.readOut, 100U * boxes.size());
	}

	// A hungry, thirsty colonist with work on offer for a stocking box, a box pulling from a lower one, and
	// a station: the decision asks the registry by name a fixed number of times, however many trees,
	// bushes, piles, and waters it remembers.
	TEST_F(AIDecisionSystemTest, NameLookupsPerDecisionDoNotGrowWithMemory) {
		auto&					registry = assets::AssetRegistry::Get();
		const auto				carryable = [&](const char* name, uint32_t stack) {
			 assets::AssetDefinition def;
			 def.defName = name;
			 def.label = name;
			 def.category = assets::ItemCategory::RawMaterial;
			 def.capabilities.carryable = assets::CarryableCapability{1};
			 def.itemProperties = assets::ItemProperties{};
			 def.itemProperties->stackSize = stack;
			 def.itemProperties->massKg = 0.1F;
			 registry.registerTestDefinition(std::move(def));
		};
		carryable("Wood", 40);
		carryable("Stone", 40);
		const auto harvestable = [&](const char* name, const char* yield) {
			assets::AssetDefinition def;
			def.defName = name;
			def.label = name;
			def.capabilities.harvestable = assets::HarvestableCapability{};
			def.capabilities.harvestable->yieldDefName = yield;
			registry.registerTestDefinition(std::move(def));
		};
		harvestable("Tree", "Wood");
		harvestable("Flora_BerryBush", "Berry");
		assets::AssetDefinition boxDef;
		boxDef.defName = "Box";
		boxDef.label = "Box";
		boxDef.capabilities.storage = assets::StorageCapability{};
		registry.registerTestDefinition(std::move(boxDef));
		const uint32_t woodId = registry.getDefNameId("Wood");
		const uint32_t stoneId = registry.getDefNameId("Stone");
		const uint32_t treeId = registry.getDefNameId("Tree");
		const uint32_t bushId = registry.getDefNameId("Flora_BerryBush");
		const uint32_t berryId = registry.getDefNameId("Berry");
		const uint32_t boxId = registry.getDefNameId("Box");

		// One decision in a fresh world whose colonist remembers `perKind` of each kind of source.
		const auto lookupsForOneDecision = [&](int perKind) {
			GoalTaskRegistry::Get().clear();
			world = std::make_unique<World>();
			world->registerSystem<AIDecisionSystem>(registry, assets::RecipeRegistry::Get(), kTestRngSeed);

			makeStockingHarvest({0.0F, 8.0F}, "Wood");
			const glm::vec2 highPos{8.0F, 0.0F};
			const glm::vec2 lowPos{-8.0F, 0.0F};
			const EntityID	high = makeStorageBox(highPos, "Box", "Stone", ecs::StoragePriority::High);
			makeStorageBox(lowPos, "Box", "Stone", ecs::StoragePriority::Low, 30);
			makeUmbrellaGoal(high, highPos, stoneId);
			const EntityID station = world->createEntity();
			world->addComponent<Position>(station, Position{{0.0F, -8.0F}});
			makeStationNeed(station, "Wood", 5);

			const EntityID colonist = createColonist({0.0F, 0.0F});
			setNeedValue(colonist, NeedType::Hunger, 40.0F);
			setNeedValue(colonist, NeedType::Thirst, 40.0F);
			setNeedValue(colonist, NeedType::Energy, 100.0F);
			setNeedValue(colonist, NeedType::Bladder, 100.0F);
			addKnownEntity(colonist, highPos, boxId, assets::CapabilityType::Storage);
			addKnownEntity(colonist, lowPos, boxId, assets::CapabilityType::Storage);
			for (int i = 0; i < perKind; ++i) {
				const float		angle = static_cast<float>(i) * 0.37F;
				const float		radius = 12.0F + static_cast<float>(i) * 0.25F;
				const glm::vec2 at{std::cos(angle) * radius, std::sin(angle) * radius};
				addKnownEntity(colonist, at, treeId, assets::CapabilityType::Harvestable);
				addKnownEntity(colonist, at, bushId, assets::CapabilityType::Harvestable);
				addKnownEntity(colonist, at, woodId, assets::CapabilityType::Carryable);
				addKnownEntity(colonist, at, stoneId, assets::CapabilityType::Carryable);
				addKnownEntity(colonist, at, berryId, assets::CapabilityType::Carryable);
				addKnownEntity(colonist, at, kWaterDefId, assets::CapabilityType::Drinkable);
			}

			const uint64_t before = registry.nameLookups();
			world->update(0.016F);
			EXPECT_NE(getTask(colonist)->type, TaskType::None);
			return registry.nameLookups() - before;
		};

		struct Counting {
			assets::AssetRegistry& registry;
			explicit Counting(assets::AssetRegistry& r) : registry(r) { registry.countNameLookups(true); }
			~Counting() { registry.countNameLookups(false); }
			Counting(const Counting&) = delete;
			Counting& operator=(const Counting&) = delete;
		} counting{registry};
		const uint64_t single = lookupsForOneDecision(50);
		const uint64_t doubled = lookupsForOneDecision(100);
		EXPECT_GT(single, 0U) << "counting is on";
		EXPECT_EQ(doubled, single);
		EXPECT_LE(single, 8U);
	}
#endif

} // namespace ecs::test
