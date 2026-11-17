// The terms the sim provides today, against a small constructed world.

#include "ColonistTerms.h"
#include "ConditionVocabulary.h"
#include "NearbyTerms.h"
#include "TimeTerms.h"

#include <ecs/World.h>
#include <ecs/components/Action.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/TimeSystem.h>

#include <assets/AssetDefinition.h>

#include <gtest/gtest.h>

using namespace conditions;

namespace {

	constexpr uint32_t kBerryBush = 11;
	constexpr uint32_t kCrate = 12;

	uint16_t capabilityBit(assets::CapabilityType capability) {
		return static_cast<uint16_t>(1U << static_cast<unsigned>(capability));
	}

	class SimTermsTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			provideColonistTerms(vocabulary);
			provideNearbyTerms(vocabulary);
			provideTimeTerms(vocabulary, NightHours{.start = 20.0F, .end = 6.0F});

			colonist = spawnColonist({0.0F, 0.0F});
			ecs::NeedsComponent& needs = world.addComponent<ecs::NeedsComponent>(colonist, ecs::NeedsComponent::createDefault());
			needs.hunger().value = 15.0F;
			ecs::Attributes attributes;
			attributes.sanity = 17.0F;
			world.addComponent<ecs::Attributes>(colonist, attributes);
			world.addComponent<ecs::Task>(colonist, ecs::Task{});
			world.addComponent<ecs::Action>(colonist, ecs::Action{});
			world.addComponent<ecs::Memory>(colonist, ecs::Memory{});
		}

		ecs::EntityID spawnColonist(glm::vec2 position) {
			const ecs::EntityID entity = world.createEntity();
			world.addComponent<ecs::Colonist>(entity, ecs::Colonist{.name = "Test"});
			world.addComponent<ecs::Position>(entity, ecs::Position{position});
			return entity;
		}

		ecs::Memory& memory() { return *world.getComponent<ecs::Memory>(colonist); }

		Truth evaluate(std::string_view text, ecs::EntityID whom) {
			const ParseResult result = vocabulary.parse(text);
			EXPECT_TRUE(result.ok()) << (result.ok() ? "" : result.error->describe());
			if (!result.ok()) {
				return Truth::Unknown;
			}
			return vocabulary.evaluate(result.condition, {.world = world, .colonist = whom});
		}

		Truth evaluate(std::string_view text) { return evaluate(text, colonist); }

		ecs::World			world;
		ecs::EntityID		colonist = ecs::kInvalidEntity;
		ConditionVocabulary vocabulary;
	};

} // namespace

TEST_F(SimTermsTest, NeedReadsTheNeedsValue) {
	EXPECT_EQ(evaluate("need(hunger) < 20"), Truth::True);
	EXPECT_EQ(evaluate("need(thirst) < 20"), Truth::False);
	EXPECT_FALSE(vocabulary.parse("need(hungr) < 20").ok());
}

TEST_F(SimTermsTest, AttributeReadsTheAttributes) {
	EXPECT_EQ(evaluate("attribute(sanity) >= 15"), Truth::True);
	EXPECT_EQ(evaluate("attribute(work_ethic) == 10"), Truth::True);
	EXPECT_FALSE(vocabulary.parse("attribute(luck) > 1").ok());
}

TEST_F(SimTermsTest, CurrentTaskMatchesATypeOrAnyWork) {
	world.getComponent<ecs::Task>(colonist)->type = ecs::TaskType::Craft;
	EXPECT_EQ(evaluate("current_task(craft)"), Truth::True);
	EXPECT_EQ(evaluate("current_task(work)"), Truth::True);
	EXPECT_EQ(evaluate("current_task(wander)"), Truth::False);

	world.getComponent<ecs::Task>(colonist)->type = ecs::TaskType::FulfillNeed;
	EXPECT_EQ(evaluate("current_task(work)"), Truth::False);
	EXPECT_EQ(evaluate("current_task(fulfill_need)"), Truth::True);
}

TEST_F(SimTermsTest, MidCraftIsACraftAction) {
	EXPECT_EQ(evaluate("mid_craft"), Truth::False);
	world.getComponent<ecs::Action>(colonist)->type = ecs::ActionType::Craft;
	EXPECT_EQ(evaluate("mid_craft"), Truth::True);
}

TEST_F(SimTermsTest, HourAndNightReadTheClock) {
	EXPECT_EQ(evaluate("hour >= 0"), Truth::Unknown);
	EXPECT_EQ(evaluate("is_night"), Truth::Unknown);

	ecs::TimeSystem& time = world.registerSystem<ecs::TimeSystem>();
	time.setTimeOfDay(22.5F);
	EXPECT_EQ(evaluate("hour > 22 and hour < 23"), Truth::True);
	EXPECT_EQ(evaluate("is_night"), Truth::True);
	time.setTimeOfDay(3.0F);
	EXPECT_EQ(evaluate("is_night"), Truth::True);
	time.setTimeOfDay(6.0F);
	EXPECT_EQ(evaluate("is_night"), Truth::False);
	time.setTimeOfDay(12.0F);
	EXPECT_EQ(evaluate("is_night"), Truth::False);
}

TEST_F(SimTermsTest, NearCountsRememberedThingsWithinTheRadius) {
	const uint16_t edible = capabilityBit(assets::CapabilityType::Edible);
	memory().rememberWorldEntity({10.0F, 0.0F}, kBerryBush, edible);
	memory().rememberWorldEntity({0.0F, 14.0F}, kBerryBush, edible);
	memory().rememberWorldEntity({30.0F, 0.0F}, kBerryBush, edible);
	memory().rememberWorldEntity({3.0F, 0.0F}, kCrate, capabilityBit(assets::CapabilityType::Storage));

	EXPECT_EQ(evaluate("near(food, 15)"), Truth::True);
	EXPECT_EQ(evaluate("near(food, 5)"), Truth::False);
	EXPECT_EQ(evaluate("count_near(food, 15) == 2"), Truth::True);
	EXPECT_EQ(evaluate("count_near(food, 100) == 3"), Truth::True);
	EXPECT_EQ(evaluate("near(storage, 5)"), Truth::True);
	EXPECT_EQ(evaluate("near(station, 100) or near(bed, 100) or near(loose_item, 100)"), Truth::False);
}

TEST_F(SimTermsTest, NearColonistUsesRememberedColonistsOnly) {
	const ecs::EntityID friendly = spawnColonist({3.0F, 0.0F});
	const ecs::EntityID distant = spawnColonist({50.0F, 0.0F});
	const ecs::EntityID animal = world.createEntity();
	spawnColonist({1.0F, 0.0F}); // beside him, but not remembered

	EXPECT_EQ(evaluate("near(colonist, 5)"), Truth::False);

	memory().rememberDynamicEntity(friendly, {3.0F, 0.0F});
	memory().rememberDynamicEntity(distant, {50.0F, 0.0F});
	memory().rememberDynamicEntity(animal, {2.0F, 0.0F});
	memory().rememberDynamicEntity(colonist, {0.0F, 0.0F});

	EXPECT_EQ(evaluate("near(colonist, 5)"), Truth::True);
	EXPECT_EQ(evaluate("count_near(colonist, 5) >= 2"), Truth::False);
	EXPECT_EQ(evaluate("count_near(colonist, 60) == 2"), Truth::True);
}

TEST_F(SimTermsTest, KindsNothingRemembersAreUnknown) {
	EXPECT_EQ(evaluate("near(lit_fire, 5)"), Truth::Unknown);
	EXPECT_EQ(evaluate("count_near(structure, 5) > 0"), Truth::Unknown);
}

TEST_F(SimTermsTest, AnEntityWithoutTheComponentsIsUnknown) {
	const ecs::EntityID bare = world.createEntity();
	EXPECT_EQ(evaluate("need(hunger) < 20", bare), Truth::Unknown);
	EXPECT_EQ(evaluate("attribute(sanity) > 1", bare), Truth::Unknown);
	EXPECT_EQ(evaluate("current_task(work)", bare), Truth::Unknown);
	EXPECT_EQ(evaluate("mid_craft", bare), Truth::Unknown);
	EXPECT_EQ(evaluate("near(food, 5)", bare), Truth::Unknown);
}

TEST_F(SimTermsTest, ASpecRequiresMixesProvidedAndUnprovidedTerms) {
	memory().rememberWorldEntity({5.0F, 0.0F}, kBerryBush, capabilityBit(assets::CapabilityType::Sleepable));
	EXPECT_EQ(evaluate("near(bed, 15) and not owner_present(bed)"), Truth::Unknown);
	EXPECT_EQ(evaluate("near(bed, 2) and not owner_present(bed)"), Truth::False);
}
