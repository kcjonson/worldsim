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

} // namespace ecs::test
