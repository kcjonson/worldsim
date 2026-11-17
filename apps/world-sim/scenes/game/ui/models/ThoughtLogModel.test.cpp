#include "ThoughtLogModel.h"

#include <ecs/ThoughtTesting.test.h>
#include <ecs/World.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Colonist.h>
#include <ecs/systems/ThoughtSystem.h>
#include <ecs/systems/TimeSystem.h>

#include <gtest/gtest.h>

#include <ostream>
#include <string>
#include <vector>

namespace world_sim {

	namespace {

		// `wetness` is answered by nothing, so a `while` on it holds the thought.
		constexpr std::string_view kTypes = R"(
  <thought id="slept_on_ground" label="Slept on the ground" importance="normal">
    <feeling is="miserable" mood="-10" hours="24"/>
    <line>My back hates me now.</line>
  </thought>
  <thought id="soaked" label="Soaked" importance="normal">
    <feeling is="miserable" mood="-6" while="wetness > 0" cause="soaked"/>
  </thought>
  <thought id="bottled_up" label="Bottled up" importance="normal" stacks="true">
    <feeling is="resigned" mood="-2" hours="24"/>
  </thought>
  <thought id="very_hungry" label="Very hungry" importance="normal">
    <feeling is="annoyed"/>
    <line>I could eat a whole bush.</line>
  </thought>
)";

	} // namespace

	void PrintTo(const MoodThoughtRow& row, std::ostream* out) {
		*out << row.id << " '" << row.label << "' " << row.amount << " '" << row.left << "'";
	}

	class ThoughtLogModelTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			for (const std::string& error : rules.errors) {
				ADD_FAILURE() << error;
			}
			time = &world.registerSystem<ecs::TimeSystem>();
			thoughts = &world.registerSystem<ecs::ThoughtSystem>(rules.types, 7U);
			colonist = world.createEntity();
			world.addComponent<ecs::Colonist>(colonist, ecs::Colonist{.name = "Ada"});
			world.addComponent<ecs::Attributes>(colonist, ecs::testing::attributesAt(10.0F));
		}

		void give(std::string_view type) { thoughts->create({.colonist = colonist, .eventType = rules.type(type)}); }

		void advanceMinutes(float minutes) { time->skipTime(minutes); }

		// The day starts at 06:00. Slept on the ground at 06:00, soaked at 06:30, very hungry at 07:30, and
		// bottled up three times by 08:00.
		void giveTheMorning() {
			give("slept_on_ground");
			advanceMinutes(30.0F);
			give("soaked");
			advanceMinutes(60.0F);
			give("very_hungry");
			for (int repeat = 0; repeat < 3; ++repeat) {
				advanceMinutes(10.0F);
				give("bottled_up");
			}
		}

		[[nodiscard]] const ecs::Thoughts& record() const { return *world.getComponent<ecs::Thoughts>(colonist); }

		[[nodiscard]] ecs::Mood moodAsSummed() const {
			ecs::Mood mood;
			mood.target = ecs::kFullMood;
			mood.needContributions[0] = -12.0F;
			mood.target -= 12.0F;
			ecs::forEachHeldThought(record(), [&mood](const ecs::Thought& thought) {
				mood.thoughtContributions.push_back({.thought = thought.id, .amount = ecs::heldMoodAmount(thought)});
				mood.target += ecs::heldMoodAmount(thought);
			});
			return mood;
		}

		ecs::testing::TestThoughtTypes rules{kTypes};
		ecs::World					   world;
		ecs::TimeSystem*			   time = nullptr;
		ecs::ThoughtSystem*			   thoughts = nullptr;
		ecs::EntityID				   colonist{0};
	};

	TEST_F(ThoughtLogModelTest, EachThoughtIsOneBlockNewestFirst) {
		giveTheMorning();
		const std::vector<ThoughtLogEntry> entries = thoughtLogEntries(rules.types, record(), gameClock(world));

		ASSERT_EQ(entries.size(), 4U);
		EXPECT_EQ(entries[0].summary, "Bottled up x3. Resigned. -6 mood, 1 day");
		EXPECT_EQ(entries[0].when, "Day 1, 08:00");
		EXPECT_EQ(entries[0].line, "");
		EXPECT_EQ(entries[1].summary, "Very hungry. Annoyed.");
		EXPECT_EQ(entries[1].line, "I could eat a whole bush.");
		EXPECT_EQ(entries[1].when, "Day 1, 07:30");
		EXPECT_EQ(entries[2].summary, "Soaked. Miserable. -6 mood, while soaked");
		EXPECT_EQ(entries[3].summary, "Slept on the ground. Miserable. -10 mood, 1 day");
		EXPECT_EQ(entries[3].line, "My back hates me now.");
		EXPECT_EQ(entries[3].when, "Day 1, 06:00");
	}

	TEST_F(ThoughtLogModelTest, TheBreakdownIsTheContributionsTheTargetSums) {
		giveTheMorning();
		const ecs::Mood		mood = moodAsSummed();
		const MoodBreakdown breakdown = moodBreakdown(rules.types, &record(), mood, time->simulationSeconds());

		EXPECT_EQ(breakdown.needs, "-12");
		ASSERT_EQ(breakdown.thoughts.size(), 3U);
		EXPECT_EQ(
			breakdown.thoughts[0], (MoodThoughtRow{.id = 1, .label = "Slept on the ground", .amount = "-10", .left = "22 hours left"})
		);
		EXPECT_EQ(breakdown.thoughts[1], (MoodThoughtRow{.id = 2, .label = "Soaked", .amount = "-6", .left = "while soaked"}));
		EXPECT_EQ(breakdown.thoughts[2].label, "Bottled up x3");
		EXPECT_EQ(breakdown.thoughts[2].amount, "-6");
		EXPECT_EQ(breakdown.thoughts[2].left, "24 hours left");
		// 100 - 12 - 10 - 6 - 6
		EXPECT_EQ(breakdown.target, "66%");
	}

	TEST_F(ThoughtLogModelTest, TheLogIsBuiltAgainOnlyWhenItChanges) {
		ThoughtLogModel model;
		EXPECT_TRUE(model.refresh(world, colonist));
		EXPECT_TRUE(model.entries().empty());
		EXPECT_FALSE(model.refresh(world, colonist));

		give("bottled_up");
		EXPECT_TRUE(model.refresh(world, colonist));
		EXPECT_FALSE(model.refresh(world, colonist));

		give("bottled_up");
		EXPECT_TRUE(model.refresh(world, colonist));
		ASSERT_EQ(model.entries().size(), 1U);
		EXPECT_EQ(model.entries()[0].summary, "Bottled up x2. Resigned. -4 mood, 1 day");
	}

	TEST(ThoughtLogText, AmountsAndLengths) {
		EXPECT_EQ(moodAmountText(-10.0F), "-10");
		EXPECT_EQ(moodAmountText(4.5F), "+4.5");
		EXPECT_EQ(moodAmountText(0.0F), "0");
		EXPECT_EQ(gameDurationText(24.0), "1 day");
		EXPECT_EQ(gameDurationText(72.0), "3 days");
		EXPECT_EQ(gameDurationText(1.0), "1 hour");
		EXPECT_EQ(gameDurationText(6.0), "6 hours");
		EXPECT_EQ(gameDurationText(1.5), "1.5 hours");
		EXPECT_EQ(gameDurationText(0.5), "30 min");
		EXPECT_EQ(timeLeftText(0.2), "12 min left");
		EXPECT_EQ(timeLeftText(5.1), "6 hours left");
		EXPECT_EQ(timeLeftText(60.0), "3 days left");
	}

} // namespace world_sim
