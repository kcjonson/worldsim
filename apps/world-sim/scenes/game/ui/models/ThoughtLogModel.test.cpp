#include "ThoughtLogModel.h"

#include <assets/MoodConfig.h>
#include <ecs/ShippedConfig.test.h>
#include <ecs/ThoughtTesting.test.h>
#include <ecs/World.h>
#include <ecs/components/Attributes.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/Needs.h>
#include <ecs/systems/MoodSystem.h>
#include <ecs/systems/ThoughtSystem.h>
#include <ecs/systems/TimeSystem.h>

#include <gtest/gtest.h>

#include <algorithm>
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
  <thought id="saw_the_sea" label="Saw the sea" importance="major">
    <feeling is="delighted"/>
  </thought>
  <thought id="vented" label="Vented" importance="normal">
    <feeling is="delighted" mood="+4" hours="12"/>
  </thought>
  <thought id="cheered" label="Cheered" importance="normal">
    <feeling is="delighted" mood="+20" hours="12"/>
  </thought>
)";

		std::vector<std::string> summaries(const ThoughtLogModel& model) {
			std::vector<std::string> shown;
			for (const ThoughtLogEntry& entry : model.entries()) {
				shown.push_back(entry.summary);
			}
			return shown;
		}

		// What the rows on screen come to: 100 less the need rows, or 0 when the floor line shows, plus the
		// thought rows. With no clamp that is the target; with one, the clamped note.
		void expectTheRowsReconcile(const MoodBreakdown& breakdown) {
			float shown = ecs::kFullMood;
			for (const MoodNeedRow& row : breakdown.needs) {
				shown += std::stof(row.amount);
			}
			if (breakdown.needsAtFloor) {
				EXPECT_LE(shown, 0.0F) << "the floor line shows only when the need rows reach 100";
				shown = 0.0F;
			}
			for (const MoodThoughtRow& row : breakdown.thoughts) {
				shown += std::stof(row.amount);
			}
			const float		   rounding = 0.05F * static_cast<float>(breakdown.needs.size() + breakdown.thoughts.size() + 1);
			const std::string& total = breakdown.clampedFrom.empty() ? breakdown.target : breakdown.clampedFrom;
			EXPECT_NEAR(shown, std::stof(total), rounding) << "the rows come to " << total;
		}

	} // namespace

	void PrintTo(const MoodThoughtRow& row, std::ostream* out) {
		*out << row.id << " '" << row.label << "' " << row.amount << " '" << row.left << "'";
	}

	void PrintTo(const MoodNeedRow& row, std::ostream* out) {
		*out << "'" << row.label << "' " << row.amount;
	}

	class ThoughtLogModelTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			for (const std::string& error : rules.errors) {
				ADD_FAILURE() << error;
			}
			time = &world.registerSystem<ecs::TimeSystem>();
			thoughts = &world.registerSystem<ecs::ThoughtSystem>(rules.types, 7U);
			colonist = spawn("Ada");
			ASSERT_TRUE(moodConfig.loadFromFile(ecs::testing::repoConfig(), "colonists/mood.xml"));
			moods = &world.registerSystem<ecs::MoodSystem>(moodConfig);
			world.addComponent<ecs::NeedsComponent>(colonist, ecs::testing::shippedNeeds());
		}

		ecs::EntityID spawn(const char* name) {
			const ecs::EntityID spawned = world.createEntity();
			world.addComponent<ecs::Colonist>(spawned, ecs::Colonist{.name = name});
			world.addComponent<ecs::Attributes>(spawned, ecs::testing::attributesAt(10.0F));
			return spawned;
		}

		void give(std::string_view type) { give(colonist, type); }
		void give(ecs::EntityID who, std::string_view type) { thoughts->create({.colonist = who, .eventType = rules.type(type)}); }

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

		void setNeed(ecs::NeedType need, float value) { world.getComponent<ecs::NeedsComponent>(colonist)->get(need).value = value; }

		/// The colonist's mood as MoodSystem sums it now.
		[[nodiscard]] const ecs::Mood& summedMood() {
			moods->update(0.0F);
			return *world.getComponent<ecs::Mood>(colonist);
		}

		[[nodiscard]] MoodBreakdown breakdownNow() { return moodBreakdown(rules.types, &record(), summedMood(), time->simulationSeconds()); }

		ecs::testing::TestThoughtTypes rules{kTypes};
		assets::MoodConfig			   moodConfig;
		ecs::World					   world;
		ecs::TimeSystem*			   time = nullptr;
		ecs::ThoughtSystem*			   thoughts = nullptr;
		ecs::MoodSystem*			   moods = nullptr;
		ecs::EntityID				   colonist{0};
	};

	TEST_F(ThoughtLogModelTest, TheNotableThoughtsShowFirstAndAllShowsTheRest) {
		giveTheMorning();
		give("saw_the_sea");
		ThoughtLogModel model;
		ASSERT_TRUE(model.refresh(world, colonist));
		EXPECT_EQ(model.currentFilter(), ThoughtLogFilter::Notable);
		EXPECT_EQ(
			summaries(model),
			(std::vector<std::string>{
				"Saw the sea. Delighted.",
				"Bottled up x3. Resigned. -6 mood, 1 day",
				"Soaked. Miserable. -6 mood, while soaked",
				"Slept on the ground. Miserable. -10 mood, 1 day",
			})
		);
		EXPECT_EQ(model.entries()[1].when, "Day 1, 08:00");
		EXPECT_EQ(model.entries()[1].line, "");
		EXPECT_EQ(model.entries()[3].line, "My back hates me now.");
		EXPECT_EQ(model.entries()[3].when, "Day 1, 06:00");

		model.setFilter(ThoughtLogFilter::All);
		ASSERT_TRUE(model.refresh(world, colonist));
		ASSERT_EQ(model.entries().size(), 5U);
		EXPECT_EQ(model.entries()[2].summary, "Very hungry. Annoyed.");
		EXPECT_EQ(model.entries()[2].line, "I could eat a whole bush.");
		EXPECT_EQ(model.entries()[2].when, "Day 1, 07:30");
	}

	TEST_F(ThoughtLogModelTest, AColonistWithOnlyPassingThoughtsHasNothingNotable) {
		give("very_hungry");
		ThoughtLogModel model;
		model.refresh(world, colonist);
		EXPECT_TRUE(model.entries().empty());
		EXPECT_TRUE(model.hasThoughts());

		const ecs::EntityID newcomer = spawn("Grace");
		model.refresh(world, newcomer);
		EXPECT_TRUE(model.entries().empty());
		EXPECT_FALSE(model.hasThoughts());
	}

	TEST_F(ThoughtLogModelTest, ShowsAPageAtATimeAndStartsAgainForAnotherColonist) {
		const std::size_t kThoughts = (2 * kThoughtLogPage) + 7;
		for (std::size_t index = 0; index < kThoughtLogPage; ++index) {
			give("saw_the_sea");
		}
		ThoughtLogModel model;
		model.refresh(world, colonist);
		EXPECT_EQ(model.entries().size(), kThoughtLogPage);
		EXPECT_FALSE(model.hasOlder()) << "exactly a page has nothing older";

		for (std::size_t index = kThoughtLogPage; index < kThoughts; ++index) {
			give("saw_the_sea");
		}
		ASSERT_TRUE(model.refresh(world, colonist));
		EXPECT_EQ(model.entries().size(), kThoughtLogPage);
		EXPECT_TRUE(model.hasOlder());
		EXPECT_EQ(model.entries().front().id, kThoughts) << "the newest first";

		model.showOlder();
		ASSERT_TRUE(model.refresh(world, colonist));
		EXPECT_EQ(model.entries().size(), 2 * kThoughtLogPage);
		model.showOlder();
		ASSERT_TRUE(model.refresh(world, colonist));
		EXPECT_EQ(model.entries().size(), kThoughts);
		EXPECT_FALSE(model.hasOlder());
		EXPECT_EQ(model.entries().back().id, 1U);

		const ecs::EntityID other = spawn("Grace");
		for (std::size_t index = 0; index < kThoughts; ++index) {
			give(other, "saw_the_sea");
		}
		ASSERT_TRUE(model.refresh(world, other));
		EXPECT_EQ(model.entries().size(), kThoughtLogPage);
		EXPECT_TRUE(model.hasOlder());

		model.showOlder();
		model.select(colonist);
		model.select(other);
		model.refresh(world, other);
		EXPECT_EQ(model.entries().size(), kThoughtLogPage) << "a selection change resets the page without a refresh in between";
	}

	TEST_F(ThoughtLogModelTest, TheNewestArrivalIsFirstAndEqualTimesGoByHigherId) {
		give("slept_on_ground");
		advanceMinutes(10.0F);
		give("saw_the_sea");
		advanceMinutes(10.0F);
		give("saw_the_sea");
		give("saw_the_sea");
		ThoughtLogModel model;
		model.refresh(world, colonist);
		ASSERT_EQ(model.entries().size(), 4U);
		EXPECT_EQ(model.entries()[0].id, 4U);
		EXPECT_EQ(model.entries()[1].id, 3U) << "the same minute, the higher id first";
		EXPECT_EQ(model.entries()[2].id, 2U);
		EXPECT_EQ(model.entries()[3].id, 1U);

		advanceMinutes(10.0F);
		give("slept_on_ground");
		ASSERT_TRUE(model.refresh(world, colonist));
		ASSERT_EQ(model.entries().size(), 4U);
		EXPECT_EQ(model.entries()[0].id, 1U) << "a repeat folded into an older thought moves it ahead of newer ones";
		EXPECT_EQ(model.entries()[1].id, 4U);
	}

	TEST_F(ThoughtLogModelTest, TheLogIsBuiltAgainOnlyWhenTheRecordChanges) {
		ThoughtLogModel model;
		EXPECT_TRUE(model.refresh(world, colonist));
		EXPECT_TRUE(model.entries().empty());
		EXPECT_FALSE(model.refresh(world, colonist));

		give("bottled_up");
		EXPECT_TRUE(model.refresh(world, colonist));
		EXPECT_FALSE(model.refresh(world, colonist));
		const uint64_t built = model.version();

		// A repeat folded into the held thought at the same simulation time.
		give("bottled_up");
		EXPECT_TRUE(model.refresh(world, colonist));
		EXPECT_GT(model.version(), built);
		ASSERT_EQ(model.entries().size(), 1U);
		EXPECT_EQ(model.entries()[0].summary, "Bottled up x2. Resigned. -4 mood, 1 day");
	}

	TEST_F(ThoughtLogModelTest, EveryWriteToTheRecordMovesItsRevision) {
		give("slept_on_ground");
		uint64_t revision = record().revision;
		give("slept_on_ground");
		EXPECT_GT(record().revision, revision) << "a repeat folded in";
		revision = record().revision;
		give("very_hungry");
		EXPECT_GT(record().revision, revision) << "a thought with no mood effect";
		revision = record().revision;
		advanceMinutes(25.0F * 60.0F);
		thoughts->update(0.0F);
		EXPECT_GT(record().revision, revision) << "a held thought ending";
	}

	TEST_F(ThoughtLogModelTest, ATimeSetDoesNotMoveWhenAThoughtArrived) {
		give("slept_on_ground");
		time->setTimeOfDay(20.0F);
		give("very_hungry");
		ThoughtLogModel model;
		model.setFilter(ThoughtLogFilter::All);
		model.refresh(world, colonist);
		ASSERT_EQ(model.entries().size(), 2U);
		EXPECT_EQ(model.entries()[0].when, "Day 1, 20:00");
		EXPECT_EQ(model.entries()[1].when, "Day 1, 06:00");
	}

	TEST_F(ThoughtLogModelTest, TheBreakdownIsTheContributionsTheTargetSums) {
		giveTheMorning();
		// Shipped costs: hunger at 30 is penalty 0.3 of 25, thirst at 46 is 0.18 of 25, and energy at 69.9 is
		// 0.00075 of 20, which rounds to nothing.
		setNeed(ecs::NeedType::Hunger, 30.0F);
		setNeed(ecs::NeedType::Thirst, 46.0F);
		setNeed(ecs::NeedType::Energy, 69.9F);
		const MoodBreakdown breakdown = breakdownNow();

		EXPECT_EQ(
			breakdown.needs,
			(std::vector<MoodNeedRow>{
				{.need = ecs::NeedType::Hunger, .label = "Hunger", .amount = "-7.5"},
				{.need = ecs::NeedType::Thirst, .label = "Thirst", .amount = "-4.5"},
			})
		) << "the largest cost first, and a need whose cost rounds to nothing is not listed";
		EXPECT_FALSE(breakdown.needsAtFloor);
		ASSERT_EQ(breakdown.thoughts.size(), 3U);
		EXPECT_EQ(
			breakdown.thoughts[0], (MoodThoughtRow{.id = 1, .label = "Slept on the ground", .amount = "-10", .left = "22 hours left"})
		);
		EXPECT_EQ(breakdown.thoughts[1].label, "Bottled up x3") << "the amounts tie, the newest arrival first";
		EXPECT_EQ(breakdown.thoughts[1].amount, "-6");
		EXPECT_EQ(breakdown.thoughts[1].left, "24 hours left");
		EXPECT_EQ(breakdown.thoughts[2], (MoodThoughtRow{.id = 2, .label = "Soaked", .amount = "-6", .left = "while soaked"}));
		EXPECT_EQ(breakdown.target, "66%");
		EXPECT_EQ(breakdown.clampedFrom, "");
		expectTheRowsReconcile(breakdown);
	}

	TEST_F(ThoughtLogModelTest, TheBreakdownShowsTenthsAndSaysWhenTheTargetIsClamped) {
		giveTheMorning();
		setNeed(ecs::NeedType::Thirst, 46.0F);
		const MoodBreakdown tenths = breakdownNow();
		ASSERT_EQ(tenths.needs.size(), 1U);
		EXPECT_EQ(tenths.needs[0].amount, "-4.5");
		EXPECT_EQ(tenths.target, "73.5%");
		EXPECT_EQ(tenths.clampedFrom, "");
		expectTheRowsReconcile(tenths);

		setNeed(ecs::NeedType::Thirst, 0.0F);
		setNeed(ecs::NeedType::Temperature, 0.0F);
		const MoodBreakdown clamped = breakdownNow();
		EXPECT_FALSE(clamped.needsAtFloor) << "the needs take 85, leaving a needs score of 15";
		EXPECT_EQ(clamped.target, "0%");
		EXPECT_EQ(clamped.clampedFrom, "-7%") << "15 - 10 - 6 - 6";
		expectTheRowsReconcile(clamped);
	}

	TEST_F(ThoughtLogModelTest, TheBreakdownSaysWhenTheTargetIsClampedAtTheTop) {
		give("cheered");
		const MoodBreakdown breakdown = breakdownNow();
		EXPECT_TRUE(breakdown.needs.empty());
		EXPECT_FALSE(breakdown.needsAtFloor);
		EXPECT_EQ(breakdown.target, "100%");
		EXPECT_EQ(breakdown.clampedFrom, "120%");
		expectTheRowsReconcile(breakdown);
	}

	TEST_F(ThoughtLogModelTest, EveryNeedEmptyShowsTheFloorAndTheThoughtGivesTheTarget) {
		for (size_t i = 0; i < ecs::kNeedCount; ++i) {
			setNeed(static_cast<ecs::NeedType>(i), 0.0F);
		}
		give("vented");
		const MoodBreakdown breakdown = breakdownNow();
		EXPECT_EQ(
			breakdown.needs,
			(std::vector<MoodNeedRow>{
				{.need = ecs::NeedType::Temperature, .label = "Temperature", .amount = "-60"},
				{.need = ecs::NeedType::Hunger, .label = "Hunger", .amount = "-25"},
				{.need = ecs::NeedType::Thirst, .label = "Thirst", .amount = "-25"},
				{.need = ecs::NeedType::Energy, .label = "Energy", .amount = "-20"},
				{.need = ecs::NeedType::Hygiene, .label = "Hygiene", .amount = "-15"},
				{.need = ecs::NeedType::Recreation, .label = "Recreation", .amount = "-15"},
				{.need = ecs::NeedType::Bladder, .label = "Bladder", .amount = "-10"},
				{.need = ecs::NeedType::Digestion, .label = "Digestion", .amount = "-10"},
			})
		);
		EXPECT_TRUE(breakdown.needsAtFloor) << "the costs come to 180, and the needs take mood to 0 at most";
		ASSERT_EQ(breakdown.thoughts.size(), 1U);
		EXPECT_EQ(breakdown.thoughts[0].amount, "+4");
		EXPECT_EQ(breakdown.target, "4%");
		EXPECT_EQ(breakdown.clampedFrom, "") << "0 + 4 is inside 0 to 100, so nothing is clamped";
		expectTheRowsReconcile(breakdown);
	}

	TEST_F(ThoughtLogModelTest, TheVitalNeedsEmptyPastTheFloorAreNotAClamp) {
		setNeed(ecs::NeedType::Temperature, 0.0F);
		setNeed(ecs::NeedType::Hunger, 0.0F);
		setNeed(ecs::NeedType::Thirst, 0.0F);
		setNeed(ecs::NeedType::Energy, 0.0F);
		give("cheered");
		const MoodBreakdown breakdown = breakdownNow();
		EXPECT_EQ(
			breakdown.needs,
			(std::vector<MoodNeedRow>{
				{.need = ecs::NeedType::Temperature, .label = "Temperature", .amount = "-60"},
				{.need = ecs::NeedType::Hunger, .label = "Hunger", .amount = "-25"},
				{.need = ecs::NeedType::Thirst, .label = "Thirst", .amount = "-25"},
				{.need = ecs::NeedType::Energy, .label = "Energy", .amount = "-20"},
			})
		);
		EXPECT_TRUE(breakdown.needsAtFloor) << "the costs come to 130";
		ASSERT_EQ(breakdown.thoughts.size(), 1U);
		EXPECT_EQ(breakdown.thoughts[0].amount, "+20");
		EXPECT_EQ(breakdown.target, "20%");
		EXPECT_EQ(breakdown.clampedFrom, "") << "0 + 20 is inside 0 to 100, so nothing is clamped";
		expectTheRowsReconcile(breakdown);
	}

	TEST_F(ThoughtLogModelTest, AMergedEntryShowsItsCountAndDateRange) {
		ecs::Thought thought{
			.id = 9,
			.eventType = rules.type("slept_on_ground"),
			.feeling = rules.feeling("miserable"),
			.moodEffect = ecs::MoodEffect::timed(-10.0F, 24.0F),
			.arrivedClock = {.day = 3, .minuteOfDay = (8 * 60) + 10},
			.lastRepeatedClock = {.day = 9, .minuteOfDay = (17 * 60) + 42},
			.count = 12,
			.merged = true,
		};
		EXPECT_EQ(thoughtWhenText(thought), "Day 3, 08:10 to Day 9, 17:42");
		EXPECT_EQ(thoughtSummary(rules.types, thought), "Slept on the ground x12. Miserable. -10 mood each, 1 day");

		thought.merged = false;
		thought.count = 3;
		EXPECT_EQ(thoughtWhenText(thought), "Day 9, 17:42") << "a stack shows its latest arrival";
		EXPECT_EQ(thoughtSummary(rules.types, thought), "Slept on the ground x3. Miserable. -30 mood, 1 day");
	}

	// Pruning merges and drops around held thoughts, so the log shows merged entries and the breakdown still
	// finds every held thought it was given.
	TEST(ThoughtLogPruning, TheLogShowsMergedEntriesAndTheBreakdownStillResolves) {
		ecs::testing::TestThoughtTypes rules{kTypes, R"(
  <tuning maxLineLength="40" maxSubjectNameLength="12" whileCheckMinutes="15">
    <kept limit="20" pruneStartsAt="0.9" pruneStopsAt="0.8" recentDays="1" trivialWeight="1" normalWeight="3"/>
  </tuning>
)"};
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		ecs::World			world;
		ecs::TimeSystem&	time = world.registerSystem<ecs::TimeSystem>();
		ecs::ThoughtSystem& thoughts = world.registerSystem<ecs::ThoughtSystem>(rules.types, 7U);
		const ecs::EntityID colonist = world.createEntity();
		world.addComponent<ecs::Colonist>(colonist, ecs::Colonist{.name = "Ada"});
		world.addComponent<ecs::Attributes>(colonist, ecs::testing::attributesAt(10.0F));
		const auto give = [&](std::string_view type) { thoughts.create({.colonist = colonist, .eventType = rules.type(type)}); };

		give("soaked");
		give("saw_the_sea");
		for (int step = 0; step < 8 * 20; ++step) {
			time.skipTime(180.0F);
			give("very_hungry");
		}
		const ecs::Thoughts& record = *world.getComponent<ecs::Thoughts>(colonist);
		ASSERT_LT(record.record.size(), rules.types.keeping().pruneStartCount);

		ThoughtLogModel model;
		model.setFilter(ThoughtLogFilter::All);
		model.refresh(world, colonist);
		const auto merged = std::ranges::find_if(model.entries(), [](const ThoughtLogEntry& entry) { return entry.when.find(" to ") != std::string::npos; });
		ASSERT_NE(merged, model.entries().end());
		EXPECT_TRUE(merged->summary.starts_with("Very hungry x")) << merged->summary;
		EXPECT_TRUE(std::ranges::any_of(model.entries(), [](const ThoughtLogEntry& entry) { return entry.summary == "Saw the sea. Delighted."; }));

		ecs::Mood mood;
		ecs::forEachHeldThought(record, [&mood](const ecs::Thought& thought) {
			mood.thoughtContributions.push_back({.thought = thought.id, .amount = ecs::heldMoodAmount(thought)});
		});
		mood.target = ecs::kFullMood - 6.0F;
		const MoodBreakdown breakdown = moodBreakdown(rules.types, &record, mood, time.simulationSeconds());
		ASSERT_EQ(breakdown.thoughts.size(), 1U);
		EXPECT_EQ(breakdown.thoughts[0].label, "Soaked");
		EXPECT_EQ(breakdown.target, "94%");
	}

	TEST(ThoughtLogText, AmountsAndLengths) {
		EXPECT_EQ(moodAmountText(-10.0F), "-10");
		EXPECT_EQ(moodAmountText(4.5F), "+4.5");
		EXPECT_EQ(moodAmountText(0.0F), "0");
		EXPECT_EQ(gameDurationText(24.0), "1 day");
		EXPECT_EQ(gameDurationText(72.0), "3 days");
		EXPECT_EQ(gameDurationText(1.0), "1 hour");
		EXPECT_EQ(gameDurationText(6.0), "6 hours");
		EXPECT_EQ(gameDurationText(1.25), "1 hour 15 min");
		EXPECT_EQ(gameDurationText(36.5), "36 hours 30 min");
		EXPECT_EQ(gameDurationText(0.5), "30 min");
	}

	TEST(ThoughtLogText, TimeLeftIsItsLargestUnitRoundedToTheNearest) {
		EXPECT_EQ(timeLeftText(0.2), "12 min left");
		EXPECT_EQ(timeLeftText(0.001), "1 min left");
		EXPECT_EQ(timeLeftText(0.995), "1 hour left");
		EXPECT_EQ(timeLeftText(5.1), "5 hours left");
		EXPECT_EQ(timeLeftText(5.6), "6 hours left");
		EXPECT_EQ(timeLeftText(47.0), "47 hours left");
		EXPECT_EQ(timeLeftText(49.0), "2 days left");
		EXPECT_EQ(timeLeftText(60.0), "3 days left");
	}

	TEST(ThoughtLogText, Dates) {
		EXPECT_EQ(gameDateText({.day = 2, .minuteOfDay = (7 * 60) + 55}), "Day 2, 07:55");
		EXPECT_EQ(gameDateText({.day = 1, .minuteOfDay = 0}), "Day 1, 00:00");
	}

} // namespace world_sim
