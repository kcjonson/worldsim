#include "ThoughtSystem.h"

#include "../World.h"
#include "TimeSystem.h"

#include <gtest/gtest.h>

#include <vector>

namespace ecs::test {

	namespace {
		constexpr ThoughtEventTypeId kSawNewTree = 1;
		constexpr ThoughtEventTypeId kSleptOnGround = 2;
		constexpr ThoughtEventTypeId kSomethingReeks = 3;
		constexpr ThoughtEventTypeId kBottledUp = 4;
		constexpr ThoughtEventTypeId kVented = 5;
		constexpr FeelingId			 kDelighted = 1;
		constexpr FeelingId			 kAnnoyed = 2;
		constexpr FeelingId			 kResigned = 3;
	} // namespace

	class ThoughtSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			time = &world.registerSystem<TimeSystem>();
			thoughts = &world.registerSystem<ThoughtSystem>();
			colonist = world.createEntity();
			thoughts->addEndedObserver([this](EntityID who, const Thought& thought) { ended.push_back({who, thought.id}); });
		}

		void advanceGameHours(float hours) {
			time->skipTime(hours * 60.0F);
			world.update(0.0F);
		}

		[[nodiscard]] const Thoughts& recordOf(EntityID who) const { return *world.getComponent<Thoughts>(who); }

		World					   world;
		TimeSystem*				   time = nullptr;
		ThoughtSystem*			   thoughts = nullptr;
		EntityID				   colonist = kInvalidEntity;
		std::vector<ThoughtHandle> ended;
	};

	TEST_F(ThoughtSystemTest, MomentaryThoughtIsRecordedAndNeverHeld) {
		std::vector<ThoughtArrival> arrivals;
		thoughts->addArrivedObserver([&](EntityID, const Thought&, ThoughtArrival arrival) { arrivals.push_back(arrival); });

		thoughts->create({.colonist = colonist, .eventType = kSawNewTree, .subject = ThoughtSubject::aboutEntity(42), .feeling = kDelighted});
		thoughts->create({.colonist = colonist, .eventType = kSawNewTree, .subject = ThoughtSubject::aboutEntity(42), .feeling = kDelighted});

		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 2U) << "a momentary repeat is its own record";
		EXPECT_FALSE(record.record[0].isHeld());
		EXPECT_FALSE(record.record[0].endedAt.has_value());
		EXPECT_TRUE(heldThoughts(record).empty());
		EXPECT_EQ(heldMoodTotal(record), 0.0F);
		EXPECT_EQ(arrivals, (std::vector{ThoughtArrival::New, ThoughtArrival::New}));
		EXPECT_TRUE(ended.empty());
	}

	TEST_F(ThoughtSystemTest, TimedEffectExpiresOnGameTimeAndStaysInHistory) {
		const double		arrived = time->simulationSeconds();
		const ThoughtHandle handle = thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-10.0F, 24.0F)}
		);

		advanceGameHours(23.5F);
		EXPECT_EQ(heldThoughts(recordOf(colonist)).size(), 1U);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -10.0F);

		advanceGameHours(0.5F);
		const Thoughts& record = recordOf(colonist);
		EXPECT_TRUE(heldThoughts(record).empty());
		EXPECT_EQ(heldMoodTotal(record), 0.0F);
		ASSERT_EQ(record.record.size(), 1U);
		const Thought& thought = record.record[0];
		ASSERT_TRUE(thought.endedAt.has_value());
		EXPECT_DOUBLE_EQ(*thought.endedAt, arrived + 24.0 * kSimulationSecondsPerGameHour);
		EXPECT_EQ(thought.feeling, kAnnoyed);
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].thought, handle.thought);
	}

	TEST_F(ThoughtSystemTest, CauseBoundEffectHoldsUntilEnded) {
		const ThoughtSubject toilet = ThoughtSubject::aboutEntity(7);
		thoughts->create(
			{.colonist = colonist,
			 .eventType = kSomethingReeks,
			 .subject = toilet,
			 .feeling = kAnnoyed,
			 .moodEffect = MoodEffect::whileCauseHolds(-4.0F)}
		);

		advanceGameHours(24.0F * 30.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -4.0F);

		EXPECT_FALSE(thoughts->endByCause(colonist, kSomethingReeks, ThoughtSubject::aboutEntity(8)));
		EXPECT_TRUE(thoughts->endByCause(colonist, kSomethingReeks, toilet));
		EXPECT_FALSE(thoughts->endByCause(colonist, kSomethingReeks, toilet)) << "it is no longer held";
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), 0.0F);
		EXPECT_EQ(recordOf(colonist).record.size(), 1U);
		EXPECT_EQ(ended.size(), 1U);
	}

	TEST_F(ThoughtSystemTest, HandleEndsTheThoughtItNames) {
		const ThoughtHandle handle = thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-4.0F)}
		);
		EXPECT_TRUE(thoughts->end(handle));
		EXPECT_FALSE(thoughts->end(handle));
		EXPECT_TRUE(recordOf(colonist).record[0].endedAt.has_value());
	}

	TEST_F(ThoughtSystemTest, NonStackingRepeatRefreshesTheHeldThought) {
		std::vector<ThoughtArrival> arrivals;
		thoughts->addArrivedObserver([&](EntityID, const Thought&, ThoughtArrival arrival) { arrivals.push_back(arrival); });

		const ThoughtHandle first = thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-10.0F, 24.0F)}
		);
		advanceGameHours(20.0F);
		const double		repeatedAt = time->simulationSeconds();
		const ThoughtHandle repeat = thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kResigned, .moodEffect = MoodEffect::timed(-6.0F, 24.0F)}
		);

		EXPECT_EQ(repeat.thought, first.thought);
		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 1U);
		EXPECT_EQ(record.record[0].count, 1U);
		EXPECT_EQ(record.record[0].feeling, kResigned);
		EXPECT_EQ(heldMoodTotal(record), -6.0F);
		EXPECT_EQ(arrivals, (std::vector{ThoughtArrival::New, ThoughtArrival::Repeat}));

		advanceGameHours(23.0F);
		EXPECT_EQ(heldThoughts(recordOf(colonist)).size(), 1U) << "the repeat restarted the 24 hours";
		advanceGameHours(1.0F);
		EXPECT_TRUE(heldThoughts(recordOf(colonist)).empty());
		EXPECT_DOUBLE_EQ(*recordOf(colonist).record[0].endedAt, repeatedAt + 24.0 * kSimulationSecondsPerGameHour);
	}

	TEST_F(ThoughtSystemTest, StackingRepeatCountsUp) {
		for (int i = 0; i < 3; ++i) {
			thoughts->create(
				{.colonist = colonist,
				 .eventType = kBottledUp,
				 .feeling = kResigned,
				 .moodEffect = MoodEffect::timed(-2.0F, 12.0F),
				 .stacks = true}
			);
		}
		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 1U);
		EXPECT_EQ(record.record[0].count, 3U);
		EXPECT_EQ(heldMoodTotal(record), -6.0F);
	}

	TEST_F(ThoughtSystemTest, HeldTotalSumsMixedSigns) {
		const EntityID idris = world.createEntity();
		thoughts->create({.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-10.0F, 24.0F)});
		thoughts->create(
			{.colonist = colonist,
			 .eventType = kVented,
			 .subject = ThoughtSubject::aboutColonist(idris),
			 .feeling = kDelighted,
			 .moodEffect = MoodEffect::timed(8.0F, 12.0F)}
		);
		thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-3.0F)}
		);
		thoughts->create({.colonist = colonist, .eventType = kSawNewTree, .feeling = kDelighted});

		EXPECT_EQ(heldThoughts(recordOf(colonist)).size(), 3U);
		EXPECT_FLOAT_EQ(heldMoodTotal(recordOf(colonist)), -5.0F);

		advanceGameHours(12.0F);
		EXPECT_FLOAT_EQ(heldMoodTotal(recordOf(colonist)), -13.0F) << "the +8 for 12 hours ran out";
		EXPECT_EQ(recordOf(colonist).record.size(), 4U);
	}

	TEST_F(ThoughtSystemTest, HistoryIsInArrivalOrder) {
		thoughts->create({.colonist = colonist, .eventType = kSawNewTree, .feeling = kDelighted});
		advanceGameHours(1.0F);
		thoughts->create({.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-1.0F, 1.0F)});
		advanceGameHours(1.0F);
		thoughts->create({.colonist = colonist, .eventType = kVented, .feeling = kDelighted});

		const auto& history = recordOf(colonist).record;
		ASSERT_EQ(history.size(), 3U);
		EXPECT_LT(history[0].arrivedAt, history[1].arrivedAt);
		EXPECT_LT(history[1].arrivedAt, history[2].arrivedAt);
		EXPECT_EQ(history[1].eventType, kSleptOnGround);
	}

	TEST_F(ThoughtSystemTest, ColonistsKeepSeparateThoughts) {
		const EntityID other = world.createEntity();
		thoughts->create({.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-10.0F, 24.0F)});
		thoughts->create({.colonist = other, .eventType = kSleptOnGround, .feeling = kResigned, .moodEffect = MoodEffect::timed(-4.0F, 6.0F)});

		EXPECT_EQ(recordOf(colonist).record.size(), 1U) << "the other colonist's thought did not fold into this one";
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -10.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(other)), -4.0F);

		advanceGameHours(6.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -10.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(other)), 0.0F);
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].colonist, other);
	}

} // namespace ecs::test
