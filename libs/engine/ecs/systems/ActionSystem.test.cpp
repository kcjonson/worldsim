// Tests for ActionSystem - Colonist need fulfillment actions
// Tests action factory methods, state machine progression, need restoration, and side effects.

#include "ActionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "TimeSystem.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Packaged.h"
#include "../components/ResourceStack.h"
#include "../components/Skills.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include "assets/AssetRegistry.h"
#include "assets/RecipeDef.h"
#include "assets/RecipeRegistry.h"

#include <gtest/gtest.h>

namespace ecs::test {

// =============================================================================
// harvestWorkRate (pure helper)
// =============================================================================
// Harvest time is work-based: chop seconds = harvestable durability / harvestWorkRate(skill).
// Mirrors constructionWorkRate, but driven by the Harvesting skill.

TEST(HarvestWorkRateTest, UntrainedForagerWorksAtBaseRate) {
	// Level-0 chops at the base rate (not zero), so a durability-40 bush takes ~4s.
	EXPECT_FLOAT_EQ(harvestWorkRate(0.0F), 10.0F);
}

TEST(HarvestWorkRateTest, RateScalesWithSkill) {
	// Higher Harvesting skill chops faster: base * (1 + 0.08 * level).
	EXPECT_FLOAT_EQ(harvestWorkRate(10.0F), 10.0F * 1.8F);
	EXPECT_FLOAT_EQ(harvestWorkRate(20.0F), 10.0F * 2.6F);
}

TEST(HarvestWorkRateTest, SkillClampedToValidRange) {
	EXPECT_FLOAT_EQ(harvestWorkRate(-5.0F), harvestWorkRate(0.0F));
	EXPECT_FLOAT_EQ(harvestWorkRate(99.0F), harvestWorkRate(20.0F));
}

// =============================================================================
// Action Factory Tests (Unit tests for Action struct)
// =============================================================================

TEST(ActionFactoryTest, EatActionCreation) {
	auto action = Action::Eat("Berry", 0.5F);

	EXPECT_EQ(action.type, ActionType::Eat);
	EXPECT_EQ(action.state, ActionState::Starting);
	EXPECT_FLOAT_EQ(action.duration, 2.0F);
	EXPECT_FLOAT_EQ(action.elapsed, 0.0F);

	// Check variant-based effect - Eat now uses ConsumptionEffect
	ASSERT_TRUE(action.hasConsumptionEffect());
	const auto& effect = action.consumptionEffect();
	EXPECT_EQ(effect.itemDefName, "Berry");
	EXPECT_EQ(effect.need, NeedType::Hunger);
	EXPECT_FLOAT_EQ(effect.restoreAmount, 50.0F); // 0.5 * 100
}

TEST(ActionFactoryTest, DrinkActionCreation) {
	auto action = Action::Drink();

	EXPECT_EQ(action.type, ActionType::Drink);
	EXPECT_EQ(action.state, ActionState::Starting);
	EXPECT_FLOAT_EQ(action.duration, 1.5F);

	// Check variant-based effect with side effects
	ASSERT_TRUE(action.hasNeedEffect());
	const auto& effect = action.needEffect();
	EXPECT_EQ(effect.need, NeedType::Thirst);
	EXPECT_FLOAT_EQ(effect.restoreAmount, 100.0F); // Full thirst restoration from water tiles
	EXPECT_EQ(effect.sideEffectNeed, NeedType::Bladder);
	EXPECT_FLOAT_EQ(effect.sideEffectAmount, -15.0F); // negative = drains bladder
}

TEST(ActionFactoryTest, SleepActionCreation) {
	auto action = Action::Sleep(0.5F); // Ground quality

	EXPECT_EQ(action.type, ActionType::Sleep);
	EXPECT_EQ(action.state, ActionState::Starting);
	EXPECT_FLOAT_EQ(action.duration, 8.0F);

	// Check variant-based effect
	ASSERT_TRUE(action.hasNeedEffect());
	const auto& effect = action.needEffect();
	EXPECT_EQ(effect.need, NeedType::Energy);
	EXPECT_FLOAT_EQ(effect.restoreAmount, 30.0F); // 60% * 0.5 quality
}

TEST(ActionFactoryTest, ToiletActionCreation_PeeOnly) {
	glm::vec2 spawnPos{5.0F, 10.0F};
	auto action = Action::Toilet(spawnPos, true, false); // Pee only

	EXPECT_EQ(action.type, ActionType::Toilet);
	EXPECT_EQ(action.state, ActionState::Starting);
	EXPECT_FLOAT_EQ(action.duration, 2.0F); // Pee only is quick
	EXPECT_FLOAT_EQ(action.targetPosition.x, 5.0F);
	EXPECT_FLOAT_EQ(action.targetPosition.y, 10.0F);
	EXPECT_FALSE(action.spawnBioPile); // Pee doesn't create bio pile

	ASSERT_TRUE(action.hasNeedEffect());
	const auto& effect = action.needEffect();
	EXPECT_EQ(effect.need, NeedType::Bladder);
	EXPECT_FLOAT_EQ(effect.restoreAmount, 100.0F);
}

TEST(ActionFactoryTest, ToiletActionCreation_PoopOnly) {
	glm::vec2 spawnPos{5.0F, 10.0F};
	auto action = Action::Toilet(spawnPos, false, true); // Poop only

	EXPECT_EQ(action.type, ActionType::Toilet);
	EXPECT_FLOAT_EQ(action.duration, 4.0F); // Poop takes longer
	EXPECT_TRUE(action.spawnBioPile); // Poop creates bio pile

	ASSERT_TRUE(action.hasNeedEffect());
	const auto& effect = action.needEffect();
	EXPECT_EQ(effect.need, NeedType::Digestion);
	EXPECT_FLOAT_EQ(effect.restoreAmount, 100.0F);
}

TEST(ActionFactoryTest, ToiletActionCreation_Both) {
	glm::vec2 spawnPos{5.0F, 10.0F};
	auto action = Action::Toilet(spawnPos, true, true); // Both

	EXPECT_EQ(action.type, ActionType::Toilet);
	EXPECT_FLOAT_EQ(action.duration, 5.0F); // Both takes longest
	EXPECT_TRUE(action.spawnBioPile); // Poop creates bio pile

	ASSERT_TRUE(action.hasNeedEffect());
	const auto& effect = action.needEffect();
	EXPECT_EQ(effect.need, NeedType::Bladder); // Primary is bladder
	EXPECT_FLOAT_EQ(effect.restoreAmount, 100.0F);
	EXPECT_EQ(effect.sideEffectNeed, NeedType::Digestion); // Side effect is digestion
	EXPECT_FLOAT_EQ(effect.sideEffectAmount, 100.0F); // Full relief
}

TEST(ActionFactoryTest, ActionClear) {
	auto action = Action::Eat("Berry", 0.8F);
	action.elapsed = 1.5F;
	action.state = ActionState::InProgress;

	action.clear();

	EXPECT_EQ(action.type, ActionType::None);
	EXPECT_EQ(action.state, ActionState::Starting);
	EXPECT_FLOAT_EQ(action.duration, 0.0F);
	EXPECT_FLOAT_EQ(action.elapsed, 0.0F);
	EXPECT_FALSE(action.hasConsumptionEffect()); // Effect variant reset to monostate
}

TEST(ActionFactoryTest, ActionIsActive) {
	Action action{};
	EXPECT_FALSE(action.isActive());

	action = Action::Eat("Berry", 0.5F);
	EXPECT_TRUE(action.isActive());
}

TEST(ActionFactoryTest, ActionProgress) {
	auto action = Action::Eat("Berry", 0.5F);
	EXPECT_FLOAT_EQ(action.progress(), 0.0F);

	action.elapsed = 1.0F;
	EXPECT_FLOAT_EQ(action.progress(), 0.5F); // 1.0 / 2.0

	action.elapsed = 2.0F;
	EXPECT_FLOAT_EQ(action.progress(), 1.0F);
}

// =============================================================================
// ActionSystem Tests
// =============================================================================

class ActionSystemTest : public ::testing::Test {
  protected:
	void SetUp() override {
		world = std::make_unique<World>();

		// Register Berry as edible entity for testing (unified entity/item model)
		engine::assets::AssetDefinition berryDef;
		berryDef.defName = "Berry";
		berryDef.label = "Berry";
		berryDef.itemProperties = engine::assets::ItemProperties{};
		berryDef.itemProperties->stackSize = 20;
		berryDef.itemProperties->edible = engine::assets::EdibleCapability{0.3F, engine::assets::CapabilityQuality::Normal, true};
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(berryDef));

		world->registerSystem<ActionSystem>();
	}

	void TearDown() override {
		// Clean up test definitions
		engine::assets::AssetRegistry::Get().clearDefinitions();
		world.reset();
	}

	/// Create a colonist entity with all required components for action processing
	EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{position});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
		world->addComponent<Memory>(entity, Memory{});
		world->addComponent<Inventory>(entity, Inventory::createForColonist());
		world->addComponent<Task>(entity, Task{});
		world->addComponent<Action>(entity, Action{});
		return entity;
	}

	/// Set up a colonist that has arrived at a target for need fulfillment
	void setupArrivedForNeed(EntityID entity, NeedType need, glm::vec2 targetPos = {5.0F, 5.0F}) {
		auto* task = world->getComponent<Task>(entity);
		task->type = TaskType::FulfillNeed;
		task->state = TaskState::Arrived;
		task->needToFulfill = need;
		task->targetPosition = targetPos;
	}

	/// Set a specific need value (0-100)
	void setNeedValue(EntityID entity, NeedType need, float value) {
		auto* needs = world->getComponent<NeedsComponent>(entity);
		ASSERT_NE(needs, nullptr);
		needs->get(need).value = value;
	}

	std::unique_ptr<World> world;
};

TEST_F(ActionSystemTest, DoesNotProcessNonArrivedEntities) {
	auto colonist = createColonist();

	// Task is not in Arrived state
	auto* task = world->getComponent<Task>(colonist);
	task->type = TaskType::FulfillNeed;
	task->state = TaskState::Moving;
	task->needToFulfill = NeedType::Hunger;

	world->update(1.0F);

	auto* action = world->getComponent<Action>(colonist);
	EXPECT_FALSE(action->isActive()); // No action started
}

TEST_F(ActionSystemTest, ClearsWanderTaskOnArrival) {
	auto colonist = createColonist();

	auto* task = world->getComponent<Task>(colonist);
	task->type = TaskType::Wander;
	task->state = TaskState::Arrived;

	world->update(1.0F);

	EXPECT_EQ(task->type, TaskType::None);
	EXPECT_EQ(task->state, TaskState::Pending);
}

TEST_F(ActionSystemTest, StartsEatActionOnArrival) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Hunger);

	// Give the colonist berries in inventory - primary way to eat now
	auto* inventory = world->getComponent<Inventory>(colonist);
	inventory->addItem("Berry", 3);

	world->update(0.1F);

	auto* action = world->getComponent<Action>(colonist);
	EXPECT_TRUE(action->isActive());
	EXPECT_EQ(action->type, ActionType::Eat);
}

TEST_F(ActionSystemTest, StartsDrinkActionOnArrival) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Thirst);

	world->update(0.1F);

	auto* action = world->getComponent<Action>(colonist);
	EXPECT_TRUE(action->isActive());
	EXPECT_EQ(action->type, ActionType::Drink);
}

TEST_F(ActionSystemTest, StartsSleepActionOnArrival) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Energy);

	world->update(0.1F);

	auto* action = world->getComponent<Action>(colonist);
	EXPECT_TRUE(action->isActive());
	EXPECT_EQ(action->type, ActionType::Sleep);
}

TEST_F(ActionSystemTest, StartsToiletActionOnArrival) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Bladder);

	world->update(0.1F);

	auto* action = world->getComponent<Action>(colonist);
	EXPECT_TRUE(action->isActive());
	EXPECT_EQ(action->type, ActionType::Toilet);
}

TEST_F(ActionSystemTest, ActionProgressesOverTime) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Thirst);

	// Start the action
	world->update(0.1F);

	auto* action = world->getComponent<Action>(colonist);
	EXPECT_EQ(action->state, ActionState::InProgress);
	float initialElapsed = action->elapsed;

	// Progress the action
	world->update(0.5F);

	EXPECT_GT(action->elapsed, initialElapsed);
}

TEST_F(ActionSystemTest, ActionCompletesAfterDuration) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Thirst);
	setNeedValue(colonist, NeedType::Thirst, 50.0F);

	// Start and complete the action (Drink is 1.5s)
	world->update(0.1F); // Start
	world->update(1.5F); // Complete

	auto* action = world->getComponent<Action>(colonist);
	auto* task = world->getComponent<Task>(colonist);

	// Action should be cleared after completion
	EXPECT_FALSE(action->isActive());
	EXPECT_EQ(task->type, TaskType::None);
}

TEST_F(ActionSystemTest, NeedRestoredOnCompletion) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Thirst);
	setNeedValue(colonist, NeedType::Thirst, 50.0F);

	// Complete the drink action
	world->update(0.1F);
	world->update(1.5F);

	auto* needs = world->getComponent<NeedsComponent>(colonist);
	// Thirst should be fully restored (water tiles are inexhaustible)
	EXPECT_GT(needs->thirst().value, 50.0F);
	EXPECT_FLOAT_EQ(needs->thirst().value, 100.0F); // Full restoration
}

TEST_F(ActionSystemTest, DrinkFillsBladder) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Thirst);
	setNeedValue(colonist, NeedType::Thirst, 50.0F);
	setNeedValue(colonist, NeedType::Bladder, 80.0F);

	// Complete the drink action
	world->update(0.1F);
	world->update(1.5F);

	auto* needs = world->getComponent<NeedsComponent>(colonist);
	// Bladder should decrease by 15% (side effect)
	EXPECT_LT(needs->bladder().value, 80.0F);
	EXPECT_FLOAT_EQ(needs->bladder().value, 65.0F); // 80 - 15
}

TEST_F(ActionSystemTest, BladderDoesNotGoBelowZero) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Thirst);
	setNeedValue(colonist, NeedType::Bladder, 5.0F); // Low bladder

	// Complete the drink action
	world->update(0.1F);
	world->update(1.5F);

	auto* needs = world->getComponent<NeedsComponent>(colonist);
	// Bladder should clamp to 0, not go negative
	EXPECT_GE(needs->bladder().value, 0.0F);
}

TEST_F(ActionSystemTest, ToiletFullyRelievesBladder) {
	auto colonist = createColonist();
	setupArrivedForNeed(colonist, NeedType::Bladder);
	setNeedValue(colonist, NeedType::Bladder, 20.0F); // Low bladder

	// Complete the toilet action (3s)
	world->update(0.1F);
	world->update(3.0F);

	auto* needs = world->getComponent<NeedsComponent>(colonist);
	// Bladder should be fully restored
	EXPECT_FLOAT_EQ(needs->bladder().value, 100.0F);
}

TEST_F(ActionSystemTest, SleepRestorationAffectedByQuality) {
	// Ground sleep (quality 0.5) - colonist sleeping at current position
	auto colonist = createColonist({0.0F, 0.0F});
	auto* task = world->getComponent<Task>(colonist);
	task->type = TaskType::FulfillNeed;
	task->state = TaskState::Arrived;
	task->needToFulfill = NeedType::Energy;
	task->targetPosition = {0.0F, 0.0F}; // Same as colonist position = ground sleep

	setNeedValue(colonist, NeedType::Energy, 40.0F);

	// Complete the sleep action (8s)
	world->update(0.1F);
	world->update(8.0F);

	auto* needs = world->getComponent<NeedsComponent>(colonist);
	// Ground sleep restores 30% (60 * 0.5 quality)
	EXPECT_FLOAT_EQ(needs->energy().value, 70.0F); // 40 + 30
}

TEST_F(ActionSystemTest, PoopingSpawnsBioPile) {
	auto colonist = createColonist({0.0F, 0.0F});
	setupArrivedForNeed(colonist, NeedType::Digestion);
	setNeedValue(colonist, NeedType::Digestion, 20.0F); // Low digestion

	// Count entities before (just the colonist)
	int entitiesBefore = 0;
	for (auto [entity, pos] : world->view<Position>()) {
		(void)entity;
		(void)pos;
		++entitiesBefore;
	}
	EXPECT_EQ(entitiesBefore, 1); // Just the colonist

	// Complete the toilet/poop action (4s for poop-only)
	world->update(0.1F);
	world->update(4.0F);

	// Count entities after (colonist + bio pile)
	int entitiesAfter = 0;
	for (auto [entity, pos] : world->view<Position>()) {
		(void)entity;
		(void)pos;
		++entitiesAfter;
	}
	EXPECT_EQ(entitiesAfter, 2); // Colonist + Bio Pile

	// Verify bio pile has correct appearance and position
	bool foundBioPile = false;
	for (auto [entity, pos, appearance] : world->view<Position, Appearance>()) {
		if (appearance.defName == "Misc_BioPile") {
			foundBioPile = true;
			// Bio pile should be at the action's target position (colonist position in this test)
			// The colonist was set up at (0.0, 0.0) which becomes the action's target
			EXPECT_FLOAT_EQ(pos.value.x, 0.0F);
			EXPECT_FLOAT_EQ(pos.value.y, 0.0F);
		}
	}
	EXPECT_TRUE(foundBioPile);
}

TEST_F(ActionSystemTest, PeeingDoesNotSpawnBioPile) {
	auto colonist = createColonist({0.0F, 0.0F});
	setupArrivedForNeed(colonist, NeedType::Bladder);
	setNeedValue(colonist, NeedType::Bladder, 20.0F);	// Low bladder
	setNeedValue(colonist, NeedType::Digestion, 100.0F); // Full digestion (no poop needed)

	// Count entities before
	int entitiesBefore = 0;
	for (auto [entity, pos] : world->view<Position>()) {
		(void)entity;
		(void)pos;
		++entitiesBefore;
	}
	EXPECT_EQ(entitiesBefore, 1);

	// Complete the toilet/pee action (2s for pee-only)
	world->update(0.1F);
	world->update(2.0F);

	// Count entities after (should still be just colonist)
	int entitiesAfter = 0;
	for (auto [entity, pos] : world->view<Position>()) {
		(void)entity;
		(void)pos;
		++entitiesAfter;
	}
	EXPECT_EQ(entitiesAfter, 1); // Just the colonist, no bio pile
}

// =============================================================================
// Goal-driven craft pipeline integration (harvest -> inventory-haul -> craft credit)
// =============================================================================

// Fixture variant that wires the GoalTaskRegistry so we can drive the goal-driven
// harvest/haul completion paths through ActionSystem deterministically. This exercises the
// exact ActionSystem code the runtime app would, without depending on world geography
// (Reed/WoodyBush only spawn in Wetland/Grassland/Forest, not Beach).
class ActionSystemGoalTest : public ::testing::Test {
  protected:
	void SetUp() override {
		GoalTaskRegistry::Get().clear();
		world = std::make_unique<World>();
		world->registerSystem<ActionSystem>();
	}

	void TearDown() override {
		GoalTaskRegistry::Get().clear();
		engine::assets::AssetRegistry::Get().clearDefinitions();
		world.reset();
	}

	EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{position});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
		world->addComponent<Memory>(entity, Memory{});
		world->addComponent<Inventory>(entity, Inventory::createForColonist());
		world->addComponent<Task>(entity, Task{});
		world->addComponent<Action>(entity, Action{});
		return entity;
	}

	std::unique_ptr<World> world;
};

// A completed Harvest credits its harvest goal by the ACTUAL yield (not 1-per-action) and,
// once satisfied, unblocks the dependent Haul. Then an inventory-sourced Haul delivering to
// the crafting station credits the parent Craft goal and unblocks it - while keeping the
// materials in inventory for the subsequent Craft action to consume.
TEST_F(ActionSystemGoalTest, HarvestThenInventoryHaulDeliversMaterialsAndUnblocksCraft) {
	auto&	 registry = GoalTaskRegistry::Get();
	EntityID station = static_cast<EntityID>(777);

	// Craft goal needs 2 Sticks total, born Blocked.
	GoalTask craft;
	craft.type = TaskType::Craft;
	craft.owner = GoalOwner::CraftingGoalSystem;
	craft.destinationEntity = station;
	craft.destinationPosition = {5.0F, 0.0F};
	craft.targetAmount = 2;
	craft.status = GoalStatus::Blocked;
	uint64_t craftId = registry.createGoal(std::move(craft));

	// Child Harvest (yields 2 Sticks) and child Haul (depends on the harvest).
	GoalTask harvest;
	harvest.type = TaskType::Harvest;
	harvest.owner = GoalOwner::CraftingGoalSystem;
	harvest.destinationEntity = station;
	harvest.targetAmount = 2;
	harvest.parentGoalId = craftId;
	harvest.status = GoalStatus::Available;
	uint64_t harvestId = registry.createGoal(std::move(harvest));

	GoalTask haul;
	haul.type = TaskType::Haul;
	haul.owner = GoalOwner::CraftingGoalSystem;
	haul.destinationEntity = station;
	haul.destinationPosition = {5.0F, 0.0F};
	haul.targetAmount = 2;
	haul.parentGoalId = craftId;
	haul.dependsOnGoalId = harvestId;
	haul.status = GoalStatus::WaitingForItems;
	uint64_t haulId = registry.createGoal(std::move(haul));

	// --- Phase 1: drive a Harvest action that yields 2 Sticks ---
	auto colonist = createColonist({1.0F, 1.0F});
	auto* task = world->getComponent<Task>(colonist);
	task->type = TaskType::Harvest;
	task->state = TaskState::Arrived;
	task->harvestGoalId = harvestId;
	task->targetPosition = {1.0F, 1.0F};

	auto* action = world->getComponent<Action>(colonist);
	*action = Action::Harvest("Stick", 2, 4.0F, {1.0F, 1.0F}, "Flora_WoodyBush",
							  /*destructive=*/false, /*regrowthTime=*/0.0F);

	world->update(0.1F); // start
	world->update(5.0F); // complete (duration 4s)

	auto* inventory = world->getComponent<Inventory>(colonist);
	EXPECT_EQ(inventory->getQuantity("Stick"), 2U) << "Harvest yield should be in inventory";
	// Harvest goal credited by the real yield (2), so it completed and was removed; the
	// dependent Haul flipped to Available.
	EXPECT_EQ(registry.getGoal(harvestId), nullptr) << "Completed harvest goal removed";
	ASSERT_NE(registry.getGoal(haulId), nullptr);
	EXPECT_EQ(registry.getGoal(haulId)->status, GoalStatus::Available)
	    << "Dependent Haul unblocked once harvest completed";
	EXPECT_EQ(registry.getGoal(craftId)->deliveredAmount, 0U)
	    << "Harvest must NOT credit the Craft - materials are only in inventory, not at station";

	// --- Phase 2: drive an inventory-sourced Haul delivering to the station ---
	task->clear();
	task->type = TaskType::Haul;
	task->state = TaskState::Arrived;
	task->haulGoalId = haulId;
	task->haulItemDefName = "Stick";
	task->haulQuantity = 2;
	task->haulTargetStorageId = static_cast<uint64_t>(station);
	task->haulTargetPosition = {5.0F, 0.0F};
	task->haulFromInventory = true;
	task->targetPosition = {5.0F, 0.0F};

	action->clear();

	world->update(0.1F); // start the deliver-to-station deposit
	world->update(2.0F); // complete (deposit duration 1s)

	// Materials stay in inventory for the Craft action; the goal hierarchy advanced.
	EXPECT_EQ(inventory->getQuantity("Stick"), 2U)
	    << "Craft-station delivery keeps items in inventory for crafting";
	EXPECT_EQ(registry.getGoal(haulId), nullptr) << "Completed haul goal removed";
	ASSERT_NE(registry.getGoal(craftId), nullptr);
	EXPECT_EQ(registry.getGoal(craftId)->deliveredAmount, 2U)
	    << "Delivery to station credited the parent Craft goal";
	EXPECT_EQ(registry.getGoal(craftId)->status, GoalStatus::Available)
	    << "Craft leaves Blocked once all materials delivered";
}

// =============================================================================
// Tree felling: a two-hand bulk material (Wood) destructive harvest
// =============================================================================
// Felling is one destructive action. The colonist takes a weight-limited armful
// of Wood into the hands (never the backpack), the remainder drops as a loose,
// haulable ground pile via the drop-resource callback, and the tree always falls
// -- even when the colonist's hands were already full, so no wood is lost and no
// stump lingers.

class FellingTest : public ::testing::Test {
  protected:
	void SetUp() override {
		world = std::make_unique<World>();

		// Wood: a two-hand bulk material at 2.5 kg/unit. handsRequired==2 routes it to the
		// hands as an armful; mass drives how much fits under the 35 kg colonist cap (14 units).
		engine::assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = engine::assets::ItemCategory::RawMaterial;
		woodDef.itemProperties = engine::assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 2.5F;
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(woodDef));

		auto& action = world->registerSystem<ActionSystem>();
		action.setDropResourceCallback(
			[this](const std::string& defName, float /*x*/, float /*y*/, uint32_t quantity) {
				++dropCalls;
				lastDropDef = defName;
				lastDropQty = quantity;
			}
		);
		action.setRemoveEntityCallback([this](const std::string& defName, float /*x*/, float /*y*/) {
			++removeCalls;
			lastRemoveDef = defName;
		});
	}

	void TearDown() override {
		engine::assets::AssetRegistry::Get().clearDefinitions();
		world.reset();
	}

	EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{position});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
		world->addComponent<Memory>(entity, Memory{});
		world->addComponent<Inventory>(entity, Inventory::createForColonist());
		world->addComponent<Task>(entity, Task{});
		world->addComponent<Action>(entity, Action{});
		return entity;
	}

	// Drive a pre-built destructive two-hand Harvest of `yield` Wood to completion.
	void fell(EntityID colonist, uint32_t yield) {
		auto* task = world->getComponent<Task>(colonist);
		task->type = TaskType::Harvest;
		task->state = TaskState::Arrived;
		task->targetPosition = {1.0F, 1.0F};
		// harvestGoalId left 0: no goal bookkeeping, just the collection effect.

		auto* action = world->getComponent<Action>(colonist);
		*action = Action::Harvest("Wood", yield, 1.0F, {1.0F, 1.0F}, "Flora_TreeOak",
								  /*destructive=*/true, /*regrowthTime=*/0.0F);

		world->update(0.1F); // start
		world->update(2.0F); // complete (duration 1s)
	}

	std::unique_ptr<World> world;
	int					   dropCalls = 0;
	int					   removeCalls = 0;
	uint32_t			   lastDropQty = 0;
	std::string			   lastDropDef;
	std::string			   lastRemoveDef;
};

// Empty hands, yield 30, cap 35 -> 14 to hands (floor(35/2.5)), 16 dropped, tree removed.
TEST_F(FellingTest, ArmfulToHandsRemainderDroppedTreeRemoved) {
	auto colonist = createColonist({1.0F, 1.0F});
	fell(colonist, 30);

	auto* inventory = world->getComponent<Inventory>(colonist);
	EXPECT_EQ(handHeldQuantity(*inventory, "Wood"), 14U) << "Armful clamped to the 35 kg carry cap";
	EXPECT_EQ(inventory->getQuantity("Wood"), 0U) << "Wood never enters the backpack";

	EXPECT_EQ(dropCalls, 1) << "Remainder dropped exactly once";
	EXPECT_EQ(lastDropDef, "Wood");
	EXPECT_EQ(lastDropQty, 16U) << "30 yield - 14 carried = 16 dropped";

	EXPECT_EQ(removeCalls, 1) << "The tree falls in one action";
	EXPECT_EQ(lastRemoveDef, "Flora_TreeOak");
}

// Hands pre-filled to capacity (added == 0): the whole yield still drops and the
// tree still falls. The destroy is NOT gated on having collected, so no wood is
// lost and no stump remains even when the colonist couldn't carry any.
TEST_F(FellingTest, HandsFullStillDropsWholeYieldAndRemovesTree) {
	auto  colonist = createColonist({1.0F, 1.0F});
	auto* inventory = world->getComponent<Inventory>(colonist);
	// Seat a full 14-unit armful first, mirrored across both hands.
	inventory->leftHand = ItemStack{"Wood", 14};
	inventory->rightHand = ItemStack{"Wood", 14};
	ASSERT_EQ(handHeldQuantity(*inventory, "Wood"), 14U);

	fell(colonist, 30);

	EXPECT_EQ(handHeldQuantity(*inventory, "Wood"), 14U) << "No room for more; armful unchanged";
	EXPECT_EQ(dropCalls, 1) << "Whole yield dropped as a pile";
	EXPECT_EQ(lastDropQty, 30U) << "added == 0, so all 30 drop";
	EXPECT_EQ(removeCalls, 1) << "Tree removed despite full hands";
}

// Regression: a wood-carrying colonist tasked with a PlacePackaged (no empty-hands
// precondition) must not duplicate the armful. clearHandsForTwoHandedPickup once dropped
// each hand independently, so a 14-Wood armful (same stack mirrored across both hands)
// dropped 28 -- and as per-unit packaged items, not a single loose pile. The mirror-aware
// path now drops the armful ONCE, as one haulable resource pile of exactly 14.
TEST_F(FellingTest, PickupPackagedDropsHeldWoodArmfulOnceNotTwice) {
	auto  colonist = createColonist({1.0F, 1.0F});
	auto* inventory = world->getComponent<Inventory>(colonist);
	// Hands hold a 14-unit Wood armful, mirrored across both hands.
	inventory->leftHand = ItemStack{"Wood", 14};
	inventory->rightHand = ItemStack{"Wood", 14};
	ASSERT_EQ(handHeldQuantity(*inventory, "Wood"), 14U);

	// A packaged shelf to pick up at the colonist's position.
	auto packaged = world->createEntity();
	world->addComponent<Position>(packaged, Position{{1.0F, 1.0F}});
	world->addComponent<Appearance>(packaged, Appearance{"BasicShelf", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
	world->addComponent<Packaged>(packaged, Packaged{glm::vec2{5.0F, 5.0F}, false});

	auto* task = world->getComponent<Task>(colonist);
	task->type = TaskType::PlacePackaged;
	task->state = TaskState::Arrived;
	task->placePackagedEntityId = static_cast<uint64_t>(packaged);
	task->placeSourcePosition = {1.0F, 1.0F};
	task->placeTargetPosition = {5.0F, 5.0F};
	task->targetPosition = {1.0F, 1.0F};

	auto* action = world->getComponent<Action>(colonist);
	*action = Action::PickupPackaged(static_cast<uint64_t>(packaged), {1.0F, 1.0F});

	world->update(0.1F); // start
	world->update(2.0F); // complete (PickupPackaged duration 1.5s) -> clears hands, picks up shelf

	// The held Wood dropped exactly once, as a single loose pile of all 14 units.
	EXPECT_EQ(dropCalls, 1) << "Armful dropped once, not once per hand";
	EXPECT_EQ(lastDropDef, "Wood");
	EXPECT_EQ(lastDropQty, 14U) << "Exactly 14 dropped (not 28 from double-clearing the mirror)";

	// Hands now hold the packaged shelf for display, the Wood is gone from them.
	EXPECT_EQ(handHeldQuantity(*inventory, "Wood"), 0U) << "Wood cleared from both hands";
	ASSERT_TRUE(inventory->carryingPackagedEntity.has_value());
	EXPECT_EQ(inventory->carryingPackagedEntity.value(), static_cast<uint64_t>(packaged));
}

// =============================================================================
// Loose ground pile haul: per-entity ResourceStack drains across armfuls
// =============================================================================
// The remainder a fell drops is a single ground entity (Appearance defName == the
// material + a ResourceStack count), spawned WITHOUT Packaged so it is immediately
// haulable. A Pickup lifts a weight-limited armful from the LIVE ResourceStack and
// decrements it; the entity is removed only when the count hits zero. A Pickup's
// CollectionEffect carries destroySource=true, but the loose-pile branch must
// intercept and decrement instead of full-destroying a still-loaded pile.

class LoosePileHaulTest : public ::testing::Test {
  protected:
	void SetUp() override {
		world = std::make_unique<World>();

		// Wood: two-hand bulk material, 2.5 kg/unit. At the 35 kg colonist cap an armful is
		// floor(35/2.5) = 14 units, so a 16-unit pile drains in 14 + 2.
		engine::assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = engine::assets::ItemCategory::RawMaterial;
		woodDef.itemProperties = engine::assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 2.5F;
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(woodDef));

		auto& action = world->registerSystem<ActionSystem>();
		// Mirror GameScene: the remove callback actually destroys the depleted pile entity
		// (Position-matched), so a later Pickup no longer finds it.
		action.setRemoveEntityCallback([this](const std::string& defName, float x, float y) {
			++removeCalls;
			lastRemoveDef = defName;
			constexpr float kEps = 0.25F;
			for (auto [ent, entPos, appearance] : world->view<Position, Appearance>()) {
				if (appearance.defName != defName) {
					continue;
				}
				const float dx = entPos.value.x - x;
				const float dy = entPos.value.y - y;
				if (dx * dx + dy * dy <= kEps * kEps) {
					world->destroyEntity(ent);
					break;
				}
			}
		});
	}

	void TearDown() override {
		engine::assets::AssetRegistry::Get().clearDefinitions();
		world.reset();
	}

	EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{position});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
		world->addComponent<Memory>(entity, Memory{});
		world->addComponent<Inventory>(entity, Inventory::createForColonist());
		world->addComponent<Task>(entity, Task{});
		world->addComponent<Action>(entity, Action{});
		return entity;
	}

	// A loose Wood pile of `count` units at `pos`: Appearance defName == material + a
	// ResourceStack, no Packaged (immediately haulable). Mirrors the drop-resource callback.
	EntityID spawnPile(uint32_t count, glm::vec2 pos) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{pos});
		world->addComponent<Appearance>(entity, Appearance{"Wood", 1.0F, {1.0F, 1.0F, 1.0F, 1.0F}});
		world->addComponent<ResourceStack>(entity, ResourceStack{count});
		return entity;
	}

	// Drive one Pickup of the loose pile at `pilePos` to completion. The Pickup's quantity (1)
	// and destroySource flag are ignored by the loose-pile branch.
	void pickUpPile(EntityID colonist, glm::vec2 pilePos) {
		auto* task = world->getComponent<Task>(colonist);
		task->type = TaskType::Harvest; // generic collection task; no harvest goal bookkeeping
		task->state = TaskState::Arrived;
		task->targetPosition = pilePos;

		auto* action = world->getComponent<Action>(colonist);
		*action = Action::Pickup("Wood", 1, pilePos, "Wood");

		world->update(0.1F); // start
		world->update(1.0F); // complete (Pickup duration 0.5s)
	}

	std::unique_ptr<World> world;
	int					   removeCalls = 0;
	std::string			   lastRemoveDef;
};

// A 16-unit pile, colonist cap 35: the first Pickup lifts 14 (floor(35/2.5)) into the hands and
// the pile drops to 2 with the entity still present; a second Pickup lifts the last 2, the stack
// hits 0, and the pile entity is removed.
TEST_F(LoosePileHaulTest, ArmfulDrainsPileAcrossTwoPickupsThenRemoves) {
	const glm::vec2 pilePos{1.0F, 1.0F};
	EntityID		pile = spawnPile(16, pilePos);
	auto			colonist = createColonist(pilePos);

	// --- First pickup: an armful of 14, pile drops to 2, entity stays ---
	pickUpPile(colonist, pilePos);

	auto* inventory = world->getComponent<Inventory>(colonist);
	EXPECT_EQ(handHeldQuantity(*inventory, "Wood"), 14U) << "Armful clamped to the 35 kg carry cap";
	EXPECT_EQ(inventory->getQuantity("Wood"), 0U) << "Two-hand Wood never enters the backpack";

	ASSERT_TRUE(world->isAlive(pile)) << "Pile remains while it still holds wood";
	auto* stack = world->getComponent<ResourceStack>(pile);
	ASSERT_NE(stack, nullptr);
	EXPECT_EQ(stack->quantity, 2U) << "16 - 14 carried = 2 left in the pile";
	EXPECT_EQ(removeCalls, 0) << "A partially-emptied pile is not removed";

	// Clear the hands so the second pickup can seat a fresh armful.
	inventory->leftHand.reset();
	inventory->rightHand.reset();

	// --- Second pickup: lift the last 2, stack hits 0, entity removed ---
	pickUpPile(colonist, pilePos);

	EXPECT_EQ(handHeldQuantity(*inventory, "Wood"), 2U) << "Last 2 units lifted into the hands";
	EXPECT_EQ(removeCalls, 1) << "Depleted pile fires the remove callback exactly once";
	EXPECT_EQ(lastRemoveDef, "Wood") << "Removal keyed by the material/pile defName";
	EXPECT_FALSE(world->isAlive(pile)) << "Pile entity destroyed once drained to zero";
}

// =============================================================================
// Hand-aware deposit and craft seams for two-hand bulk material (Wood)
// =============================================================================
// Wood rides in BOTH hands as a mirrored armful, never the backpack. A deposit must
// pull from the hands, bounce any storage-full or destroyed-storage remainder back to
// the hands (mirror intact), and credit the haul goal only by what actually landed in
// storage. A craft must consume its Wood input from the hands too.

class HandMaterialActionTest : public ::testing::Test {
  protected:
	void SetUp() override {
		GoalTaskRegistry::Get().clear();
		world = std::make_unique<World>();

		// Wood: two-hand bulk material, 2.5 kg/unit -> an empty colonist's 35 kg cap holds 14.
		engine::assets::AssetDefinition woodDef;
		woodDef.defName = "Wood";
		woodDef.label = "Wood";
		woodDef.handsRequired = 2;
		woodDef.category = engine::assets::ItemCategory::RawMaterial;
		woodDef.itemProperties = engine::assets::ItemProperties{};
		woodDef.itemProperties->stackSize = 40;
		woodDef.itemProperties->massKg = 2.5F;
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(woodDef));

		// Plank: a one-hand craft output so the produced item lands in the backpack.
		engine::assets::AssetDefinition plankDef;
		plankDef.defName = "Plank";
		plankDef.label = "Plank";
		plankDef.handsRequired = 1;
		plankDef.category = engine::assets::ItemCategory::RawMaterial;
		plankDef.itemProperties = engine::assets::ItemProperties{};
		plankDef.itemProperties->stackSize = 40;
		plankDef.itemProperties->massKg = 0.5F;
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(plankDef));

		world->registerSystem<ActionSystem>();
	}

	void TearDown() override {
		GoalTaskRegistry::Get().clear();
		engine::assets::RecipeRegistry::Get().clear();
		engine::assets::AssetRegistry::Get().clearDefinitions();
		world.reset();
	}

	EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{position});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
		world->addComponent<Memory>(entity, Memory{});
		world->addComponent<Inventory>(entity, Inventory::createForColonist());
		world->addComponent<Task>(entity, Task{});
		world->addComponent<Action>(entity, Action{});
		return entity;
	}

	// Seat a mirrored two-hand Wood armful of `count` units in both hands.
	static void seatArmful(Inventory& inventory, uint32_t count) {
		inventory.leftHand = ItemStack{"Wood", count};
		inventory.rightHand = ItemStack{"Wood", count};
	}

	std::unique_ptr<World> world;
};

// 14-Wood armful deposited into storage that can take only 8: 8 land in storage, the
// other 6 bounce back mirrored across both hands, the haul goal is credited 8 (not 14),
// and no wood is lost (8 stored + 6 held == 14).
TEST_F(HandMaterialActionTest, DepositFromHandsBouncesStorageFullRemainderToBothHands) {
	auto&	 registry = GoalTaskRegistry::Get();

	// Storage that accepts exactly 8 Wood: one slot capped at 8.
	auto storage = world->createEntity();
	Inventory storageInv;
	storageInv.maxCapacity = 1;
	storageInv.maxStackSize = 8;
	world->addComponent<Inventory>(storage, storageInv);

	// Haul goal wants 14 (stays open after a partial 8 so we can read the credit).
	GoalTask haul;
	haul.type = TaskType::Haul;
	haul.owner = GoalOwner::CraftingGoalSystem;
	haul.destinationEntity = storage;
	haul.targetAmount = 14;
	haul.status = GoalStatus::Available;
	uint64_t haulId = registry.createGoal(std::move(haul));

	auto  colonist = createColonist({0.0F, 0.0F});
	auto* inventory = world->getComponent<Inventory>(colonist);
	seatArmful(*inventory, 14);

	auto* task = world->getComponent<Task>(colonist);
	task->type = TaskType::Haul;
	task->state = TaskState::Arrived;
	task->haulGoalId = haulId;
	task->targetPosition = {0.0F, 0.0F};

	auto* action = world->getComponent<Action>(colonist);
	*action = Action::Deposit("Wood", 14, static_cast<uint64_t>(storage), {0.0F, 0.0F});

	world->update(0.1F); // start
	world->update(2.0F); // complete (deposit duration 1s)

	auto* storedInv = world->getComponent<Inventory>(storage);
	EXPECT_EQ(storedInv->getQuantity("Wood"), 8U) << "Storage took only what its 8-cap slot allows";

	// The 6-unit remainder bounced back as a mirrored armful.
	ASSERT_TRUE(inventory->leftHand.has_value());
	ASSERT_TRUE(inventory->rightHand.has_value());
	EXPECT_EQ(inventory->leftHand->quantity, 6U);
	EXPECT_EQ(inventory->rightHand->quantity, 6U) << "Remainder stays mirrored across both hands";
	EXPECT_EQ(handHeldQuantity(*inventory, "Wood"), 6U);
	EXPECT_EQ(inventory->getQuantity("Wood"), 0U) << "Two-hand Wood never bounces into the backpack";

	EXPECT_EQ(registry.getGoal(haulId)->deliveredAmount, 8U) << "Goal credited only the 8 that landed";

	EXPECT_EQ(storedInv->getQuantity("Wood") + handHeldQuantity(*inventory, "Wood"), 14U)
	    << "No wood lost: stored + held == the original armful";
}

// Deposit targeting a non-existent storage entity: all 14 bounce back to the hands
// (mirrored), the goal is credited 0, and nothing is lost.
TEST_F(HandMaterialActionTest, DepositFromHandsToMissingStorageReturnsAllToBothHands) {
	auto& registry = GoalTaskRegistry::Get();

	constexpr uint64_t kMissingStorage = 999999ULL;
	ASSERT_FALSE(world->isAlive(static_cast<EntityID>(kMissingStorage)));

	GoalTask haul;
	haul.type = TaskType::Haul;
	haul.owner = GoalOwner::CraftingGoalSystem;
	haul.targetAmount = 14;
	haul.status = GoalStatus::Available;
	uint64_t haulId = registry.createGoal(std::move(haul));

	auto  colonist = createColonist({0.0F, 0.0F});
	auto* inventory = world->getComponent<Inventory>(colonist);
	seatArmful(*inventory, 14);

	auto* task = world->getComponent<Task>(colonist);
	task->type = TaskType::Haul;
	task->state = TaskState::Arrived;
	task->haulGoalId = haulId;
	task->targetPosition = {0.0F, 0.0F};

	auto* action = world->getComponent<Action>(colonist);
	*action = Action::Deposit("Wood", 14, kMissingStorage, {0.0F, 0.0F});

	world->update(0.1F);
	world->update(2.0F);

	ASSERT_TRUE(inventory->leftHand.has_value());
	ASSERT_TRUE(inventory->rightHand.has_value());
	EXPECT_EQ(inventory->leftHand->quantity, 14U);
	EXPECT_EQ(inventory->rightHand->quantity, 14U) << "All 14 return mirrored across both hands";
	EXPECT_EQ(handHeldQuantity(*inventory, "Wood"), 14U) << "Nothing lost when storage is gone";
	EXPECT_EQ(registry.getGoal(haulId)->deliveredAmount, 0U) << "Destroyed storage credits nothing";
}

// A craft whose only input is two-hand Wood consumes it straight from the hands: the
// hands decrement by the input count (mirror stays synced) and the output is produced.
TEST_F(HandMaterialActionTest, CraftConsumesWoodFromHandsAndProducesOutput) {
	engine::assets::RecipeDef recipe;
	recipe.defName = "Recipe_Plank";
	recipe.label = "Plank";
	recipe.stationDefName = "CraftingSpot";
	recipe.workAmount = 50.0F; // -> 0.5s craft
	recipe.inputs.push_back({"Wood", 0, 4});
	recipe.outputs.push_back({"Plank", 0, 1});
	engine::assets::RecipeRegistry::Get().registerTestRecipe(recipe);

	EntityID station = static_cast<EntityID>(555);

	auto  colonist = createColonist({0.0F, 0.0F});
	auto* inventory = world->getComponent<Inventory>(colonist);
	seatArmful(*inventory, 10); // 10 Wood in hands, empty backpack
	ASSERT_EQ(inventory->getQuantity("Wood"), 0U);

	auto* task = world->getComponent<Task>(colonist);
	task->type = TaskType::Craft;
	task->state = TaskState::Arrived;
	task->craftRecipeDefName = "Recipe_Plank";
	task->targetStationId = static_cast<uint64_t>(station);
	task->targetPosition = {0.0F, 0.0F};

	world->update(0.1F); // startCraftAction builds the Craft action (readiness gate sees hand Wood)
	auto* action = world->getComponent<Action>(colonist);
	ASSERT_TRUE(action->hasCraftingEffect()) << "Hand-carried Wood satisfies the craft readiness gate";

	world->update(1.0F); // complete the 0.5s craft

	EXPECT_EQ(handHeldQuantity(*inventory, "Wood"), 6U) << "4 Wood consumed from the hands (10 - 4)";
	ASSERT_TRUE(inventory->leftHand.has_value());
	ASSERT_TRUE(inventory->rightHand.has_value());
	EXPECT_EQ(inventory->leftHand->quantity, inventory->rightHand->quantity) << "Hand mirror stays synced";
	EXPECT_EQ(inventory->getQuantity("Plank"), 1U) << "Craft produced its output";
}

// =============================================================================
// Game-speed scaling (ActionSystem reads TimeSystem::effectiveTimeScale)
// =============================================================================

// Fixture that registers BOTH TimeSystem and ActionSystem so action timing can be
// driven at different game speeds. World::update sorts systems by priority, so
// TimeSystem (10) always runs before ActionSystem (350) regardless of registration
// order - effectiveTimeScale() is up to date when the action system reads it.
class ActionSystemTimeTest : public ::testing::Test {
  protected:
	void SetUp() override {
		world = std::make_unique<World>();

		engine::assets::AssetDefinition berryDef;
		berryDef.defName = "Berry";
		berryDef.label = "Berry";
		berryDef.itemProperties = engine::assets::ItemProperties{};
		berryDef.itemProperties->stackSize = 20;
		berryDef.itemProperties->edible = engine::assets::EdibleCapability{0.3F, engine::assets::CapabilityQuality::Normal, true};
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(berryDef));

		timeSystem = &world->registerSystem<TimeSystem>();
		world->registerSystem<ActionSystem>();
	}

	void TearDown() override {
		engine::assets::AssetRegistry::Get().clearDefinitions();
		world.reset();
	}

	EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{position});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
		world->addComponent<Memory>(entity, Memory{});
		world->addComponent<Inventory>(entity, Inventory::createForColonist());
		world->addComponent<Task>(entity, Task{});
		world->addComponent<Action>(entity, Action{});
		return entity;
	}

	/// A colonist arrived at a berry, hunger pre-set low, ready to start an Eat action.
	EntityID createHungryEater(float hungerValue) {
		auto colonist = createColonist();
		auto* task = world->getComponent<Task>(colonist);
		task->type = TaskType::FulfillNeed;
		task->state = TaskState::Arrived;
		task->needToFulfill = NeedType::Hunger;
		task->targetPosition = {5.0F, 5.0F};
		world->getComponent<Inventory>(colonist)->addItem("Berry", 3);
		world->getComponent<NeedsComponent>(colonist)->get(NeedType::Hunger).value = hungerValue;
		return colonist;
	}

	std::unique_ptr<World> world;
	TimeSystem*			   timeSystem = nullptr;
};

// Paused freezes actions: an Eat starts (the start block isn't gated on dt) but never
// progresses, because scaledDt = deltaTime * effectiveTimeScale() and the pause scale is 0.
// The hunger need stays exactly where it was even after a huge real-time step.
TEST_F(ActionSystemTimeTest, PausedFreezesActionProgress) {
	timeSystem->setSpeed(GameSpeed::Paused);
	ASSERT_FLOAT_EQ(timeSystem->effectiveTimeScale(), 0.0F);

	auto  colonist = createHungryEater(30.0F);
	auto* action = world->getComponent<Action>(colonist);
	auto* needs = world->getComponent<NeedsComponent>(colonist);

	world->update(100.0F); // a giant step that would finish any action at normal speed

	EXPECT_TRUE(action->isActive()) << "Action starts even while paused";
	EXPECT_EQ(action->type, ActionType::Eat);
	EXPECT_FLOAT_EQ(action->elapsed, 0.0F) << "Paused scaledDt is 0, so no progress accrues";
	EXPECT_LT(action->progress(), 1.0F);
	EXPECT_FLOAT_EQ(needs->hunger().value, 30.0F) << "Need not restored - action never completed";
}

// Resuming from pause lets a frozen action progress again.
TEST_F(ActionSystemTimeTest, ResumeUnfreezesActionProgress) {
	timeSystem->setSpeed(GameSpeed::Paused);
	auto  colonist = createHungryEater(30.0F);
	auto* action = world->getComponent<Action>(colonist);

	world->update(1.0F); // starts but frozen
	ASSERT_FLOAT_EQ(action->elapsed, 0.0F);

	timeSystem->setSpeed(GameSpeed::Normal);
	world->update(0.5F);
	EXPECT_GT(action->elapsed, 0.0F) << "Progress resumes once unpaused";
}

// A faster game speed completes the same action in proportionally fewer real seconds.
// Eat is authored at 2.0s of game time; the real seconds needed scale by 1/effectiveTimeScale.
// We assert the boundary on both sides of the threshold rather than a fixed frame count, and
// confirm the faster tier needs strictly less real time than Normal.
TEST_F(ActionSystemTimeTest, FasterSpeedCompletesActionInLessRealTime) {
	constexpr float kEatDuration = 2.0F; // Action::Eat authored duration (game seconds)
	constexpr float kEps = 0.01F;

	// --- Normal (1x): needs ~kEatDuration real seconds ---
	timeSystem->setSpeed(GameSpeed::Normal);
	const float normalScale = timeSystem->effectiveTimeScale();
	ASSERT_GT(normalScale, 0.0F);
	const float normalRealSeconds = kEatDuration / normalScale;

	{
		auto  colonist = createHungryEater(30.0F);
		auto* action = world->getComponent<Action>(colonist);
		world->update(0.0F);							 // start the action (no progress)
		world->update(normalRealSeconds - kEps);		 // just shy of completion
		EXPECT_TRUE(action->isActive()) << "Not done before its (scaled) duration at Normal";
		world->update(2.0F * kEps);						 // cross the threshold
		EXPECT_FALSE(action->isActive()) << "Completed once scaled elapsed reached duration";
	}

	// --- Fastest tier: needs strictly fewer real seconds ---
	timeSystem->setSpeed(GameSpeed::VeryFast);
	const float fastScale = timeSystem->effectiveTimeScale();
	ASSERT_GT(fastScale, normalScale) << "VeryFast must be a higher multiplier than Normal";
	const float fastRealSeconds = kEatDuration / fastScale;
	EXPECT_LT(fastRealSeconds, normalRealSeconds) << "Faster speed => fewer real seconds to finish";

	{
		auto  colonist = createHungryEater(30.0F);
		auto* action = world->getComponent<Action>(colonist);
		world->update(0.0F);						   // start
		world->update(fastRealSeconds - kEps);		   // just shy
		EXPECT_TRUE(action->isActive()) << "Not done before its (scaled) duration at VeryFast";
		world->update(2.0F * kEps);					   // cross
		EXPECT_FALSE(action->isActive()) << "Completed at the faster speed too";
	}
}

// =============================================================================
// Harvest durability -> seconds conversion (driven through ActionSystem's start path)
// =============================================================================

// The conversion `action.duration /= harvestWorkRate(skill)` only runs when a Harvest action
// is freshly STARTED from a task (the `if (!action.isActive())` block). A pre-built
// Action::Harvest has type != None, so isActive() is true and the start block - and the
// divide - is skipped (that's why HarvestThenInventoryHaul, which pre-builds the action,
// doesn't exercise it). To hit the real path we set up a Harvest TASK plus the world memory
// and capability data startHarvestAction needs, then update once and assert the converted
// seconds equal durability / harvestWorkRate(skill).
class ActionSystemHarvestTimeTest : public ::testing::Test {
  protected:
	void SetUp() override {
		world = std::make_unique<World>();

		// Yield item (so getDefNameId("TestWood") resolves and matches the harvestable's yield).
		engine::assets::AssetDefinition wood;
		wood.defName = "TestWood";
		wood.label = "Test Wood";
		wood.itemProperties = engine::assets::ItemProperties{};
		wood.itemProperties->stackSize = 50;
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(wood));

		// Harvestable tree. The capability must be set BEFORE registering so the registry's
		// capability mask (built in registerTestDefinition) marks it Harvestable; otherwise
		// startHarvestAction's hasCapability() gate rejects it. durability 60 with the work
		// rates (10/18 units per second at skill 0/10) gives clean expected seconds: 6.0 and 3.333.
		engine::assets::AssetDefinition tree;
		tree.defName = "Flora_TestTree";
		tree.label = "Test Tree";
		engine::assets::HarvestableCapability cap;
		cap.yieldDefName = "TestWood";
		cap.amountMin = 1; // amountMin == amountMax => deterministic yield, no RNG
		cap.amountMax = 1;
		cap.durability = 60.0F;
		cap.destructive = true;
		cap.regrowthTime = 0.0F;
		cap.requiredToolType = ""; // empty => inventoryHoldsToolType passes for a tool-less colonist
		tree.capabilities.harvestable = cap;
		engine::assets::AssetRegistry::Get().registerTestDefinition(std::move(tree));

		world->registerSystem<ActionSystem>();
	}

	void TearDown() override {
		engine::assets::AssetRegistry::Get().clearDefinitions();
		world.reset();
	}

	/// Create a colonist set up to start harvesting Flora_TestTree at `treePos`, with the
	/// given Harvesting skill. Seeds memory with the tree (startHarvestAction scans memory,
	/// not live world entities) and an Arrived Harvest task targeting it.
	EntityID createHarvester(glm::vec2 treePos, float harvestingSkill) {
		auto& registry = engine::assets::AssetRegistry::Get();

		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{treePos});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
		world->addComponent<Inventory>(entity, Inventory::createForColonist());
		world->addComponent<Action>(entity, Action{});

		Skills skills;
		skills.setLevel("Harvesting", harvestingSkill);
		world->addComponent<Skills>(entity, std::move(skills));

		Memory memory;
		memory.rememberWorldEntity(treePos, "Flora_TestTree"); // string overload interns + masks
		world->addComponent<Memory>(entity, std::move(memory));

		Task task;
		task.type = TaskType::Harvest;
		task.state = TaskState::Arrived;
		task.targetPosition = treePos;
		task.harvestYieldDefNameId = registry.getDefNameId("TestWood");
		world->addComponent<Task>(entity, std::move(task));

		return entity;
	}

	std::unique_ptr<World> world;
};

// Untrained forager: 60 work units / 10 units-per-second = 6.0s.
TEST_F(ActionSystemHarvestTimeTest, ConvertsDurabilityToSecondsAtSkillZero) {
	const glm::vec2 treePos{3.0F, 4.0F};
	auto			harvester = createHarvester(treePos, 0.0F);

	world->update(0.1F); // triggers startHarvestAction + the duration conversion

	auto* action = world->getComponent<Action>(harvester);
	ASSERT_EQ(action->type, ActionType::Harvest) << "Harvest action started from the task";
	EXPECT_NEAR(action->duration, 60.0F / harvestWorkRate(0.0F), 0.001F);
	EXPECT_NEAR(action->duration, 6.0F, 0.001F);
}

// Skilled forager: 60 / harvestWorkRate(10) = 60 / 18 = 3.333s. Proves the divide samples the
// colonist's Harvesting skill, not a constant.
TEST_F(ActionSystemHarvestTimeTest, ConvertsDurabilityToSecondsAtSkillTen) {
	const glm::vec2 treePos{3.0F, 4.0F};
	auto			harvester = createHarvester(treePos, 10.0F);

	world->update(0.1F);

	auto* action = world->getComponent<Action>(harvester);
	ASSERT_EQ(action->type, ActionType::Harvest);
	EXPECT_NEAR(action->duration, 60.0F / harvestWorkRate(10.0F), 0.001F);
	EXPECT_NEAR(action->duration, 60.0F / 18.0F, 0.001F);
}

} // namespace ecs::test
