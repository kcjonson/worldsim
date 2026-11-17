#include "ThoughtPruning.h"

#include "ThoughtTesting.test.h"
#include "World.h"
#include "components/Attributes.h"
#include "components/Colonist.h"
#include "systems/ThoughtSystem.h"
#include "systems/TimeSystem.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ecs::test {

	namespace {

		constexpr double kDay = kSimulationSecondsPerGameDay;

		constexpr ThoughtKeeping kKeeping{
			.pruneStartCount = 18,
			.pruneStopCount = 16,
			.recentSimulationSeconds = 10.0 * kDay,
			.trivialWeight = 1.0F,
			.normalWeight = 3.0F,
		};

		/// Builds a record by hand: each thought arrives after the last, in id order.
		class RecordBuilder {
		  public:
			Thought& add(
				ThoughtEventTypeId		  eventType,
				FeelingId				  feeling,
				double					  arrivedAt,
				ThoughtImportance		  importance = ThoughtImportance::Trivial,
				std::optional<MoodEffect> moodEffect = std::nullopt,
				ThoughtSubject			  subject = ThoughtSubject::aboutNothing()
			) {
				const ThoughtId id = thoughts.nextId++;
				thoughts.record.push_back({
					.id = id,
					.eventType = eventType,
					.subject = subject,
					.feeling = feeling,
					.moodEffect = moodEffect,
					.importance = importance,
					.arrivedAt = arrivedAt,
					.arrivedClock = {.day = static_cast<int>(arrivedAt / kDay) + 1, .minuteOfDay = static_cast<int>(id)},
					.lastRepeatedAt = arrivedAt,
					.lastRepeatedClock = {.day = static_cast<int>(arrivedAt / kDay) + 1, .minuteOfDay = static_cast<int>(id)},
				});
				Thought& added = thoughts.record.back();
				if (moodEffect.has_value()) {
					added.endedAt = arrivedAt + kDay;
				}
				return added;
			}

			Thoughts thoughts;
		};

		std::vector<ThoughtId> idsOf(const Thoughts& thoughts) {
			std::vector<ThoughtId> ids;
			for (const Thought& thought : thoughts.record) {
				ids.push_back(thought.id);
			}
			return ids;
		}

		bool has(const Thoughts& thoughts, ThoughtId id) {
			return findThought(thoughts, id) != nullptr;
		}

	} // namespace

	// The first thought of type 0 is kept as a first; the rest of type 0 differ by feeling, so none merge.
	TEST(ThoughtPruning, StartsAtTheStartSizeAndStopsAtTheStopSize) {
		RecordBuilder		builder;
		ThoughtPruneScratch scratch;
		builder.add(0, 0, 0.0);
		for (FeelingId feeling = 1; feeling < 17; ++feeling) {
			builder.add(0, feeling, static_cast<double>(feeling) * kDay);
		}
		const double now = 100.0 * kDay;
		ASSERT_EQ(builder.thoughts.record.size(), 17U);
		EXPECT_FALSE(pruneThoughts(builder.thoughts, kKeeping, now, scratch)) << "below the start size";
		EXPECT_EQ(builder.thoughts.record.size(), 17U);
		EXPECT_EQ(builder.thoughts.revision, 0U);

		builder.add(0, 17, 17.0 * kDay);
		EXPECT_TRUE(pruneThoughts(builder.thoughts, kKeeping, now, scratch));
		EXPECT_EQ(builder.thoughts.record.size(), kKeeping.pruneStopCount);
		EXPECT_EQ(builder.thoughts.revision, 1U);
		EXPECT_TRUE(std::ranges::is_sorted(idsOf(builder.thoughts))) << "survivors keep their ids, in order";

		EXPECT_FALSE(pruneThoughts(builder.thoughts, kKeeping, now, scratch)) << "back at the stop size, it waits for the start size";
		EXPECT_EQ(builder.thoughts.revision, 1U);
	}

	TEST(ThoughtPruning, EveryProtectedThoughtSurvives) {
		RecordBuilder builder;
		const double  now = 100.0 * kDay;
		const ThoughtId firstOfType = builder.add(0, 0, 0.0).id;
		const ThoughtId firstAboutSubject = builder.add(0, 20, 1.0 * kDay, ThoughtImportance::Trivial, std::nullopt, ThoughtSubject::aboutKind(7)).id;
		builder.add(0, 21, 1.5 * kDay, ThoughtImportance::Trivial, std::nullopt, ThoughtSubject::aboutKind(7));
		const ThoughtId major = builder.add(0, 1, 2.0 * kDay, ThoughtImportance::Major).id;
		Thought&		held = builder.add(0, 2, 3.0 * kDay, ThoughtImportance::Normal, MoodEffect::whileCauseHolds(-4.0F));
		held.endedAt.reset();
		const ThoughtId heldId = held.id;
		Thought&		endedLately = builder.add(0, 3, 4.0 * kDay, ThoughtImportance::Normal, MoodEffect::whileCauseHolds(-4.0F));
		endedLately.endedAt = now - (2.0 * kDay);
		const ThoughtId endedLatelyId = endedLately.id;
		Thought&		repeatedLately = builder.add(0, 4, 5.0 * kDay);
		repeatedLately.lastRepeatedAt = now - kDay;
		const ThoughtId repeatedLatelyId = repeatedLately.id;
		const ThoughtId recent = builder.add(0, 5, now - (9.0 * kDay)).id;
		for (FeelingId feeling = 6; feeling < 18; ++feeling) {
			builder.add(0, feeling, static_cast<double>(feeling) * kDay);
		}
		ASSERT_EQ(builder.thoughts.record.size(), 20U);

		ThoughtPruneScratch scratch;
		const ThoughtKeeping dropEverything{.pruneStartCount = 10, .pruneStopCount = 0, .recentSimulationSeconds = 10.0 * kDay, .trivialWeight = 1.0F, .normalWeight = 3.0F};
		ASSERT_TRUE(pruneThoughts(builder.thoughts, dropEverything, now, scratch));
		EXPECT_EQ(
			idsOf(builder.thoughts),
			(std::vector<ThoughtId>{firstOfType, firstAboutSubject, major, heldId, endedLatelyId, repeatedLatelyId, recent})
		);
	}

	TEST(ThoughtPruning, MergesTheOldestRunFirstKeepingItsCountAndDates) {
		RecordBuilder builder;
		builder.add(0, 0, 0.0);
		// Run A: type 0, feeling 1, from day 1 to day 3, its middle thought a stack of 3 that ended on day 4.
		const ThoughtId runAFirst = builder.add(0, 1, 1.0 * kDay).id;
		Thought&		stack = builder.add(0, 1, 2.0 * kDay, ThoughtImportance::Trivial, MoodEffect::timed(-2.0F, 48.0F));
		stack.count = 3;
		stack.endedAt = 4.0 * kDay;
		const Thought	runALast = builder.add(0, 1, 3.0 * kDay);
		// Run B: type 0, feeling 2, from day 5.
		const ThoughtId runBFirst = builder.add(0, 2, 5.0 * kDay).id;
		const ThoughtId runBSecond = builder.add(0, 2, 6.0 * kDay).id;
		const ThoughtId runBThird = builder.add(0, 2, 7.0 * kDay).id;
		const double	now = 100.0 * kDay;

		ThoughtPruneScratch scratch;
		Thoughts			wholeRun = builder.thoughts;
		ASSERT_TRUE(pruneThoughts(wholeRun, {.pruneStartCount = 7, .pruneStopCount = 5, .recentSimulationSeconds = 10.0 * kDay, .trivialWeight = 1.0F, .normalWeight = 3.0F}, now, scratch));
		ASSERT_EQ(idsOf(wholeRun), (std::vector<ThoughtId>{1, runAFirst, runBFirst, runBSecond, runBThird}));
		const Thought& merged = *findThought(wholeRun, runAFirst);
		EXPECT_TRUE(merged.merged);
		EXPECT_EQ(merged.count, 5U) << "1 + a stack of 3 + 1";
		EXPECT_EQ(merged.arrivedAt, 1.0 * kDay);
		EXPECT_EQ(merged.arrivedClock, (ClockTime{.day = 2, .minuteOfDay = static_cast<int>(runAFirst)}));
		EXPECT_EQ(merged.lastRepeatedAt, runALast.lastRepeatedAt);
		EXPECT_EQ(merged.lastRepeatedClock, runALast.lastRepeatedClock);
		EXPECT_EQ(merged.endedAt, 4.0 * kDay) << "the latest end among its thoughts";
		EXPECT_FALSE(findThought(wholeRun, runBFirst)->merged) << "the newer run is not needed";

		Thoughts partly = builder.thoughts;
		ASSERT_TRUE(pruneThoughts(partly, {.pruneStartCount = 7, .pruneStopCount = 4, .recentSimulationSeconds = 10.0 * kDay, .trivialWeight = 1.0F, .normalWeight = 3.0F}, now, scratch));
		EXPECT_EQ(idsOf(partly), (std::vector<ThoughtId>{1, runAFirst, runBFirst, runBThird})) << "the next run merges its oldest first";
		EXPECT_EQ(findThought(partly, runBFirst)->count, 2U);
		EXPECT_EQ(findThought(partly, runBFirst)->lastRepeatedAt, 6.0 * kDay);
	}

	TEST(ThoughtPruning, DropsTheLowestScoreFirstAndTheOldestOnATie) {
		RecordBuilder builder;
		const double  now = 200.0 * kDay;
		builder.add(0, 0, 0.0);
		// Scores: importance x (1 + size of mood effect) x (1 + count) / age in days.
		const ThoughtId normalOld = builder.add(0, 1, now - (120.0 * kDay), ThoughtImportance::Normal).id; // 3 x 1 x 2 / 120 = 0.05
		const ThoughtId trivialOld = builder.add(0, 2, now - (40.0 * kDay)).id;							  // 1 x 1 x 2 / 40 = 0.05
		const ThoughtId trivial = builder.add(0, 3, now - (20.0 * kDay)).id;								  // 1 x 1 x 2 / 20 = 0.1
		Thought&		mergedEntry = builder.add(0, 4, now - (30.0 * kDay));
		mergedEntry.count = 4;
		mergedEntry.lastRepeatedAt = now - (20.0 * kDay); // 1 x 1 x 5 / 20 = 0.25
		const ThoughtId merged = mergedEntry.id;
		const ThoughtId normal = builder.add(0, 5, now - (20.0 * kDay), ThoughtImportance::Normal).id; // 3 x 1 x 2 / 20 = 0.3
		Thought&		feltEntry = builder.add(0, 6, now - (21.0 * kDay), ThoughtImportance::Trivial, MoodEffect::timed(-4.0F, 24.0F));
		const ThoughtId felt = feltEntry.id; // ended a day later: 1 x 5 x 2 / 20 = 0.5

		const Thoughts& record = builder.thoughts;
		EXPECT_DOUBLE_EQ(thoughtPruneScore(*findThought(record, normalOld), kKeeping, now), 0.05);
		EXPECT_DOUBLE_EQ(thoughtPruneScore(*findThought(record, trivialOld), kKeeping, now), 0.05);
		EXPECT_DOUBLE_EQ(thoughtPruneScore(*findThought(record, merged), kKeeping, now), 0.25);
		EXPECT_DOUBLE_EQ(thoughtPruneScore(*findThought(record, felt), kKeeping, now), 0.5);

		const auto pruneTo = [&](std::size_t stop) {
			Thoughts			pruned = record;
			ThoughtPruneScratch scratch;
			pruneThoughts(pruned, {.pruneStartCount = 7, .pruneStopCount = stop, .recentSimulationSeconds = 10.0 * kDay, .trivialWeight = 1.0F, .normalWeight = 3.0F}, now, scratch);
			return idsOf(pruned);
		};
		EXPECT_EQ(pruneTo(6), (std::vector<ThoughtId>{1, trivialOld, trivial, merged, normal, felt})) << "a tie drops the oldest";
		EXPECT_EQ(pruneTo(5), (std::vector<ThoughtId>{1, trivial, merged, normal, felt}));
		EXPECT_EQ(pruneTo(3), (std::vector<ThoughtId>{1, normal, felt}));
		EXPECT_EQ(pruneTo(2), (std::vector<ThoughtId>{1, felt}));
	}

	TEST(ThoughtPruning, TheSameRecordAlwaysPrunesTheSameWay) {
		RecordBuilder builder;
		uint32_t	  state = 12345;
		const auto	  next = [&state](uint32_t below) {
			   state = (state * 1103515245U) + 12345U;
			   return (state >> 16U) % below;
		};
		for (int index = 0; index < 400; ++index) {
			const ThoughtSubject subject = next(3) == 0 ? ThoughtSubject::aboutKind(next(20)) : ThoughtSubject::aboutNothing();
			const std::optional<MoodEffect> effect = next(2) == 0 ? std::optional(MoodEffect::timed(-static_cast<float>(next(10) + 1), 24.0F)) : std::nullopt;
			builder.add(next(6), next(4), static_cast<double>(index) * kDay / 4.0, next(2) == 0 ? ThoughtImportance::Trivial : ThoughtImportance::Normal, effect, subject);
		}
		const ThoughtKeeping keeping{.pruneStartCount = 360, .pruneStopCount = 320, .recentSimulationSeconds = 10.0 * kDay, .trivialWeight = 1.0F, .normalWeight = 3.0F};
		const double		 now = 120.0 * kDay;

		Thoughts			first = builder.thoughts;
		Thoughts			second = builder.thoughts;
		ThoughtPruneScratch scratch;
		ASSERT_TRUE(pruneThoughts(first, keeping, now, scratch));
		ThoughtPruneScratch otherScratch;
		ASSERT_TRUE(pruneThoughts(second, keeping, now, otherScratch));
		ASSERT_EQ(first.record.size(), keeping.pruneStopCount);
		ASSERT_EQ(idsOf(first), idsOf(second));
		for (std::size_t index = 0; index < first.record.size(); ++index) {
			EXPECT_EQ(first.record[index].count, second.record[index].count);
			EXPECT_EQ(first.record[index].merged, second.record[index].merged);
			EXPECT_EQ(first.record[index].lastRepeatedAt, second.record[index].lastRepeatedAt);
		}
	}

	namespace {
		constexpr std::string_view kTypes = R"(
  <thought id="mused" label="Mused" importance="trivial">
    <feeling is="content"/>
  </thought>
  <thought id="something_reeks" label="Something reeks" importance="normal">
    <feeling is="annoyed" mood="-4" while="wetness > 0" cause="the cause"/>
    <ends thought="relieved"/>
  </thought>
  <thought id="relieved" label="Relieved" importance="trivial">
    <feeling is="relieved"/>
  </thought>
  <thought id="bottled_up" label="Bottled up" importance="normal" stacks="true">
    <feeling is="resigned" mood="-2" hours="2400"/>
  </thought>
)";
		// A record of 20 at most: pruning starts at 18 and stops at 16.
		constexpr std::string_view kSmallRecord = R"(
  <tuning maxLineLength="40" maxSubjectNameLength="12" whileCheckMinutes="15">
    <kept limit="20" pruneStartsAt="0.9" pruneStopsAt="0.8" recentDays="1" trivialWeight="1" normalWeight="3"/>
  </tuning>
)";
	} // namespace

	class ThoughtPruningSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			for (const std::string& error : rules.errors) {
				ADD_FAILURE() << error;
			}
			time = &world.registerSystem<TimeSystem>();
			thoughts = &world.registerSystem<ThoughtSystem>(rules.types, 7U);
			colonist = world.createEntity();
			world.addComponent<Colonist>(colonist, Colonist{.name = "Ada"});
			world.addComponent<Attributes>(colonist, testing::attributesAt(10.0F));
		}

		ThoughtHandle give(std::string_view type) { return thoughts->create({.colonist = colonist, .eventType = rules.type(type)}); }

		[[nodiscard]] const Thoughts& record() const { return *world.getComponent<Thoughts>(colonist); }

		testing::TestThoughtTypes rules{kTypes, kSmallRecord};
		World					  world;
		TimeSystem*				  time = nullptr;
		ThoughtSystem*			  thoughts = nullptr;
		EntityID				  colonist = kInvalidEntity;
	};

	TEST_F(ThoughtPruningSystemTest, TheTuningSetsTheSizes) {
		EXPECT_EQ(rules.types.keeping().pruneStartCount, 18U);
		EXPECT_EQ(rules.types.keeping().pruneStopCount, 16U);
		EXPECT_DOUBLE_EQ(rules.types.keeping().recentSimulationSeconds, kDay);

		testing::TestThoughtTypes tooSmall{kTypes, R"(
  <tuning maxLineLength="40" maxSubjectNameLength="12" whileCheckMinutes="15">
    <kept limit="2" pruneStartsAt="0.9" pruneStopsAt="0.8" recentDays="1" trivialWeight="1" normalWeight="3"/>
  </tuning>
)"};
		ASSERT_EQ(tooSmall.errors.size(), 1U);
		EXPECT_NE(tooSmall.errors[0].find("<kept> limit=\"2\" is too small"), std::string::npos) << tooSmall.errors[0];
	}

	// Held thoughts are never pruned or merged, and a thought that ended this frame is still there for the
	// mood contributions MoodSystem summed before it ended.
	TEST_F(ThoughtPruningSystemTest, HeldThoughtsAreUntouchedAndEveryContributionStillResolves) {
		const ThoughtHandle reeks = give("something_reeks");
		give("bottled_up");
		const ThoughtHandle bottled = give("bottled_up");
		// Every three hours for thirty days: a day's recent thoughts fit under the stop size.
		std::size_t largest = 0;
		for (int step = 0; step < 8 * 30; ++step) {
			time->skipTime(180.0F);
			world.update(0.0F);
			give("mused");
			largest = std::max(largest, record().record.size());
		}
		EXPECT_LT(largest, rules.types.keeping().pruneStartCount) << "every arrival that reached the start size pruned";
		EXPECT_TRUE(std::ranges::any_of(record().record, [](const Thought& thought) { return thought.merged; }));

		const Thought* held = findThought(record(), reeks.thought);
		ASSERT_NE(held, nullptr);
		EXPECT_TRUE(isHeld(*held));
		EXPECT_FALSE(held->merged);
		ASSERT_NE(findThought(record(), bottled.thought), nullptr);
		EXPECT_EQ(findThought(record(), bottled.thought)->count, 2U);
		EXPECT_EQ(heldMoodTotal(record()), -8.0F);

		// What MoodSystem summed this frame, before the thought ends and its <ends> thought prunes the record.
		while (record().record.size() < rules.types.keeping().pruneStartCount - 1) {
			give("mused");
		}
		std::vector<ThoughtId> contributions;
		forEachHeldThought(record(), [&contributions](const Thought& thought) { contributions.push_back(thought.id); });
		ASSERT_TRUE(thoughts->end(reeks));
		EXPECT_EQ(record().record.size(), rules.types.keeping().pruneStopCount) << "the <ends> thought pruned the record";
		for (const ThoughtId id : contributions) {
			const Thought* contributed = findThought(record(), id);
			ASSERT_NE(contributed, nullptr) << id;
			EXPECT_TRUE(contributed->moodEffect.has_value());
		}
	}

} // namespace ecs::test
