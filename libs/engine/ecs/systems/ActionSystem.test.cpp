// Tests for ActionSystem - Colonist need fulfillment actions
// Tests action factory methods, state machine progression, need restoration, and side effects.

#include "ActionSystem.h"

#include "../GoalTaskRegistry.h"
#include "../InventoryMass.h"
#include "../World.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Inventory.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include "assets/AssetRegistry.h"

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

} // namespace ecs::test
