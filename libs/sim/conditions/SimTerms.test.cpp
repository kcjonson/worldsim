// The shipped condition vocabulary (assets/config/conditions) and the terms the sim provides, against a
// small constructed world.

#include "SimTerms.h"
#include "ConditionVocabulary.h"

#include <ecs/GoalTaskRegistry.h>
#include <ecs/World.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Memory.h>
#include <ecs/components/Needs.h>
#include <ecs/components/Task.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/TimeSystem.h>

#include <assets/AssetDefinition.h>
#include <assets/ConditionConfig.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

using namespace conditions;

namespace {

	constexpr uint32_t kBerryBush = 11;
	constexpr uint32_t kCrate = 12;

	std::filesystem::path shippedConditions() {
		// <root>/libs/sim/conditions/SimTerms.test.cpp -> <root>/assets/config/conditions
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "conditions";
	}

	uint16_t capabilityBit(assets::CapabilityType capability) {
		return static_cast<uint16_t>(1U << static_cast<unsigned>(capability));
	}

	class SimTermsTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			build(shippedConditions());

			colonist = spawnColonist({0.0F, 0.0F});
			ecs::NeedsComponent& needs = world.addComponent<ecs::NeedsComponent>(colonist, ecs::NeedsComponent::createDefault());
			needs.hunger().value = 15.0F;
			ecs::Attributes attributes;
			attributes.sanity = 17.0F;
			world.addComponent<ecs::Attributes>(colonist, attributes);
			world.addComponent<ecs::Task>(colonist, ecs::Task{});
			world.addComponent<ecs::Memory>(colonist, ecs::Memory{});
		}

		void TearDown() override { ecs::GoalTaskRegistry::Get().clear(); }

		void build(const std::filesystem::path& folder) {
			const assets::ConditionConfigLoad load = assets::loadConditionConfig(folder);
			for (const std::string& error : load.errors) {
				ADD_FAILURE() << error;
			}
			for (const std::string& error : buildSimVocabulary(vocabulary, load.config)) {
				ADD_FAILURE() << error;
			}
		}

		ecs::EntityID spawnColonist(glm::vec2 position) {
			const ecs::EntityID entity = world.createEntity();
			world.addComponent<ecs::Colonist>(entity, ecs::Colonist{.name = "Test"});
			world.addComponent<ecs::Position>(entity, ecs::Position{position});
			return entity;
		}

		ecs::Memory& memory() { return *world.getComponent<ecs::Memory>(colonist); }
		ecs::Task&	 task() { return *world.getComponent<ecs::Task>(colonist); }

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

TEST_F(SimTermsTest, TheShippedVocabularyDeclaresEveryTermTheSpecsName) {
	for (const char* name : {"near",		   "count_near",   "in_room",		"room_occupants", "unexplored_in_sight",
							 "observed",	   "owner",		   "owner_present", "opinion",		  "likes",
							 "dislikes",	   "recent_event", "mood",			"mood_cause",	  "need",
							 "attribute",	   "trait",		   "current_task",	"mid_craft",	  "hours_worked",
							 "holds_in_a_row", "affinity",	   "time_below",	"scarcity",		  "colony_mood",
							 "hour",		   "is_night",	   "wetness",		"feels_hot",	  "feels_cold"}) {
		EXPECT_TRUE(vocabulary.findTerm(name).has_value()) << name;
	}
}

TEST_F(SimTermsTest, TheSpecsExamplesParse) {
	const char* examples[] = {
		// mood.md, Data
		"not owner_present(target) and owner(target) != self",
		"dislikes(owner(target))",
		"not observed",
		// thoughts.md, Data
		"wetness > 0.6",
		"trait(volatile)",
		"feels_hot",
		// mood.md, Conditions and the behavior tables
		"near(bed, 15)",
		"count_near(colonist, 5) >= 2",
		"recent_event(target, argument, 24)",
		"time_below(12, 48)",
		"hours_worked(24) > 10 and not is_night",
		"affinity(hoard) >= 2 or holds_in_a_row > 3",
		"mood_cause is hunger and colony_mood < 30 and hour >= 22",
		"in_room and room_occupants > 3 or unexplored_in_sight",
		"current_task(work) and not mid_craft and opinion(target) < -20",
		"attribute(sanity) > 15 and likes(target) and need(hunger) < 30",
		"scarcity(food) is short and not feels_cold",
		"near(latrine, 30) or near(door, 5) or near(tool, 5) or near(prepared_food, 5)",
	};
	for (const char* example : examples) {
		const ParseResult result = vocabulary.parse(example);
		EXPECT_TRUE(result.ok()) << example << ": " << (result.ok() ? "" : result.error->describe());
	}
}

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

TEST_F(SimTermsTest, CurrentTaskMatchesATypeOrATaskGroup) {
	task().type = ecs::TaskType::Craft;
	EXPECT_EQ(evaluate("current_task(craft)"), Truth::True);
	EXPECT_EQ(evaluate("current_task(work)"), Truth::True);
	EXPECT_EQ(evaluate("current_task(wander)"), Truth::False);

	task().type = ecs::TaskType::FulfillNeed;
	EXPECT_EQ(evaluate("current_task(work)"), Truth::False);
	EXPECT_EQ(evaluate("current_task(fulfill_need)"), Truth::True);
}

TEST_F(SimTermsTest, MidCraftIsACraftUnderWayIncludingFetchingItsInputs) {
	EXPECT_EQ(evaluate("mid_craft"), Truth::False);

	task().type = ecs::TaskType::Craft;
	task().craftRecipeDefName = "StoneAxe";
	EXPECT_EQ(evaluate("mid_craft"), Truth::True);

	ecs::GoalTaskRegistry& goals = ecs::GoalTaskRegistry::Get();
	const uint64_t		   craftGoal =
		goals.createGoal({.type = ecs::TaskType::Craft, .destinationEntity = 101, .owner = ecs::GoalOwner::CraftingGoalSystem});
	ecs::GoalTask haulForCraft{.type = ecs::TaskType::Haul, .owner = ecs::GoalOwner::CraftingGoalSystem};
	haulForCraft.parentGoalId = craftGoal;
	const uint64_t inputGoal = goals.createGoal(haulForCraft);
	const uint64_t storageGoal =
		goals.createGoal({.type = ecs::TaskType::Haul, .destinationEntity = 102, .owner = ecs::GoalOwner::StorageGoalSystem});

	task().clear();
	task().type = ecs::TaskType::Haul;
	task().haulGoalId = inputGoal;
	EXPECT_EQ(evaluate("mid_craft"), Truth::True);
	task().haulGoalId = craftGoal;
	EXPECT_EQ(evaluate("mid_craft"), Truth::True);
	task().haulGoalId = storageGoal;
	EXPECT_EQ(evaluate("mid_craft"), Truth::False);

	task().clear();
	task().type = ecs::TaskType::Harvest;
	task().harvestGoalId = inputGoal;
	EXPECT_EQ(evaluate("mid_craft"), Truth::True);
}

TEST_F(SimTermsTest, HourAndNightReadTheClockAgainstTheConfiguredNight) {
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
	EXPECT_EQ(evaluate("count_near(food, 100) >= 2"), Truth::True);
	EXPECT_EQ(evaluate("count_near(food, 100) < 1"), Truth::False);
	EXPECT_EQ(evaluate("near(storage, 5)"), Truth::True);
	EXPECT_EQ(evaluate("near(station, 100) or near(bed, 100) or near(loose_item, 100) or near(latrine, 100)"), Truth::False);
}

TEST_F(SimTermsTest, NearColonistUsesRememberedColonistsOnly) {
	const ecs::EntityID friendly = spawnColonist({3.0F, 0.0F});
	const ecs::EntityID distant = spawnColonist({50.0F, 0.0F});
	spawnColonist({1.0F, 0.0F}); // beside him, but not remembered

	EXPECT_EQ(evaluate("near(colonist, 5)"), Truth::False);

	memory().rememberDynamicEntity(friendly, {3.0F, 0.0F});
	memory().rememberDynamicEntity(distant, {50.0F, 0.0F});
	memory().rememberDynamicEntity(colonist, {0.0F, 0.0F});

	EXPECT_EQ(evaluate("near(colonist, 5)"), Truth::True);
	EXPECT_EQ(evaluate("count_near(colonist, 5) >= 2"), Truth::False);
	EXPECT_EQ(evaluate("count_near(colonist, 60) == 2"), Truth::True);
}

TEST_F(SimTermsTest, KindsWithNoMappingAreUnknown) {
	EXPECT_EQ(evaluate("near(lit_fire, 5)"), Truth::Unknown);
	EXPECT_EQ(evaluate("count_near(structure, 5) > 0"), Truth::Unknown);
	EXPECT_EQ(evaluate("near(door, 5) or near(tool, 5) or near(prepared_food, 5)"), Truth::Unknown);
}

TEST_F(SimTermsTest, AnEntityWithoutTheComponentsIsUnknown) {
	const ecs::EntityID bare = world.createEntity();
	EXPECT_EQ(evaluate("need(hunger) < 20", bare), Truth::Unknown);
	EXPECT_EQ(evaluate("attribute(sanity) > 1", bare), Truth::Unknown);
	EXPECT_EQ(evaluate("current_task(work)", bare), Truth::Unknown);
	EXPECT_EQ(evaluate("mid_craft", bare), Truth::Unknown);
	EXPECT_EQ(evaluate("near(food, 5)", bare), Truth::Unknown);
}

TEST_F(SimTermsTest, ASpecConditionMixesProvidedAndUnprovidedTerms) {
	memory().rememberWorldEntity({5.0F, 0.0F}, kBerryBush, capabilityBit(assets::CapabilityType::Sleepable));
	EXPECT_EQ(evaluate("near(bed, 15) and not observed"), Truth::Unknown);
	EXPECT_EQ(evaluate("near(bed, 2) and not observed"), Truth::False);
}

TEST(SimTermsData, AKindAddedThroughDataWorksWithNear) {
	const std::filesystem::path folder = std::filesystem::temp_directory_path() / "worldsim-conditions-mod-test";
	std::filesystem::remove_all(folder);
	std::filesystem::create_directories(folder);
	std::filesystem::copy(shippedConditions(), folder / "base", std::filesystem::copy_options::recursive);
	std::filesystem::create_directories(folder / "mods" / "wells");
	{
		std::ofstream mod(folder / "mods" / "wells" / "kinds.xml");
		mod << R"(<ConditionVocabulary><Kind name="well" capability="Drinkable"/></ConditionVocabulary>)";
	}
	const assets::ConditionConfigLoad load = assets::loadConditionConfig(folder);
	std::filesystem::remove_all(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();

	ConditionVocabulary vocabulary;
	ASSERT_TRUE(buildSimVocabulary(vocabulary, load.config).empty());

	ecs::World			world;
	const ecs::EntityID colonist = world.createEntity();
	world.addComponent<ecs::Position>(colonist, ecs::Position{{0.0F, 0.0F}});
	ecs::Memory& memory = world.addComponent<ecs::Memory>(colonist, ecs::Memory{});
	memory.rememberWorldEntity({4.0F, 0.0F}, kCrate, capabilityBit(assets::CapabilityType::Drinkable));

	const ParseResult result = vocabulary.parse("near(well, 5) and count_near(well, 3) == 0");
	ASSERT_TRUE(result.ok()) << result.error->describe();
	EXPECT_EQ(vocabulary.evaluate(result.condition, {.world = world, .colonist = colonist}), Truth::True);
}
