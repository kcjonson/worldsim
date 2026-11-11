// Minimal test to observe the infinite goal regeneration bug
//
// Bug: Goals are being removed unexpectedly, causing infinite regeneration.
// This test creates a crafting station with a job, runs the system multiple
// times, and checks if goal count stays stable.

#include "CraftingGoalSystem.h"
#include "GoalSystemHelpers.h"
#include "StorageGoalSystem.h"

#include "../GoalTaskRegistry.h"
#include "../World.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
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
		assets::RecipeRegistry::Get().clear();

		// Register a test recipe
		assets::RecipeDef testRecipe;
		testRecipe.defName = "TestRecipe";
		testRecipe.label = "Test Recipe";
		testRecipe.stationDefName = ""; // No station required
		testRecipe.workAmount = 100.0F;
		assets::RecipeRegistry::Get().registerTestRecipe(testRecipe);

		// Create world and register system
		world = std::make_unique<World>();
		world->registerSystem<CraftingGoalSystem>();
	}

	void TearDown() override {
		GoalTaskRegistry::Get().clear();
		assets::RecipeRegistry::Get().clear();
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
		assets::RecipeRegistry::Get().clear();
		assets::AssetRegistry::Get().clearDefinitions();

		// Register "Stick" asset so recipes with Stick inputs work
		assets::AssetDefinition stickDef;
		stickDef.defName = "Stick";
		stickDef.label = "Stick";
		stickDef.category = assets::ItemCategory::RawMaterial;
		stickDef.itemProperties = assets::ItemProperties{};
		stickDef.itemProperties->stackSize = 10;
		assets::AssetRegistry::Get().registerTestDefinition(std::move(stickDef));

		// Register test recipe
		assets::RecipeDef testRecipe;
		testRecipe.defName = "TestRecipe";
		testRecipe.label = "Test Recipe";
		testRecipe.stationDefName = "";
		testRecipe.workAmount = 100.0F;
		assets::RecipeRegistry::Get().registerTestRecipe(testRecipe);

		// Create world with BOTH systems
		world = std::make_unique<World>();
		world->registerSystem<StorageGoalSystem>();  // Priority 55
		world->registerSystem<CraftingGoalSystem>(); // Priority 56
	}

	void TearDown() override {
		GoalTaskRegistry::Get().clear();
		assets::RecipeRegistry::Get().clear();
		assets::AssetRegistry::Get().clearDefinitions();
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
		    .category = assets::ItemCategory::RawMaterial,
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
	assets::RecipeDef recipeWithInputs;
	recipeWithInputs.defName = "RecipeWithInputs";
	recipeWithInputs.label = "Recipe With Inputs";
	recipeWithInputs.stationDefName = "";
	recipeWithInputs.workAmount = 100.0F;
	recipeWithInputs.inputs.push_back(assets::RecipeInput{
	    .defName = "Stick",
	    .defNameId = 0,
	    .count = 2});
	assets::RecipeRegistry::Get().registerTestRecipe(recipeWithInputs);

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

// Materials already sitting in the station's store count as delivered: the craft goal is born
// Available with no child Haul/Harvest, because nothing more needs provisioning. Mirrors a build
// site whose delivered[] manifest is already satisfied.
TEST_F(MultiSystemGoalTest, PrestagedStoreMaterialsCreateNoChildGoalsAndUnblockCraft) {
	assets::RecipeDef recipeWithInputs;
	recipeWithInputs.defName = "RecipeWithInputs";
	recipeWithInputs.label = "Recipe With Inputs";
	recipeWithInputs.stationDefName = "";
	recipeWithInputs.workAmount = 100.0F;
	recipeWithInputs.inputs.push_back(assets::RecipeInput{.defName = "Stick", .defNameId = 0, .count = 2});
	assets::RecipeRegistry::Get().registerTestRecipe(recipeWithInputs);

	// A station whose store ALREADY holds the full recipe input (2 Sticks).
	auto  station = createCraftingStation({0.0F, 0.0F}, "RecipeWithInputs");
	auto& store = world->addComponent<Inventory>(station, Inventory::createForStorage());
	store.addItem("Stick", 2);

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	EXPECT_EQ(registry.goalCount(TaskType::Craft), 1U) << "one craft goal for the station";
	EXPECT_EQ(registry.goalCount(TaskType::Haul), 0U) << "no haul: the store already holds the input";
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U) << "no harvest either";

	const auto* craft = registry.getGoalByDestination(station);
	ASSERT_NE(craft, nullptr);
	EXPECT_EQ(registry.remainingNeed(*world, craft->id, assets::AssetRegistry::Get().getDefNameId("Stick"), NeedBasis::Stock), 0U)
		<< "store contents count as landed";
	EXPECT_EQ(craft->status, GoalStatus::Available) << "all inputs present -> craft is workable immediately";
}

// Regression: a multi-unit craft must not stall at 1/N. The Craft root's remaining need is read
// from the live store toward the NEXT unit; finishing a unit drains the store, so the need returns
// and provisioning reopens for the next unit. A monotonic counter here once hung the job at 1/N. Here we drive all 3 units by mirroring exactly what
// CraftActions does on unit completion (drain the recipe inputs from the store, bump job.completed)
// and confirm the goal system re-provisions between units and the job runs to 3/3.
TEST_F(MultiSystemGoalTest, MultiUnitCraftReprovisionsBetweenUnitsAndCompletesAllN) {
	assets::RecipeDef recipe;
	recipe.defName = "MultiUnitRecipe";
	recipe.label = "Multi Unit Recipe";
	recipe.stationDefName = "";
	recipe.workAmount = 100.0F;
	recipe.inputs.push_back(assets::RecipeInput{.defName = "Stick", .defNameId = 0, .count = 2});
	assets::RecipeRegistry::Get().registerTestRecipe(recipe);

	// Station with a 3-unit job. createCraftingStation hardcodes quantity=1, so queue the job here.
	auto  station = world->createEntity();
	world->addComponent<Position>(station, Position{{0.0F, 0.0F}});
	auto& queue = world->addComponent<WorkQueue>(station);
	queue.addJob("MultiUnitRecipe", 3);
	auto& store = world->addComponent<Inventory>(station, Inventory::createForStorage());

	auto& registry = GoalTaskRegistry::Get();

	// Mirror one unit of crafting: consume the recipe inputs from the store and bump completed,
	// exactly as ActionSystem::applyCraftingEffect does on a finished craft.
	auto craftOneUnit = [&]() {
		for (const auto& input : recipe.inputs) {
			store.removeItem(input.defName, input.count);
		}
		auto* job = queue.getNextJob();
		ASSERT_NE(job, nullptr);
		job->completed++;
		queue.cleanupCompleted();
	};

	uint32_t unitsCrafted = 0;
	for (int unit = 0; unit < 3; ++unit) {
		// Stage materials for this unit (a colonist hauling them in -> store holds 2 Sticks).
		store.addItem("Stick", 2);
		runUpdates(60);

		const auto* craft = registry.getGoalByDestination(station);
		ASSERT_NE(craft, nullptr) << "Craft goal must persist across all units (unit " << unit << ")";
		EXPECT_EQ(registry.remainingNeed(*world, craft->id, assets::AssetRegistry::Get().getDefNameId("Stick"), NeedBasis::Stock), 0U)
		    << "fully staged for this unit (unit " << unit << ")";
		EXPECT_EQ(craft->status, GoalStatus::Available)
		    << "fully staged -> craft is workable for this unit (unit " << unit << ")";

		// This unit crafts: store drains, job advances.
		craftOneUnit();
		++unitsCrafted;

		// Goal system sees the drained store: the need returns, the craft re-blocks, and a Haul
		// reopens provisioning for the next unit -- unless
		// this was the last unit, in which case the job is gone and the whole hierarchy is reaped.
		runUpdates(60);
		if (unit < 2) {
			const auto* next = registry.getGoalByDestination(station);
			ASSERT_NE(next, nullptr) << "Craft goal still present for remaining units";
			EXPECT_EQ(registry.remainingNeed(*world, next->id, assets::AssetRegistry::Get().getDefNameId("Stick"), NeedBasis::Stock), 2U)
			    << "store drained by the finished unit -> the next unit needs both again";
			EXPECT_EQ(next->status, GoalStatus::Blocked)
			    << "craft re-blocked, waiting on the next unit's materials";
			EXPECT_GE(registry.goalCount(TaskType::Haul), 1U)
			    << "provisioning rebuilt: a Haul exists to gather the next unit's materials";
		}
	}

	EXPECT_EQ(unitsCrafted, 3U) << "all three units crafted, no stall at 1/3";
	EXPECT_EQ(queue.getNextJob(), nullptr) << "the 3-unit job is fully complete";
	EXPECT_EQ(registry.goalCount(TaskType::Craft), 0U) << "no pending work -> craft hierarchy reaped";
}

// A harvestable input (something a tree yields) creates only a Harvest goal. The cut itself carries
// the material to the station, so no haul shows in the task list for it.
TEST_F(MultiSystemGoalTest, HarvestableInputCreatesHarvestNotHaul) {
	auto& assetReg = assets::AssetRegistry::Get();

	assets::AssetDefinition woodDef;
	woodDef.defName = "Wood";
	woodDef.label = "Wood";
	woodDef.category = assets::ItemCategory::RawMaterial;
	woodDef.itemProperties = assets::ItemProperties{};
	woodDef.itemProperties->stackSize = 40;
	assetReg.registerTestDefinition(std::move(woodDef));

	// A harvestable that yields Wood makes Wood harvestable.
	assets::AssetDefinition treeDef;
	treeDef.defName = "TestTree";
	treeDef.label = "Test Tree";
	treeDef.capabilities.harvestable = assets::HarvestableCapability{};
	treeDef.capabilities.harvestable->yieldDefName = "Wood";
	assetReg.registerTestDefinition(std::move(treeDef));

	assets::RecipeDef woodRecipe;
	woodRecipe.defName = "WoodRecipe";
	woodRecipe.label = "Wood Recipe";
	woodRecipe.stationDefName = "";
	woodRecipe.workAmount = 100.0F;
	woodRecipe.inputs.push_back(assets::RecipeInput{.defName = "Wood", .defNameId = 0, .count = 1});
	assets::RecipeRegistry::Get().registerTestRecipe(woodRecipe);

	// Resolution is memory-driven: a colonist must KNOW a harvestable source for the input to
	// resolve to a Harvest (otherwise it waits as NoSource). Give one a remembered TestTree.
	auto  colonist = world->createEntity();
	auto& mem = world->addComponent<Memory>(colonist);
	uint32_t treeId = assetReg.getDefNameId("TestTree");
	mem.rememberWorldEntity({2.0F, 2.0F}, treeId, assetReg.getCapabilityMask(treeId));

	auto  station = createCraftingStation({0.0F, 0.0F}, "WoodRecipe");
	auto& registry = GoalTaskRegistry::Get();

	runUpdates(60);

	EXPECT_EQ(registry.goalCount(TaskType::Craft), 1U) << "one craft goal for the station";
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 1U) << "harvestable input gets a Harvest goal";
	EXPECT_EQ(registry.goalCount(TaskType::Haul), 0U)
	    << "no Haul exists until the cut completes (lazy haul)";
}

// Regression: leftover material sitting in ANOTHER station's (or storage's) store must NOT count
// as "carried stock". colonyCarriesStock once viewed every Inventory, so a stack stranded in a
// second crafting station made the input resolve to a fetch Haul that can never source it (a
// craft-material haul only delivers from a colonist's pack or a loose ground pile), stranding the
// craft. It must still resolve to a Harvest of the known source.
TEST_F(MultiSystemGoalTest, LeftoverInOtherStationStoreDoesNotBlockHarvest) {
	auto& assetReg = assets::AssetRegistry::Get();

	assets::AssetDefinition woodDef;
	woodDef.defName = "Wood";
	woodDef.label = "Wood";
	woodDef.category = assets::ItemCategory::RawMaterial;
	woodDef.itemProperties = assets::ItemProperties{};
	woodDef.itemProperties->stackSize = 40;
	assetReg.registerTestDefinition(std::move(woodDef));

	assets::AssetDefinition treeDef;
	treeDef.defName = "TestTree";
	treeDef.label = "Test Tree";
	treeDef.capabilities.harvestable = assets::HarvestableCapability{};
	treeDef.capabilities.harvestable->yieldDefName = "Wood";
	assetReg.registerTestDefinition(std::move(treeDef));

	assets::RecipeDef woodRecipe;
	woodRecipe.defName = "WoodRecipe";
	woodRecipe.label = "Wood Recipe";
	woodRecipe.stationDefName = "";
	woodRecipe.workAmount = 100.0F;
	woodRecipe.inputs.push_back(assets::RecipeInput{.defName = "Wood", .defNameId = 0, .count = 1});
	assets::RecipeRegistry::Get().registerTestRecipe(woodRecipe);

	// A colonist who KNOWS a TestTree (so the input CAN resolve to a Harvest).
	auto	 colonist = world->createEntity();
	auto&	 mem = world->addComponent<Memory>(colonist);
	uint32_t treeId = assetReg.getDefNameId("TestTree");
	mem.rememberWorldEntity({2.0F, 2.0F}, treeId, assetReg.getCapabilityMask(treeId));

	// A SECOND, unrelated crafting station holding leftover Wood in its store. This is the trap:
	// it is an Inventory-bearing entity, but it is not a colonist, so its store is not deliverable.
	auto  otherStation = createCraftingStation({50.0F, 50.0F}, ""); // no job
	auto& otherStore = world->addComponent<Inventory>(otherStation, Inventory::createForStorage());
	otherStore.addItem("Wood", 5);

	// The station that actually needs Wood.
	createCraftingStation({0.0F, 0.0F}, "WoodRecipe");
	auto& registry = GoalTaskRegistry::Get();

	runUpdates(60);

	EXPECT_EQ(registry.goalCount(TaskType::Craft), 1U) << "one craft goal for the working station";
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 1U)
	    << "input resolves to Harvest despite leftover Wood in another station's store";
	EXPECT_EQ(registry.goalCount(TaskType::Haul), 0U)
	    << "no fetch Haul: the other station's store is not carried/deliverable stock";
}

// With no colonist aware of any source, a craft input resolves to a NoSource Haul ("none found"),
// not a speculative workable goal.
TEST_F(MultiSystemGoalTest, UnknownSourceInputBecomesNoSource) {
	auto& assetReg = assets::AssetRegistry::Get();

	assets::AssetDefinition woodDef;
	woodDef.defName = "Wood";
	woodDef.label = "Wood";
	woodDef.category = assets::ItemCategory::RawMaterial;
	woodDef.itemProperties = assets::ItemProperties{};
	woodDef.itemProperties->stackSize = 40;
	assetReg.registerTestDefinition(std::move(woodDef));

	assets::AssetDefinition treeDef;
	treeDef.defName = "TestTree";
	treeDef.label = "Test Tree";
	treeDef.capabilities.harvestable = assets::HarvestableCapability{};
	treeDef.capabilities.harvestable->yieldDefName = "Wood";
	assetReg.registerTestDefinition(std::move(treeDef));

	assets::RecipeDef woodRecipe;
	woodRecipe.defName = "WoodRecipe";
	woodRecipe.label = "Wood Recipe";
	woodRecipe.stationDefName = "";
	woodRecipe.workAmount = 100.0F;
	woodRecipe.inputs.push_back(assets::RecipeInput{.defName = "Wood", .defNameId = 0, .count = 1});
	assets::RecipeRegistry::Get().registerTestRecipe(woodRecipe);

	auto  station = createCraftingStation({0.0F, 0.0F}, "WoodRecipe");
	auto& registry = GoalTaskRegistry::Get();

	runUpdates(60);

	// Nobody knows a tree or stock, so the input waits as one NoSource Haul.
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U) << "no harvestable known -> no Harvest goal";
	ASSERT_EQ(registry.goalCount(TaskType::Haul), 1U) << "the input is one Haul need";
	const auto* craft = registry.getGoalByDestination(station);
	ASSERT_NE(craft, nullptr);
	const GoalTask* need = nullptr;
	for (const auto* child : registry.getChildGoals(craft->id)) {
		if (child->type == TaskType::Haul) {
			need = child;
			break;
		}
	}
	ASSERT_NE(need, nullptr);
	EXPECT_EQ(need->status, GoalStatus::NoSource) << "waiting: none found";
}

// A NoSource need flips to a Harvest the moment a colonist discovers a harvestable source.
TEST_F(MultiSystemGoalTest, NoSourceReresolvesToHarvestWhenSourceDiscovered) {
	auto& assetReg = assets::AssetRegistry::Get();

	assets::AssetDefinition woodDef;
	woodDef.defName = "Wood";
	woodDef.label = "Wood";
	woodDef.category = assets::ItemCategory::RawMaterial;
	woodDef.itemProperties = assets::ItemProperties{};
	woodDef.itemProperties->stackSize = 40;
	assetReg.registerTestDefinition(std::move(woodDef));

	assets::AssetDefinition treeDef;
	treeDef.defName = "TestTree";
	treeDef.label = "Test Tree";
	treeDef.capabilities.harvestable = assets::HarvestableCapability{};
	treeDef.capabilities.harvestable->yieldDefName = "Wood";
	assetReg.registerTestDefinition(std::move(treeDef));

	assets::RecipeDef woodRecipe;
	woodRecipe.defName = "WoodRecipe";
	woodRecipe.label = "Wood Recipe";
	woodRecipe.stationDefName = "";
	woodRecipe.workAmount = 100.0F;
	woodRecipe.inputs.push_back(assets::RecipeInput{.defName = "Wood", .defNameId = 0, .count = 1});
	assets::RecipeRegistry::Get().registerTestRecipe(woodRecipe);

	auto  station = createCraftingStation({0.0F, 0.0F}, "WoodRecipe");
	auto& registry = GoalTaskRegistry::Get();

	runUpdates(60);
	ASSERT_EQ(registry.goalCount(TaskType::Haul), 1U) << "starts as a NoSource Haul need";
	ASSERT_EQ(registry.goalCount(TaskType::Harvest), 0U);

	// A colonist discovers a tree.
	auto  colonist = world->createEntity();
	auto& mem = world->addComponent<Memory>(colonist);
	uint32_t treeId = assetReg.getDefNameId("TestTree");
	mem.rememberWorldEntity({2.0F, 2.0F}, treeId, assetReg.getCapabilityMask(treeId));

	runUpdates(60); // re-resolution pass

	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 1U) << "NoSource need re-resolved to a Harvest";
	EXPECT_EQ(registry.goalCount(TaskType::Haul), 0U) << "the NoSource Haul was swapped out";
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
	auto& assetReg = assets::AssetRegistry::Get();

	// A second material so recipe B's inputs differ from recipe A's.
	assets::AssetDefinition fiberDef;
	fiberDef.defName = "PlantFiber";
	fiberDef.label = "Plant Fiber";
	fiberDef.category = assets::ItemCategory::RawMaterial;
	fiberDef.itemProperties = assets::ItemProperties{};
	fiberDef.itemProperties->stackSize = 10;
	assetReg.registerTestDefinition(std::move(fiberDef));

	assets::RecipeDef recipeA;
	recipeA.defName = "RecipeA";
	recipeA.label = "Recipe A";
	recipeA.stationDefName = "";
	recipeA.workAmount = 100.0F;
	recipeA.inputs.push_back(assets::RecipeInput{.defName = "Stick", .defNameId = 0, .count = 2});
	assets::RecipeRegistry::Get().registerTestRecipe(recipeA);

	assets::RecipeDef recipeB;
	recipeB.defName = "RecipeB";
	recipeB.label = "Recipe B";
	recipeB.stationDefName = "";
	recipeB.workAmount = 100.0F;
	recipeB.inputs.push_back(assets::RecipeInput{.defName = "PlantFiber", .defNameId = 0, .count = 3});
	assets::RecipeRegistry::Get().registerTestRecipe(recipeB);

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

	// The need is recipe B's (3 PlantFiber), and the craft is Blocked again.
	EXPECT_EQ(registry.remainingNeed(*world, craftB->id, fiberId, NeedBasis::Stock), 3U);
	EXPECT_EQ(craftB->status, GoalStatus::Blocked);
}

TEST_F(MultiSystemGoalTest, GoalOwnershipIsSetCorrectly) {
	// Create crafting station with recipe that has inputs (creates child goals)
	assets::RecipeDef recipeWithInputs;
	recipeWithInputs.defName = "OwnershipTestRecipe";
	recipeWithInputs.label = "Ownership Test Recipe";
	recipeWithInputs.stationDefName = "";
	recipeWithInputs.workAmount = 100.0F;
	recipeWithInputs.inputs.push_back(assets::RecipeInput{
	    .defName = "Stick",
	    .defNameId = 0,
	    .count = 1});
	assets::RecipeRegistry::Get().registerTestRecipe(recipeWithInputs);

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
// Storage stocking: an unfilled minAmount drives chopping (Task B)
// =============================================================================

namespace {
	// Register Wood + a TestTree that yields Wood. Returns the Wood defNameId.
	uint32_t registerWoodAndTree() {
		auto& assetReg = assets::AssetRegistry::Get();

		assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.category = assets::ItemCategory::RawMaterial;
		woodDef.capabilities.carryable = assets::CarryableCapability{6};
		woodDef.itemProperties = assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 1.0F;
		assetReg.registerTestDefinition(std::move(woodDef));

		assets::AssetDefinition treeDef;
		treeDef.defName = "TestTree";
		treeDef.label = "Test Tree";
		treeDef.capabilities.harvestable = assets::HarvestableCapability{};
		treeDef.capabilities.harvestable->yieldDefName = "Wood";
		assetReg.registerTestDefinition(std::move(treeDef));

		return assetReg.getDefNameId("Wood");
	}

	// A colonist who remembers a TestTree at the given spot (so a Wood source is "known").
	void giveColonistKnownTree(World& world, glm::vec2 treePos) {
		auto&	 assetReg = assets::AssetRegistry::Get();
		auto	 colonist = world.createEntity();
		auto&	 mem = world.addComponent<Memory>(colonist);
		uint32_t treeId = assetReg.getDefNameId("TestTree");
		mem.rememberWorldEntity(treePos, treeId, assetReg.getCapabilityMask(treeId));
	}

	// A storage with a SPECIFIC-item rule: keep at least `minAmount` of `defName`.
	EntityID createStorageWithMin(World& world, glm::vec2 pos, const std::string& defName,
								  assets::ItemCategory category, uint32_t minAmount) {
		auto entity = world.createEntity();
		world.addComponent<Position>(entity, Position{pos});
		world.addComponent<Inventory>(entity, Inventory::createForStorage());
		auto& config = world.addComponent<StorageConfiguration>(entity);
		config.addRule(StorageRule{
		    .defName = defName,
		    .category = category,
		    .priority = StoragePriority::Medium,
		    .minAmount = minAmount,
		    .maxAmount = 0});
		return entity;
	}
} // namespace

// A specific-item rule whose minAmount exceeds the box's current count emits a stocking Harvest
// (yield = that item) when a colonist knows a harvestable source. Net chain start: shortfall -> chop.
TEST_F(MultiSystemGoalTest, SpecificItemMinAmountDrivesHarvestWhenSourceKnown) {
	registerWoodAndTree();
	giveColonistKnownTree(*world, {5.0F, 5.0F});
	auto storage = createStorageWithMin(*world, {0.0F, 0.0F}, "Wood", assets::ItemCategory::RawMaterial, 10);

	runUpdates(60);

	auto&		registry = GoalTaskRegistry::Get();
	const auto* umbrella = registry.getGoalByDestination(storage);
	ASSERT_NE(umbrella, nullptr);
	EXPECT_EQ(umbrella->type, TaskType::Haul) << "storage umbrella is its Haul goal";

	// Exactly one stocking Harvest child for Wood, owned by StorageGoalSystem. It only marks that
	// cutting is open; the umbrella's remaining need toward the minimum says how much.
	auto children = registry.getChildGoals(umbrella->id);
	int	 harvestChildren = 0;
	for (const auto* c : children) {
		if (c->type == TaskType::Harvest) {
			harvestChildren++;
			EXPECT_EQ(c->owner, GoalOwner::StorageGoalSystem);
			EXPECT_EQ(c->yieldDefNameId, assets::AssetRegistry::Get().getDefNameId("Wood"));
		}
	}
	EXPECT_EQ(harvestChildren, 1) << "one stocking Harvest for the Wood shortfall";
	EXPECT_EQ(registry.remainingNeed(*world, umbrella->id, assets::AssetRegistry::Get().getDefNameId("Wood"), NeedBasis::Harvest), 10U);
}

// No harvestable source known -> no stocking Harvest. The box just waits for items to be hauled in
// (it still has its normal slot-based Haul umbrella), exactly as before.
TEST_F(MultiSystemGoalTest, MinAmountDoesNotDriveHarvestWithoutKnownSource) {
	registerWoodAndTree();
	// No colonist memory of any tree.
	auto storage = createStorageWithMin(*world, {0.0F, 0.0F}, "Wood", assets::ItemCategory::RawMaterial, 10);

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U)
	    << "no known Wood source -> no stocking harvest";
	EXPECT_GE(registry.goalCount(GoalOwner::StorageGoalSystem), 1U) << "umbrella Haul still exists";
}

// A category WILDCARD rule must NOT drive harvest even with minAmount set: wildcards only accept
// hauled items. (StorageRule.minAmount on a wildcard has no per-item meaning.)
TEST_F(MultiSystemGoalTest, WildcardRuleDoesNotDriveHarvest) {
	registerWoodAndTree();
	giveColonistKnownTree(*world, {5.0F, 5.0F});

	auto  storage = world->createEntity();
	world->addComponent<Position>(storage, Position{{0.0F, 0.0F}});
	world->addComponent<Inventory>(storage, Inventory::createForStorage());
	auto& config = world->addComponent<StorageConfiguration>(storage);
	config.addRule(StorageRule{
	    .defName = "*",
	    .category = assets::ItemCategory::RawMaterial,
	    .priority = StoragePriority::Medium,
	    .minAmount = 10, // inert on a wildcard
	    .maxAmount = 0});

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U)
	    << "a category wildcard never drives harvest, even with minAmount";
}

// Once the box already holds at least minAmount, no stocking Harvest is emitted.
TEST_F(MultiSystemGoalTest, MetMinimumEmitsNoHarvest) {
	registerWoodAndTree();
	giveColonistKnownTree(*world, {5.0F, 5.0F});
	auto storage = createStorageWithMin(*world, {0.0F, 0.0F}, "Wood", assets::ItemCategory::RawMaterial, 10);
	// Pre-stock the box to its minimum.
	world->getComponent<Inventory>(storage)->addItem("Wood", 10);

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U) << "minimum already met -> no chopping";
}

// Retire (a): an existing stocking Harvest is REMOVED once the box reaches minAmount by other means
// (e.g. material hauled in), and is not re-emitted. Without retirement the stale Harvest would
// persist and the AI would keep re-selecting it -- a job that never reaches a terminal state.
TEST_F(MultiSystemGoalTest, StockingHarvestRetiresWhenBoxReachesMinimum) {
	registerWoodAndTree();
	giveColonistKnownTree(*world, {5.0F, 5.0F});
	auto storage = createStorageWithMin(*world, {0.0F, 0.0F}, "Wood", assets::ItemCategory::RawMaterial, 10);

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	ASSERT_EQ(registry.goalCount(TaskType::Harvest), 1U) << "shortfall drives a stocking Harvest";

	// The box fills to its minimum by other means (a hauled-in delivery, simulated directly).
	world->getComponent<Inventory>(storage)->addItem("Wood", 10);

	runUpdates(60); // reconcile pass sees the met minimum

	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U)
	    << "stocking Harvest retired once the box holds >= minAmount";

	// Stays retired across further ticks (no re-emit).
	runUpdates(120);
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U) << "retired Harvest is not re-emitted";
}

// Retire (b): an existing stocking Harvest is REMOVED when no colony-known harvestable source for
// its yield remains (the source vanished / was forgotten), and is not re-emitted.
// A box that fills up keeps its umbrella: goods on their way toward it keep counting, and its id is the
// same once room returns.
TEST_F(MultiSystemGoalTest, AFullBoxKeepsItsUmbrella) {
	auto& registry = GoalTaskRegistry::Get();
	auto  box = world->createEntity();
	world->addComponent<Position>(box, Position{{0.0F, 0.0F}});
	auto store = Inventory::createForStorage();
	store.maxCapacity = 1;
	world->addComponent<Inventory>(box, std::move(store));
	StorageConfiguration config;
	config.addRule(StorageRule{.defName = "Stick", .category = assets::ItemCategory::RawMaterial});
	world->addComponent<StorageConfiguration>(box, std::move(config));

	runUpdates(60);
	const GoalTask* umbrella = registry.getGoalByDestination(box);
	ASSERT_NE(umbrella, nullptr);
	const uint64_t umbrellaId = umbrella->id;

	auto& inventory = *world->getComponent<Inventory>(box);
	inventory.addItem("Stick", inventory.addableCount("Stick"));
	ASSERT_EQ(inventory.addableCount("Stick"), 0U);
	runUpdates(60);
	ASSERT_NE(registry.getGoalByDestination(box), nullptr) << "full, it keeps its umbrella";
	EXPECT_EQ(registry.getGoalByDestination(box)->id, umbrellaId);
	EXPECT_EQ(registry.remainingNeed(*world, umbrellaId, assets::AssetRegistry::Get().getDefNameId("Stick"), NeedBasis::Stock), 0U)
		<< "and wants nothing";

	inventory.removeItem("Stick", 5);
	runUpdates(60);
	ASSERT_NE(registry.getGoalByDestination(box), nullptr);
	EXPECT_EQ(registry.getGoalByDestination(box)->id, umbrellaId) << "the same root once room returns";
}

TEST_F(MultiSystemGoalTest, StockingHarvestRetiresWhenSourceVanishes) {
	registerWoodAndTree();

	// Inline the colonist so we can vanish its knowledge of the tree later.
	auto&	 assetReg = assets::AssetRegistry::Get();
	auto	 colonist = world->createEntity();
	auto&	 mem = world->addComponent<Memory>(colonist);
	uint32_t treeId = assetReg.getDefNameId("TestTree");
	mem.rememberWorldEntity({5.0F, 5.0F}, treeId, assetReg.getCapabilityMask(treeId));

	createStorageWithMin(*world, {0.0F, 0.0F}, "Wood", assets::ItemCategory::RawMaterial, 10);

	runUpdates(60);

	auto& registry = GoalTaskRegistry::Get();
	ASSERT_EQ(registry.goalCount(TaskType::Harvest), 1U) << "a known source drives a stocking Harvest";

	// The only colony source for Wood disappears (tree felled / forgotten).
	world->getComponent<Memory>(colonist)->clear();
	ASSERT_FALSE(colonyKnowsHarvestableSource(world.get(), assetReg, assetReg.getDefNameId("Wood")))
	    << "no colony-known Wood source remains";

	runUpdates(60); // reconcile pass sees the vanished source

	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U)
	    << "stocking Harvest retired when its source is no longer known";

	runUpdates(120);
	EXPECT_EQ(registry.goalCount(TaskType::Harvest), 0U) << "retired Harvest is not re-emitted";
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
		craft.status = GoalStatus::Blocked;
		uint64_t craftId = registry.createGoal(std::move(craft));

		GoalTask harvest;
		harvest.type = TaskType::Harvest;
		harvest.owner = GoalOwner::CraftingGoalSystem;
		harvest.destinationEntity = station;
		harvest.parentGoalId = craftId;
		harvest.status = GoalStatus::Available;
		uint64_t harvestId = registry.createGoal(std::move(harvest));

		GoalTask haul;
		haul.type = TaskType::Haul;
		haul.owner = GoalOwner::CraftingGoalSystem;
		haul.destinationEntity = station;
		haul.parentGoalId = craftId;
		haul.status = GoalStatus::Available;
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
	assets::RecipeDef recipe;
	recipe.defName = "ReapRecipe";
	recipe.label = "Reap Recipe";
	recipe.stationDefName = "";
	recipe.workAmount = 100.0F;
	assets::AssetRegistry::Get().clearDefinitions();
	assets::AssetDefinition stickDef;
	stickDef.defName = "Stick";
	stickDef.label = "Stick";
	stickDef.category = assets::ItemCategory::RawMaterial;
	stickDef.itemProperties = assets::ItemProperties{};
	stickDef.itemProperties->stackSize = 10;
	assets::AssetRegistry::Get().registerTestDefinition(std::move(stickDef));
	recipe.inputs.push_back(assets::RecipeInput{.defName = "Stick", .defNameId = 0, .count = 1});
	assets::RecipeRegistry::Get().registerTestRecipe(recipe);

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

	assets::AssetRegistry::Get().clearDefinitions();
}

} // namespace ecs::test
