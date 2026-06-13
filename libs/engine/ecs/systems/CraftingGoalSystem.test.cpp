// Minimal test to observe the infinite goal regeneration bug
//
// Bug: Goals are being removed unexpectedly, causing infinite regeneration.
// This test creates a crafting station with a job, runs the system multiple
// times, and checks if goal count stays stable.

#include "CraftingGoalSystem.h"
#include "StorageGoalSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/StorageConfiguration.h"
#include "../components/Transform.h"
#include "../components/WorkQueue.h"

#include <assets/AssetRegistry.h>
#include <assets/RecipeDef.h>
#include <assets/RecipeRegistry.h>

#include <gtest/gtest.h>

namespace ecs::test {

class CraftingGoalSystemTest : public ::testing::Test {
  protected:
	void SetUp() override {
		// Clear registries before each test
		GoalTaskRegistry::Get().clear();
		engine::assets::RecipeRegistry::Get().clear();

		// Register a test recipe
		engine::assets::RecipeDef testRecipe;
		testRecipe.defName = "TestRecipe";
		testRecipe.label = "Test Recipe";
		testRecipe.stationDefName = ""; // No station required
		testRecipe.workAmount = 100.0F;
		engine::assets::RecipeRegistry::Get().registerTestRecipe(testRecipe);

		// Create world and register system
		world = std::make_unique<World>();
		world->registerSystem<CraftingGoalSystem>();
	}

	void TearDown() override {
		GoalTaskRegistry::Get().clear();
		engine::assets::RecipeRegistry::Get().clear();
		world.reset();
	}

	/// Create a crafting station with a job queued
	EntityID createCraftingStation(glm::vec2 pos, const std::string& recipe, uint32_t quantity = 1) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{pos});
		auto& queue = world->addComponent<WorkQueue>(entity);
		if (!recipe.empty()) {
			queue.addJob(recipe, quantity);
		}
		return entity;
	}

	/// Run N system updates (accounting for throttling)
	/// CraftingGoalSystem updates every 60 frames, so 600 updates = 10 actual cycles
	void runUpdates(int count) {
		for (int i = 0; i < count; ++i) {
			world->update(0.016F);
		}
	}

	std::unique_ptr<World> world;
};

// =============================================================================
// Bug Reproduction Test
// =============================================================================

TEST_F(CraftingGoalSystemTest, GoalCountStaysStable_BugRepro) {
	// Setup: Create a crafting station with a queued job
	auto station = createCraftingStation({10.0F, 20.0F}, "TestRecipe", 1);

	// Run enough updates to trigger multiple system cycles
	// System runs every 60 frames, so 600 updates = 10 cycles
	runUpdates(600);

	auto& registry = GoalTaskRegistry::Get();

	// Capture goal count after initial creation
	size_t craftGoals = registry.goalCount(TaskType::Craft);
	size_t totalGoals = registry.goalCount();

	// Log for debugging
	printf("[BugRepro] After 600 updates: Craft goals=%zu, Total goals=%zu\n",
	       craftGoals, totalGoals);

	// Run more updates
	runUpdates(600);

	size_t craftGoalsAfter = registry.goalCount(TaskType::Craft);
	size_t totalGoalsAfter = registry.goalCount();

	printf("[BugRepro] After 1200 updates: Craft goals=%zu, Total goals=%zu\n",
	       craftGoalsAfter, totalGoalsAfter);

	// KEY ASSERTION: Goal count should be stable
	EXPECT_EQ(craftGoalsAfter, craftGoals) << "Craft goal count changed - possible regeneration bug";
	EXPECT_GE(craftGoalsAfter, 1U) << "Should have at least 1 craft goal";
}

TEST_F(CraftingGoalSystemTest, GoalPersistsAcrossUpdates) {
	auto station = createCraftingStation({0.0F, 0.0F}, "TestRecipe", 1);

	// First cycle - goal should be created
	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	const auto* goal1 = registry.getGoalByDestination(station);

	ASSERT_NE(goal1, nullptr) << "Goal should be created for station with job";
	uint64_t originalId = goal1->id;

	printf("[Persist] Goal created with ID %llu\n", static_cast<unsigned long long>(originalId));

	// Run more cycles
	runUpdates(120);

	const auto* goal2 = registry.getGoalByDestination(station);
	ASSERT_NE(goal2, nullptr) << "Goal should persist across updates";

	printf("[Persist] After more updates, goal ID is %llu\n",
	       static_cast<unsigned long long>(goal2->id));

	// Same goal ID means it persisted, different ID means it was recreated
	EXPECT_EQ(goal2->id, originalId) << "Goal was recreated (different ID) - this is the bug!";
}

TEST_F(CraftingGoalSystemTest, NoGoalWhenNoJob) {
	auto station = createCraftingStation({0.0F, 0.0F}, "", 0);

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	const auto* goal = registry.getGoalByDestination(station);

	EXPECT_EQ(goal, nullptr) << "Should not create goal for station with no job";
	EXPECT_EQ(registry.goalCount(TaskType::Craft), 0U);
}

TEST_F(CraftingGoalSystemTest, GoalRemovedWhenJobCompleted) {
	auto station = createCraftingStation({0.0F, 0.0F}, "TestRecipe", 1);

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	ASSERT_NE(registry.getGoalByDestination(station), nullptr);

	// Simulate job completion by removing it from queue
	auto* queue = world->getComponent<WorkQueue>(station);
	ASSERT_NE(queue, nullptr);
	queue->jobs.clear();

	// Run update - goal should be removed
	runUpdates(60);

	const auto* goal = registry.getGoalByDestination(station);
	EXPECT_EQ(goal, nullptr) << "Goal should be removed when job is complete";
}

// =============================================================================
// Multi-System Interaction Tests
// =============================================================================

class MultiSystemGoalTest : public ::testing::Test {
  protected:
	void SetUp() override {
		GoalTaskRegistry::Get().clear();
		engine::assets::RecipeRegistry::Get().clear();
		engine::assets::AssetRegistry::Get().clearDefinitions();

		// Register "Stick" asset so recipes with Stick inputs work
		engine::assets::AssetDefinition stickDef;
		stickDef.defName = "Stick";
		stickDef.label = "Stick";
		stickDef.category = engine::assets::ItemCategory::RawMaterial;
		stickDef.itemProperties = engine::assets::ItemProperties{};
		stickDef.itemProperties->stackSize = 10;
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(stickDef));

		// Register test recipe
		engine::assets::RecipeDef testRecipe;
		testRecipe.defName = "TestRecipe";
		testRecipe.label = "Test Recipe";
		testRecipe.stationDefName = "";
		testRecipe.workAmount = 100.0F;
		engine::assets::RecipeRegistry::Get().registerTestRecipe(testRecipe);

		// Create world with BOTH systems
		world = std::make_unique<World>();
		world->registerSystem<StorageGoalSystem>();  // Priority 55
		world->registerSystem<CraftingGoalSystem>(); // Priority 56
	}

	void TearDown() override {
		GoalTaskRegistry::Get().clear();
		engine::assets::RecipeRegistry::Get().clear();
		engine::assets::AssetRegistry::Get().clearDefinitions();
		world.reset();
	}

	EntityID createCraftingStation(glm::vec2 pos, const std::string& recipe) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{pos});
		auto& queue = world->addComponent<WorkQueue>(entity);
		if (!recipe.empty()) {
			queue.addJob(recipe, 1);
		}
		return entity;
	}

	EntityID createStorage(glm::vec2 pos) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{pos});
		world->addComponent<Inventory>(entity);
		auto& config = world->addComponent<StorageConfiguration>(entity);
		// Accept all raw materials
		config.addRule(StorageRule{
		    .defName = "*",
		    .category = engine::assets::ItemCategory::RawMaterial,
		    .priority = StoragePriority::Medium,
		});
		return entity;
	}

	void runUpdates(int count) {
		for (int i = 0; i < count; ++i) {
			world->update(0.016F);
		}
	}

	std::unique_ptr<World> world;
};

// THIS TEST CATCHES THE INFINITE REGENERATION BUG!
//
// Root cause: StorageGoalSystem iterates ALL Haul goals and builds a set of
// destination entities. When it doesn't find a matching Storage entity in its
// component view, it calls removeGoalByDestination() - but this removes the
// CRAFT goal (which is in destinationToGoal index) instead of the Haul goal.
// CraftingGoalSystem then recreates the goal hierarchy, creating orphaned
// Haul goals each cycle.
//
// Fix: StorageGoalSystem.cpp line 30-32 should skip child Haul goals:
//   if (!goal->parentGoalId.has_value()) {
//       storagesWithGoals.insert(goal->destinationEntity);
//   }
TEST_F(MultiSystemGoalTest, RecipeWithInputsCreatesChildGoals) {
	// Register a recipe WITH inputs - this triggers child goal creation
	engine::assets::RecipeDef recipeWithInputs;
	recipeWithInputs.defName = "RecipeWithInputs";
	recipeWithInputs.label = "Recipe With Inputs";
	recipeWithInputs.stationDefName = "";
	recipeWithInputs.workAmount = 100.0F;
	recipeWithInputs.inputs.push_back(engine::assets::RecipeInput{
	    .defName = "Stick",
	    .defNameId = 0,
	    .count = 2});
	engine::assets::RecipeRegistry::Get().registerTestRecipe(recipeWithInputs);

	// Create station with the recipe that has inputs
	auto station = createCraftingStation({0.0F, 0.0F}, "RecipeWithInputs");

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();

	size_t craftGoals = registry.goalCount(TaskType::Craft);
	size_t harvestGoals = registry.goalCount(TaskType::Harvest);
	size_t haulGoals = registry.goalCount(TaskType::Haul);

	printf("[InputsTest] After 60 updates: Craft=%zu, Harvest=%zu, Haul=%zu\n",
	       craftGoals, harvestGoals, haulGoals);

	// Should have craft goal
	EXPECT_EQ(craftGoals, 1U) << "Should have 1 craft goal";

	// Should have haul goal (for the input)
	EXPECT_GE(haulGoals, 1U) << "Should have haul goal for input";

	// Run more updates - goal counts should be stable
	runUpdates(600);

	size_t craftGoalsAfter = registry.goalCount(TaskType::Craft);
	size_t haulGoalsAfter = registry.goalCount(TaskType::Haul);

	printf("[InputsTest] After 660 updates: Craft=%zu, Haul=%zu\n",
	       craftGoalsAfter, haulGoalsAfter);

	EXPECT_EQ(craftGoalsAfter, craftGoals) << "Craft goal count changed - regeneration bug!";
	EXPECT_EQ(haulGoalsAfter, haulGoals) << "Haul goal count changed - regeneration bug!";
}

TEST_F(MultiSystemGoalTest, BothSystemsRunWithoutInterference) {
	// Create both a crafting station and a storage
	auto station = createCraftingStation({0.0F, 0.0F}, "TestRecipe");
	auto storage = createStorage({10.0F, 0.0F});

	// Run enough updates for both systems to cycle multiple times
	runUpdates(600);

	auto& registry = GoalTaskRegistry::Get();

	// Check Craft goals (from CraftingGoalSystem)
	size_t craftGoals = registry.goalCount(TaskType::Craft);

	// Check Haul goals (from StorageGoalSystem)
	size_t haulGoals = registry.goalCount(TaskType::Haul);

	printf("[MultiSystem] After 600 updates: Craft=%zu, Haul=%zu, Total=%zu\n",
	       craftGoals, haulGoals, registry.goalCount());

	// Run more updates
	runUpdates(600);

	size_t craftGoalsAfter = registry.goalCount(TaskType::Craft);
	size_t haulGoalsAfter = registry.goalCount(TaskType::Haul);

	printf("[MultiSystem] After 1200 updates: Craft=%zu, Haul=%zu, Total=%zu\n",
	       craftGoalsAfter, haulGoalsAfter, registry.goalCount());

	// Goal counts should be stable
	EXPECT_EQ(craftGoalsAfter, craftGoals) << "Craft goal count changed!";
	EXPECT_GE(craftGoalsAfter, 1U) << "Should have craft goal";
}

// Recipe swap: when a station's next job changes to a different recipe, the old recipe's
// child Harvest/Haul goals are torn down and rebuilt to match the new recipe's inputs.
TEST_F(MultiSystemGoalTest, RecipeSwapRebuildsChildHierarchy) {
	auto& assetReg = engine::assets::AssetRegistry::Get();

	// A second material so recipe B's inputs differ from recipe A's.
	engine::assets::AssetDefinition fiberDef;
	fiberDef.defName = "PlantFiber";
	fiberDef.label = "Plant Fiber";
	fiberDef.category = engine::assets::ItemCategory::RawMaterial;
	fiberDef.itemProperties = engine::assets::ItemProperties{};
	fiberDef.itemProperties->stackSize = 10;
	assetReg.registerTestDefinition(std::move(fiberDef));

	engine::assets::RecipeDef recipeA;
	recipeA.defName = "RecipeA";
	recipeA.label = "Recipe A";
	recipeA.stationDefName = "";
	recipeA.workAmount = 100.0F;
	recipeA.inputs.push_back(engine::assets::RecipeInput{.defName = "Stick", .defNameId = 0, .count = 2});
	engine::assets::RecipeRegistry::Get().registerTestRecipe(recipeA);

	engine::assets::RecipeDef recipeB;
	recipeB.defName = "RecipeB";
	recipeB.label = "Recipe B";
	recipeB.stationDefName = "";
	recipeB.workAmount = 100.0F;
	recipeB.inputs.push_back(engine::assets::RecipeInput{.defName = "PlantFiber", .defNameId = 0, .count = 3});
	engine::assets::RecipeRegistry::Get().registerTestRecipe(recipeB);

	uint32_t stickId = assetReg.getDefNameId("Stick");
	uint32_t fiberId = assetReg.getDefNameId("PlantFiber");

	auto  station = createCraftingStation({0.0F, 0.0F}, "RecipeA");
	auto& registry = GoalTaskRegistry::Get();

	runUpdates(60);

	const auto* craftA = registry.getGoalByDestination(station);
	ASSERT_NE(craftA, nullptr);
	uint64_t craftIdA = craftA->id;

	// Children should accept Stick (recipe A's input).
	{
		auto children = registry.getChildGoals(craftIdA);
		ASSERT_FALSE(children.empty());
		for (const auto* child : children) {
			ASSERT_FALSE(child->acceptedDefNameIds.empty());
			EXPECT_EQ(child->acceptedDefNameIds[0], stickId) << "Recipe A children should accept Stick";
		}
	}

	// Swap the queued job to recipe B (simulates job A finishing, job B becoming next).
	auto* queue = world->getComponent<WorkQueue>(station);
	ASSERT_NE(queue, nullptr);
	queue->jobs.clear();
	queue->addJob("RecipeB", 1);

	runUpdates(60);

	const auto* craftB = registry.getGoalByDestination(station);
	ASSERT_NE(craftB, nullptr);

	// Children must now match recipe B (accept PlantFiber, not Stick).
	auto childrenB = registry.getChildGoals(craftB->id);
	ASSERT_FALSE(childrenB.empty());
	for (const auto* child : childrenB) {
		ASSERT_FALSE(child->acceptedDefNameIds.empty());
		EXPECT_EQ(child->acceptedDefNameIds[0], fiberId) << "After swap, children should accept PlantFiber";
		EXPECT_NE(child->acceptedDefNameIds[0], stickId) << "Stale Stick children must be gone";
	}

	// Craft material total reflects recipe B (3), reset and Blocked again.
	EXPECT_EQ(craftB->targetAmount, 3U);
	EXPECT_EQ(craftB->deliveredAmount, 0U);
	EXPECT_EQ(craftB->status, GoalStatus::Blocked);
}

TEST_F(MultiSystemGoalTest, GoalOwnershipIsSetCorrectly) {
	// Create crafting station with recipe that has inputs (creates child goals)
	engine::assets::RecipeDef recipeWithInputs;
	recipeWithInputs.defName = "OwnershipTestRecipe";
	recipeWithInputs.label = "Ownership Test Recipe";
	recipeWithInputs.stationDefName = "";
	recipeWithInputs.workAmount = 100.0F;
	recipeWithInputs.inputs.push_back(engine::assets::RecipeInput{
	    .defName = "Stick",
	    .defNameId = 0,
	    .count = 1});
	engine::assets::RecipeRegistry::Get().registerTestRecipe(recipeWithInputs);

	auto station = createCraftingStation({0.0F, 0.0F}, "OwnershipTestRecipe");
	auto storage = createStorage({10.0F, 0.0F});

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();

	// Verify CraftingGoalSystem goals have correct owner
	size_t craftingOwnedGoals = registry.goalCount(GoalOwner::CraftingGoalSystem);
	EXPECT_GE(craftingOwnedGoals, 2U) << "CraftingGoalSystem should own Craft + child Haul goals";

	// Verify StorageGoalSystem goals have correct owner
	size_t storageOwnedGoals = registry.goalCount(GoalOwner::StorageGoalSystem);
	EXPECT_GE(storageOwnedGoals, 1U) << "StorageGoalSystem should own storage Haul goal";

	// Verify ownership separation - getGoalsByOwner returns only owned goals
	auto craftingGoals = registry.getGoalsByOwner(GoalOwner::CraftingGoalSystem);
	auto storageGoals = registry.getGoalsByOwner(GoalOwner::StorageGoalSystem);

	for (const auto* goal : craftingGoals) {
		EXPECT_EQ(goal->owner, GoalOwner::CraftingGoalSystem);
	}

	for (const auto* goal : storageGoals) {
		EXPECT_EQ(goal->owner, GoalOwner::StorageGoalSystem);
	}

	printf("[Ownership] CraftingGoalSystem owns %zu goals, StorageGoalSystem owns %zu goals\n",
	       craftingOwnedGoals, storageOwnedGoals);
}

// =============================================================================
// Goal Hierarchy & Lifecycle Tests (registry semantics)
// =============================================================================

class GoalHierarchyTest : public ::testing::Test {
  protected:
	void SetUp() override { GoalTaskRegistry::Get().clear(); }
	void TearDown() override { GoalTaskRegistry::Get().clear(); }

	/// Build a Craft goal with a child Harvest and a child Haul that depends on the Harvest.
	/// Returns the three goal IDs.
	struct Hierarchy {
		uint64_t craftId;
		uint64_t harvestId;
		uint64_t haulId;
	};

	Hierarchy buildCraftHierarchy(EntityID station) {
		auto& registry = GoalTaskRegistry::Get();

		GoalTask craft;
		craft.type = TaskType::Craft;
		craft.owner = GoalOwner::CraftingGoalSystem;
		craft.destinationEntity = station;
		craft.targetAmount = 1;
		craft.status = GoalStatus::Blocked;
		uint64_t craftId = registry.createGoal(std::move(craft));

		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::CraftingGoalSystem;
		harvest.destinationEntity = station;
		harvest.targetAmount = 1;
		harvest.parentGoalId = craftId;
		harvest.status = GoalStatus::Available;
		uint64_t harvestId = registry.createGoal(std::move(harvest));

		GoalTask haul;
		haul.type = TaskType::Haul;
		haul.owner = GoalOwner::CraftingGoalSystem;
		haul.destinationEntity = station;
		haul.targetAmount = 1;
		haul.parentGoalId = craftId;
		haul.dependsOnGoalId = harvestId;
		haul.status = GoalStatus::WaitingForItems;
		uint64_t haulId = registry.createGoal(std::move(haul));

		return {craftId, harvestId, haulId};
	}
};

// (a) Cancel cascade: removing a Craft goal removes all child Haul/Harvest goals
TEST_F(GoalHierarchyTest, RemoveGoalWithChildrenCascades) {
	auto& registry = GoalTaskRegistry::Get();
	auto  h = buildCraftHierarchy(static_cast<EntityID>(42));

	ASSERT_EQ(registry.goalCount(), 3U);
	ASSERT_EQ(registry.getChildGoals(h.craftId).size(), 2U);

	registry.removeGoalWithChildren(h.craftId);

	EXPECT_EQ(registry.goalCount(), 0U) << "Craft + both children should be gone";
	EXPECT_EQ(registry.getGoal(h.craftId), nullptr);
	EXPECT_EQ(registry.getGoal(h.harvestId), nullptr);
	EXPECT_EQ(registry.getGoal(h.haulId), nullptr);
}

// (a') The WorkQueue-job-removed path in CraftingGoalSystem reaps children too
TEST_F(CraftingGoalSystemTest, JobRemovedReapsChildGoals) {
	// Recipe with an input creates a Craft + child Haul hierarchy
	engine::assets::RecipeDef recipe;
	recipe.defName = "ReapRecipe";
	recipe.label = "Reap Recipe";
	recipe.stationDefName = "";
	recipe.workAmount = 100.0F;
	engine::assets::AssetRegistry::Get().clearDefinitions();
	engine::assets::AssetDefinition stickDef;
	stickDef.defName = "Stick";
	stickDef.label = "Stick";
	stickDef.category = engine::assets::ItemCategory::RawMaterial;
	stickDef.itemProperties = engine::assets::ItemProperties{};
	stickDef.itemProperties->stackSize = 10;
	engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(stickDef));
	recipe.inputs.push_back(engine::assets::RecipeInput{.defName = "Stick", .defNameId = 0, .count = 1});
	engine::assets::RecipeRegistry::Get().registerTestRecipe(recipe);

	auto  station = createCraftingStation({0.0F, 0.0F}, "ReapRecipe", 1);
	auto& registry = GoalTaskRegistry::Get();

	runUpdates(60);
	ASSERT_GE(registry.goalCount(), 2U) << "Craft + child Haul should exist";

	// Remove the job; the system should reap the whole hierarchy
	auto* queue = world->getComponent<WorkQueue>(station);
	ASSERT_NE(queue, nullptr);
	queue->jobs.clear();

	runUpdates(60);

	EXPECT_EQ(registry.goalCount(), 0U) << "All goals reaped when job removed";

	engine::assets::AssetRegistry::Get().clearDefinitions();
}

// (b) Dependency unlock: completing a Harvest flips its dependent Haul to Available
TEST_F(GoalHierarchyTest, NotifyGoalCompletedUnlocksDependents) {
	auto& registry = GoalTaskRegistry::Get();
	auto  h = buildCraftHierarchy(static_cast<EntityID>(7));

	ASSERT_EQ(registry.getGoal(h.haulId)->status, GoalStatus::WaitingForItems);
	ASSERT_EQ(registry.getDependentGoals(h.harvestId).size(), 1U);

	registry.notifyGoalCompleted(h.harvestId);

	EXPECT_EQ(registry.getGoal(h.haulId)->status, GoalStatus::Available)
	    << "Haul should unblock once its Harvest dependency completes";
}

// (c) Delivery completion: recordDelivery increments deliveredAmount and completes at target
TEST_F(GoalHierarchyTest, RecordDeliveryCompletesGoal) {
	auto& registry = GoalTaskRegistry::Get();

	GoalTask haul;
	haul.type = TaskType::Haul;
	haul.owner = GoalOwner::StorageGoalSystem;
	haul.destinationEntity = static_cast<EntityID>(99);
	haul.targetAmount = 3;
	uint64_t haulId = registry.createGoal(std::move(haul));

	EXPECT_EQ(registry.getGoal(haulId)->deliveredAmount, 0U);
	EXPECT_EQ(registry.getGoal(haulId)->availableCapacity(), 3U);
	EXPECT_FALSE(registry.getGoal(haulId)->isComplete());

	// recordDelivery now counts ITEMS, not actions: one haul moving 2 items records 2.
	registry.recordDelivery(haulId, 2);
	EXPECT_EQ(registry.getGoal(haulId)->deliveredAmount, 2U);
	EXPECT_EQ(registry.getGoal(haulId)->availableCapacity(), 1U);
	EXPECT_FALSE(registry.getGoal(haulId)->isComplete());

	// A zero-amount delivery is a no-op.
	registry.recordDelivery(haulId, 0);
	EXPECT_EQ(registry.getGoal(haulId)->deliveredAmount, 2U);

	registry.recordDelivery(haulId, 1);
	EXPECT_EQ(registry.getGoal(haulId)->deliveredAmount, 3U);
	EXPECT_EQ(registry.getGoal(haulId)->availableCapacity(), 0U);
	EXPECT_TRUE(registry.getGoal(haulId)->isComplete());
}

// (c') Delivering to a child Haul also credits its parent Craft goal and unblocks it once the
// materials are satisfied (single source of truth: child and parent advance together).
TEST_F(GoalHierarchyTest, ChildHaulDeliveryCreditsParentCraftAndUnblocks) {
	auto& registry = GoalTaskRegistry::Get();

	GoalTask craft;
	craft.type = TaskType::Craft;
	craft.owner = GoalOwner::CraftingGoalSystem;
	craft.destinationEntity = static_cast<EntityID>(123);
	craft.targetAmount = 3;
	craft.status = GoalStatus::Blocked;
	uint64_t craftId = registry.createGoal(std::move(craft));

	GoalTask haul;
	haul.type = TaskType::Haul;
	haul.owner = GoalOwner::CraftingGoalSystem;
	haul.destinationEntity = static_cast<EntityID>(123);
	haul.targetAmount = 3;
	haul.parentGoalId = craftId;
	haul.status = GoalStatus::Available;
	uint64_t haulId = registry.createGoal(std::move(haul));

	registry.recordDelivery(haulId, 2);
	// Both child and parent advanced by the same amount, no double-count.
	EXPECT_EQ(registry.getGoal(haulId)->deliveredAmount, 2U);
	EXPECT_EQ(registry.getGoal(craftId)->deliveredAmount, 2U);
	EXPECT_EQ(registry.getGoal(craftId)->status, GoalStatus::Blocked)
	    << "Craft stays Blocked until all materials delivered";

	registry.recordDelivery(haulId, 1);
	EXPECT_EQ(registry.getGoal(craftId)->deliveredAmount, 3U);
	EXPECT_EQ(registry.getGoal(craftId)->status, GoalStatus::Available)
	    << "Craft leaves Blocked once materials are satisfied";
}

} // namespace ecs::test
