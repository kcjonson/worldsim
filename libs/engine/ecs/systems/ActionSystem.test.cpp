// Tests for ActionSystem - Colonist need fulfillment actions
// Tests action factory methods, state machine progression, need restoration, and side effects.

#include "ActionSystem.h"

#include "../World.h"
#include "../components/Action.h"
#include "../components/Appearance.h"
#include "../components/Memory.h"
#include "../components/Movement.h"
#include "../components/Needs.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <gtest/gtest.h>

namespace ecs::test {

// =============================================================================
// Action Factory Tests (Unit tests for Action struct)
// =============================================================================

TEST(ActionFactoryTest, EatActionCreation) {
	auto action = Action::Eat(0.5F);

	EXPECT_EQ(action.type, ActionType::Eat);
	EXPECT_EQ(action.state, ActionState::Starting);
	EXPECT_FLOAT_EQ(action.duration, 2.0F);
	EXPECT_FLOAT_EQ(action.elapsed, 0.0F);

	// Check variant-based effect
	ASSERT_TRUE(action.hasNeedEffect());
	const auto& effect = action.needEffect();
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
	auto action = Action::Eat(0.8F);
	action.elapsed = 1.5F;
	action.state = ActionState::InProgress;

	action.clear();

	EXPECT_EQ(action.type, ActionType::None);
	EXPECT_EQ(action.state, ActionState::Starting);
	EXPECT_FLOAT_EQ(action.duration, 0.0F);
	EXPECT_FLOAT_EQ(action.elapsed, 0.0F);
	EXPECT_FALSE(action.hasNeedEffect()); // Effect variant reset to monostate
}

TEST(ActionFactoryTest, ActionIsActive) {
	Action action{};
	EXPECT_FALSE(action.isActive());

	action = Action::Eat(0.5F);
	EXPECT_TRUE(action.isActive());
}

TEST(ActionFactoryTest, ActionProgress) {
	auto action = Action::Eat(0.5F);
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
		world->registerSystem<ActionSystem>();
	}

	void TearDown() override { world.reset(); }

	/// Create a colonist entity with all required components for action processing
	EntityID createColonist(glm::vec2 position = {0.0F, 0.0F}) {
		auto entity = world->createEntity();
		world->addComponent<Position>(entity, Position{position});
		world->addComponent<Velocity>(entity, Velocity{{0.0F, 0.0F}});
		world->addComponent<MovementTarget>(entity, MovementTarget{{0.0F, 0.0F}, 2.0F, false});
		world->addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
		world->addComponent<Memory>(entity, Memory{});
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

	// Add edible entity to memory at target position
	auto* memory = world->getComponent<Memory>(colonist);
	memory->rememberWorldEntity(
		{5.0F, 5.0F}, 1001, static_cast<uint8_t>(1 << static_cast<size_t>(engine::assets::CapabilityType::Edible))
	);

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

	// Verify bio pile has correct appearance
	bool foundBioPile = false;
	for (auto [entity, pos, appearance] : world->view<Position, Appearance>()) {
		if (appearance.defName == "Misc_BioPile") {
			foundBioPile = true;
			// Bio pile should be at the colonist's position
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

} // namespace ecs::test
