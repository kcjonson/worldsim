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

		[[nodiscard]] std::vector<ThoughtId> heldIdsOf(EntityID who) const {
			std::vector<ThoughtId> held;
			forEachHeldThought(recordOf(who), [&held](const Thought& thought) { held.push_back(thought.id); });
			return held;
		}

		World					   world;
		TimeSystem*				   time = nullptr;
		ThoughtSystem*			   thoughts = nullptr;
		EntityID				   colonist = kInvalidEntity;
		std::vector<ThoughtHandle> ended;
	};

	TEST_F(ThoughtSystemTest, MomentaryThoughtIsRecordedAndNeverHeld) {
		std::vector<ThoughtArrival> arrivals;
		thoughts->addArrivedObserver([&](EntityID, const Thought&, ThoughtArrival arrival) { arrivals.push_back(arrival); });

		thoughts->create(
			{.colonist = colonist, .eventType = kSawNewTree, .subject = ThoughtSubject::aboutEntity(42), .feeling = kDelighted}
		);
		thoughts->create(
			{.colonist = colonist, .eventType = kSawNewTree, .subject = ThoughtSubject::aboutEntity(42), .feeling = kDelighted}
		);

		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 2U) << "a momentary repeat is its own record";
		EXPECT_FALSE(isHeld(record.record[0]));
		EXPECT_FALSE(record.record[0].endedAt.has_value());
		EXPECT_TRUE(heldIdsOf(colonist).empty());
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
		EXPECT_EQ(heldIdsOf(colonist).size(), 1U);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -10.0F);

		advanceGameHours(0.5F);
		const Thoughts& record = recordOf(colonist);
		EXPECT_TRUE(heldIdsOf(colonist).empty());
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
		EXPECT_FALSE(isHeld(*findThought(recordOf(colonist), handle.thought)));
		EXPECT_TRUE(heldIdsOf(colonist).empty());
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].colonist, colonist);
		EXPECT_EQ(ended[0].thought, handle.thought);

		EXPECT_FALSE(thoughts->end(handle));
		EXPECT_EQ(ended.size(), 1U);
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
		EXPECT_EQ(heldIdsOf(colonist).size(), 1U) << "the repeat restarted the 24 hours";
		advanceGameHours(1.0F);
		EXPECT_TRUE(heldIdsOf(colonist).empty());
		EXPECT_DOUBLE_EQ(*recordOf(colonist).record[0].endedAt, repeatedAt + 24.0 * kSimulationSecondsPerGameHour);
	}

	TEST_F(ThoughtSystemTest, StacksShareOneTimerAndEndAsAWhole) {
		const auto bottleUp = [&](float amount) {
			return thoughts->create(
				{.colonist = colonist,
				 .eventType = kBottledUp,
				 .feeling = kResigned,
				 .moodEffect = MoodEffect::timed(amount, 12.0F),
				 .stacks = true}
			);
		};
		const ThoughtHandle first = bottleUp(-2.0F);
		advanceGameHours(4.0F);
		bottleUp(-2.0F);
		advanceGameHours(4.0F);
		EXPECT_EQ(bottleUp(-3.0F).thought, first.thought);

		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 1U);
		EXPECT_EQ(record.record[0].count, 3U);
		EXPECT_EQ(heldMoodTotal(record), -9.0F) << "the latest repeat's amount, three times";

		advanceGameHours(11.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -9.0F) << "the last repeat restarted the 12 hours";
		EXPECT_TRUE(ended.empty());
		advanceGameHours(1.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), 0.0F);
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].thought, first.thought);
	}

	TEST_F(ThoughtSystemTest, SameEventAboutDifferentSubjectsIsHeldTwice) {
		const EntityID idris = world.createEntity();
		const EntityID mara = world.createEntity();
		const auto	   ventedAbout = [&](EntityID subject, float amount) {
			return thoughts->create(
				{.colonist = colonist,
					 .eventType = kVented,
					 .subject = ThoughtSubject::aboutColonist(subject),
					 .feeling = kDelighted,
					 .moodEffect = MoodEffect::whileCauseHolds(amount)}
			);
		};
		const ThoughtHandle aboutIdris = ventedAbout(idris, 3.0F);
		const ThoughtHandle aboutMara = ventedAbout(mara, 5.0F);

		EXPECT_NE(aboutIdris.thought, aboutMara.thought);
		EXPECT_EQ(heldIdsOf(colonist), (std::vector{aboutIdris.thought, aboutMara.thought}));
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), 8.0F);

		EXPECT_TRUE(thoughts->endByCause(colonist, kVented, ThoughtSubject::aboutColonist(idris)));
		EXPECT_EQ(heldIdsOf(colonist), (std::vector{aboutMara.thought}));
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), 5.0F);
	}

	TEST_F(ThoughtSystemTest, HeldTotalSumsMixedSigns) {
		const EntityID idris = world.createEntity();
		thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-10.0F, 24.0F)}
		);
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

		EXPECT_EQ(heldIdsOf(colonist).size(), 3U);
		EXPECT_FLOAT_EQ(heldMoodTotal(recordOf(colonist)), -5.0F);

		advanceGameHours(12.0F);
		EXPECT_FLOAT_EQ(heldMoodTotal(recordOf(colonist)), -13.0F) << "the +8 for 12 hours ran out";
		EXPECT_EQ(recordOf(colonist).record.size(), 4U);
	}

	TEST_F(ThoughtSystemTest, RepeatAfterTheThoughtEndedIsANewRecord) {
		const auto sleptOnGround = [&] {
			return thoughts->create(
				{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-1.0F, 1.0F)}
			);
		};
		const ThoughtHandle first = sleptOnGround();
		thoughts->create({.colonist = colonist, .eventType = kSawNewTree, .feeling = kDelighted});
		EXPECT_EQ(sleptOnGround().thought, first.thought) << "a repeat while held folds in";
		advanceGameHours(2.0F);
		const ThoughtHandle again = sleptOnGround();

		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 3U);
		EXPECT_EQ(again.thought, 3U);
		for (ThoughtId id = 1; id <= 3; ++id) {
			EXPECT_EQ(findThought(record, id), &record.record[id - 1]);
		}
		EXPECT_EQ(record.record[0].eventType, kSleptOnGround);
		EXPECT_TRUE(record.record[0].endedAt.has_value()) << "an ended thought stays in the record";
		EXPECT_EQ(record.record[1].eventType, kSawNewTree);
		EXPECT_EQ(record.record[2].eventType, kSleptOnGround);
		EXPECT_EQ(heldIdsOf(colonist), (std::vector{again.thought}));
	}

	TEST_F(ThoughtSystemTest, ColonistsKeepSeparateThoughts) {
		const EntityID other = world.createEntity();
		thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-10.0F, 24.0F)}
		);
		thoughts->create(
			{.colonist = other, .eventType = kSleptOnGround, .feeling = kResigned, .moodEffect = MoodEffect::timed(-4.0F, 6.0F)}
		);

		EXPECT_EQ(recordOf(colonist).record.size(), 1U) << "the other colonist's thought did not fold into this one";
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -10.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(other)), -4.0F);

		advanceGameHours(6.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -10.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(other)), 0.0F);
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].colonist, other);
	}

	TEST_F(ThoughtSystemTest, EndedObserverEndingAnotherExpiredThoughtEndsItOnce) {
		thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-1.0F, 1.0F)}
		);
		const ThoughtHandle second = thoughts->create(
			{.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::timed(2.0F, 2.0F)}
		);
		thoughts->addEndedObserver([&](EntityID, const Thought& thought) {
			if (thought.id != second.thought) {
				thoughts->end(second);
			}
		});

		advanceGameHours(3.0F);
		ASSERT_EQ(ended.size(), 2U);
		EXPECT_EQ(ended[1].thought, second.thought);
		EXPECT_TRUE(heldIdsOf(colonist).empty());
	}

	TEST_F(ThoughtSystemTest, RepeatFromAnEndedObserverDoesNotRefreshAnExpiredThought) {
		thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-1.0F, 1.0F)}
		);
		const ThoughtHandle second = thoughts->create(
			{.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::timed(2.0F, 2.0F)}
		);
		bool repeated = false;
		thoughts->addEndedObserver([&](EntityID who, const Thought&) {
			if (!repeated) {
				repeated = true;
				thoughts->create(
					{.colonist = who, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::timed(2.0F, 2.0F)}
				);
			}
		});

		advanceGameHours(3.0F);
		const double   now = time->simulationSeconds();
		const Thought* expired = findThought(recordOf(colonist), second.thought);
		ASSERT_TRUE(expired->endedAt.has_value());
		EXPECT_LE(*expired->endedAt, now);
		ASSERT_EQ(heldIdsOf(colonist).size(), 1U);
		EXPECT_NE(heldIdsOf(colonist)[0], second.thought) << "the repeat is a new thought";
	}

	TEST_F(ThoughtSystemTest, EndedObserverMayDestroyTheColonist) {
		thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-1.0F, 1.0F)}
		);
		thoughts->create({.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::timed(2.0F, 2.0F)});
		thoughts->addEndedObserver([&](EntityID who, const Thought&) { world.destroyEntity(who); });

		advanceGameHours(3.0F);
		EXPECT_EQ(ended.size(), 1U);
		EXPECT_FALSE(world.isAlive(colonist));
	}

	TEST_F(ThoughtSystemTest, StaleHandleCannotEndAnotherColonistsThought) {
		const ThoughtHandle stale = thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-4.0F)}
		);
		world.destroyEntity(colonist);
		const EntityID reuser = world.createEntity();
		ASSERT_EQ(getIndex(reuser), getIndex(colonist));
		const ThoughtHandle current = thoughts->create(
			{.colonist = reuser, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-4.0F)}
		);
		ASSERT_EQ(current.thought, stale.thought);

		EXPECT_FALSE(thoughts->end(stale));
		EXPECT_FALSE(thoughts->endByCause(colonist, kSomethingReeks, ThoughtSubject::aboutNothing()));
		EXPECT_EQ(heldMoodTotal(recordOf(reuser)), -4.0F);
		EXPECT_TRUE(ended.empty());
	}

	TEST_F(ThoughtSystemTest, CreateOnADeadColonistAttachesNothing) {
		world.destroyEntity(colonist);
		const ThoughtHandle handle = thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-4.0F)}
		);
		EXPECT_EQ(handle.colonist, kInvalidEntity);
		const EntityID reuser = world.createEntity();
		ASSERT_EQ(getIndex(reuser), getIndex(colonist));
		EXPECT_EQ(world.getComponent<Thoughts>(reuser), nullptr);
	}

	TEST_F(ThoughtSystemTest, RepeatAfterTheTimerRanOutIsANewThought) {
		const ThoughtHandle first = thoughts->create(
			{.colonist = colonist,
			 .eventType = kBottledUp,
			 .feeling = kResigned,
			 .moodEffect = MoodEffect::timed(-2.0F, 1.0F),
			 .stacks = true}
		);
		const double expiry = time->simulationSeconds() + kSimulationSecondsPerGameHour;
		time->skipTime(90.0F);
		const ThoughtHandle repeat = thoughts->create(
			{.colonist = colonist,
			 .eventType = kBottledUp,
			 .feeling = kResigned,
			 .moodEffect = MoodEffect::timed(-2.0F, 1.0F),
			 .stacks = true}
		);
		EXPECT_NE(repeat.thought, first.thought);

		world.update(0.0F);
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].thought, first.thought);
		EXPECT_DOUBLE_EQ(*findThought(recordOf(colonist), first.thought)->endedAt, expiry);
		EXPECT_EQ(findThought(recordOf(colonist), repeat.thought)->count, 1U);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -2.0F);
	}

	TEST_F(ThoughtSystemTest, EndedEventsAfterABigJumpAreInExpiryOrder) {
		const EntityID		other = world.createEntity();
		const ThoughtHandle late = thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-1.0F, 10.0F)}
		);
		const ThoughtHandle middle = thoughts->create(
			{.colonist = other, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-1.0F, 5.0F)}
		);
		const ThoughtHandle early = thoughts->create(
			{.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::timed(1.0F, 2.0F)}
		);

		advanceGameHours(24.0F);
		ASSERT_EQ(ended.size(), 3U);
		EXPECT_EQ(ended[0].colonist, early.colonist);
		EXPECT_EQ(ended[0].thought, early.thought);
		EXPECT_EQ(ended[1].colonist, middle.colonist);
		EXPECT_EQ(ended[1].thought, middle.thought);
		EXPECT_EQ(ended[2].colonist, late.colonist);
		EXPECT_EQ(ended[2].thought, late.thought);
	}

	TEST_F(ThoughtSystemTest, ObserverAddedDuringANotificationHearsTheNextOne) {
		constexpr int kAddedObservers = 8;
		constexpr int kTag = 7;
		int			  lateArrivedCalls = 0;
		int			  lateEndedCalls = 0;
		int			  arrivedTagSeen = 0;
		int			  endedTagSeen = 0;
		bool		  addedArrived = false;
		bool		  addedEnded = false;
		thoughts->addArrivedObserver([&, tag = kTag](EntityID, const Thought&, ThoughtArrival) {
			if (!addedArrived) {
				addedArrived = true;
				for (int i = 0; i < kAddedObservers; ++i) {
					thoughts->addArrivedObserver([&](EntityID, const Thought&, ThoughtArrival) { ++lateArrivedCalls; });
				}
			}
			arrivedTagSeen = tag;
		});
		thoughts->addEndedObserver([&, tag = kTag](EntityID, const Thought&) {
			if (!addedEnded) {
				addedEnded = true;
				for (int i = 0; i < kAddedObservers; ++i) {
					thoughts->addEndedObserver([&](EntityID, const Thought&) { ++lateEndedCalls; });
				}
			}
			endedTagSeen = tag;
		});

		const ThoughtHandle first = thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-1.0F)}
		);
		EXPECT_EQ(arrivedTagSeen, kTag) << "the observer being notified stays in place while observers are added";
		EXPECT_EQ(lateArrivedCalls, 0);
		const ThoughtHandle second = thoughts->create(
			{.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::whileCauseHolds(1.0F)}
		);
		EXPECT_EQ(lateArrivedCalls, kAddedObservers);

		thoughts->end(first);
		EXPECT_EQ(endedTagSeen, kTag);
		EXPECT_EQ(lateEndedCalls, 0);
		thoughts->end(second);
		EXPECT_EQ(lateEndedCalls, kAddedObservers);
	}

} // namespace ecs::test
