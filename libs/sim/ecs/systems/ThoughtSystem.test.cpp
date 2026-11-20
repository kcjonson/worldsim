#include "ThoughtSystem.h"

#include "../ColonistRules.h"
#include "../ThoughtTesting.test.h"
#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Attributes.h"
#include "../components/Colonist.h"
#include "TimeSystem.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace ecs::test {

	namespace {
		constexpr uint32_t kSeed = 20261116;

		// `wetness` is declared and answered by nothing, so a `while` on it is Unknown and holds the thought.
		constexpr std::string_view kTypes = R"(
  <thought id="saw_new_tree" label="Saw new tree" importance="trivial">
    <feeling is="delighted"/>
  </thought>
  <thought id="slept_on_ground" label="Slept on ground" importance="normal">
    <feeling is="annoyed" mood="-10" hours="24"/>
    <feeling is="resigned" when="attribute(sanity) >= 15" mood="-6" hours="24"/>
  </thought>
  <thought id="something_reeks" label="Something reeks" importance="normal">
    <feeling is="annoyed" mood="-4" while="wetness > 0" cause="the cause"/>
  </thought>
  <thought id="bottled_up" label="Bottled up" importance="normal" stacks="true">
    <feeling is="resigned" mood="-2" hours="12"/>
  </thought>
  <thought id="vented" label="Vented" importance="normal">
    <feeling is="delighted" mood="+3" while="wetness > 0" cause="the cause"/>
  </thought>
  <thought id="chatted" label="Chatted" importance="normal">
    <feeling is="delighted" mood="+8" hours="12"/>
  </thought>
  <thought id="stubbed_toe" label="Stubbed toe" importance="trivial">
    <feeling is="annoyed" mood="-1" hours="1"/>
  </thought>
  <thought id="napped" label="Napped" importance="trivial">
    <feeling is="delighted" mood="+2" hours="2"/>
  </thought>
)";
	} // namespace

	class ThoughtSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			for (const std::string& error : rules.errors) {
				ADD_FAILURE() << error;
			}
			time = &world.registerSystem<TimeSystem>();
			thoughts = &world.registerSystem<ThoughtSystem>(rules.types, kSeed);
			colonist = spawnColonist("Ada");
			thoughts->addEndedObserver([this](EntityID who, const Thought& thought) { ended.push_back({who, thought.id}); });
		}

		EntityID spawnColonist(const std::string& name) {
			const EntityID entity = world.createEntity();
			world.addComponent<Colonist>(entity, Colonist{.name = name});
			world.addComponent<Attributes>(entity, testing::attributesAt(10.0F));
			return entity;
		}

		Attributes& attributesOf(EntityID who) { return *world.getComponent<Attributes>(who); }

		ThoughtHandle give(EntityID who, std::string_view type, ThoughtSubject subject = ThoughtSubject::aboutNothing()) {
			return thoughts->create({.colonist = who, .eventType = rules.type(type), .subject = subject});
		}
		ThoughtHandle give(std::string_view type, ThoughtSubject subject = ThoughtSubject::aboutNothing()) {
			return give(colonist, type, subject);
		}

		void advanceGameHours(float hours) {
			time->skipTime(hours * 60.0F);
			world.update(0.0F);
		}

		[[nodiscard]] const Thoughts& recordOf(EntityID who) const { return *world.getComponent<Thoughts>(who); }
		[[nodiscard]] const Thought&  thoughtOf(ThoughtHandle handle) const {
			 return *findThought(recordOf(handle.colonist), handle.thought);
		}

		[[nodiscard]] std::vector<ThoughtId> heldIdsOf(EntityID who) const {
			std::vector<ThoughtId> held;
			forEachHeldThought(recordOf(who), [&held](const Thought& thought) { held.push_back(thought.id); });
			return held;
		}

		testing::TestThoughtTypes  rules{kTypes};
		World					   world;
		TimeSystem*				   time = nullptr;
		ThoughtSystem*			   thoughts = nullptr;
		EntityID				   colonist = kInvalidEntity;
		std::vector<ThoughtHandle> ended;
	};

	TEST_F(ThoughtSystemTest, MomentaryThoughtIsRecordedAndNeverHeld) {
		std::vector<ThoughtArrival> arrivals;
		thoughts->addArrivedObserver([&](EntityID, const Thought&, ThoughtArrival arrival) { arrivals.push_back(arrival); });

		give("saw_new_tree", ThoughtSubject::aboutEntity(42));
		give("saw_new_tree", ThoughtSubject::aboutEntity(42));

		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 2U) << "a momentary repeat is its own record";
		EXPECT_FALSE(isHeld(record.record[0]));
		EXPECT_FALSE(record.record[0].endedAt.has_value());
		EXPECT_EQ(record.record[0].feeling, rules.feeling("delighted"));
		EXPECT_EQ(record.record[0].importance, ThoughtImportance::Trivial);
		EXPECT_TRUE(heldIdsOf(colonist).empty());
		EXPECT_EQ(heldMoodTotal(record), 0.0F);
		EXPECT_EQ(arrivals, (std::vector{ThoughtArrival::New, ThoughtArrival::New}));
		EXPECT_TRUE(ended.empty());
	}

	TEST_F(ThoughtSystemTest, MoodEffectImportanceAndStackingComeFromTheDefinition) {
		const Thought& slept = thoughtOf(give("slept_on_ground"));
		ASSERT_TRUE(slept.moodEffect.has_value());
		EXPECT_EQ(slept.moodEffect->amount, -10.0F);
		EXPECT_EQ(slept.moodEffect->lasts, MoodEffectLasts::ForDuration);
		EXPECT_EQ(slept.moodEffect->durationGameHours, 24.0F);
		EXPECT_EQ(slept.importance, ThoughtImportance::Normal);

		const Thought& reeks = thoughtOf(give("something_reeks"));
		ASSERT_TRUE(reeks.moodEffect.has_value());
		EXPECT_EQ(reeks.moodEffect->lasts, MoodEffectLasts::WhileCauseHolds);

		give("bottled_up");
		give("slept_on_ground");
		const ThoughtHandle bottled = give("bottled_up");
		EXPECT_EQ(thoughtOf(bottled).count, 2U) << "bottled_up stacks";
		EXPECT_EQ(findThought(recordOf(colonist), 1)->count, 1U) << "slept_on_ground refreshes, never stacks";
	}

	TEST_F(ThoughtSystemTest, TimedEffectExpiresOnGameTimeAndStaysInHistory) {
		const double		arrived = time->simulationSeconds();
		const ThoughtHandle handle = give("slept_on_ground");

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
		EXPECT_EQ(thought.feeling, rules.feeling("annoyed"));
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].thought, handle.thought);
	}

	TEST_F(ThoughtSystemTest, CauseBoundEffectHoldsWhileUnknownUntilEnded) {
		const ThoughtSubject toilet = ThoughtSubject::aboutEntity(world.createEntity());
		give("something_reeks", toilet);

		advanceGameHours(24.0F * 30.0F);
		for (int check = 0; check < 8; ++check) {
			advanceGameHours(0.25F);
		}
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -4.0F) << "an Unknown `while` keeps it held through every check";

		EXPECT_FALSE(thoughts->endByCause(colonist, rules.type("something_reeks"), ThoughtSubject::aboutEntity(8)));
		EXPECT_TRUE(thoughts->endByCause(colonist, rules.type("something_reeks"), toilet));
		EXPECT_FALSE(thoughts->endByCause(colonist, rules.type("something_reeks"), toilet)) << "it is no longer held";
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), 0.0F);
		EXPECT_EQ(recordOf(colonist).record.size(), 1U);
		EXPECT_EQ(ended.size(), 1U);
	}

	TEST_F(ThoughtSystemTest, HandleEndsTheThoughtItNames) {
		const ThoughtHandle handle = give("something_reeks");
		EXPECT_TRUE(thoughts->end(handle));
		EXPECT_FALSE(isHeld(thoughtOf(handle)));
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

		const ThoughtHandle first = give("slept_on_ground");
		advanceGameHours(20.0F);
		attributesOf(colonist).sanity = 16.0F;
		const double		repeatedAt = time->simulationSeconds();
		const ThoughtHandle repeat = give("slept_on_ground");

		EXPECT_EQ(repeat.thought, first.thought);
		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 1U);
		EXPECT_EQ(record.record[0].count, 1U);
		EXPECT_EQ(record.record[0].feeling, rules.feeling("resigned")) << "the repeat's feeling";
		EXPECT_EQ(heldMoodTotal(record), -6.0F);
		EXPECT_EQ(arrivals, (std::vector{ThoughtArrival::New, ThoughtArrival::Repeat}));

		advanceGameHours(23.0F);
		EXPECT_EQ(heldIdsOf(colonist).size(), 1U) << "the repeat restarted the 24 hours";
		advanceGameHours(1.0F);
		EXPECT_TRUE(heldIdsOf(colonist).empty());
		EXPECT_DOUBLE_EQ(*recordOf(colonist).record[0].endedAt, repeatedAt + 24.0 * kSimulationSecondsPerGameHour);
	}

	TEST_F(ThoughtSystemTest, StacksShareOneTimerAndEndAsAWhole) {
		const ThoughtHandle first = give("bottled_up");
		advanceGameHours(4.0F);
		give("bottled_up");
		advanceGameHours(4.0F);
		EXPECT_EQ(give("bottled_up").thought, first.thought);

		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 1U);
		EXPECT_EQ(record.record[0].count, 3U);
		EXPECT_EQ(heldMoodTotal(record), -6.0F);

		advanceGameHours(11.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -6.0F) << "the last repeat restarted the 12 hours";
		EXPECT_TRUE(ended.empty());
		advanceGameHours(1.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), 0.0F);
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].thought, first.thought);
	}

	TEST_F(ThoughtSystemTest, SameEventAboutDifferentSubjectsIsHeldTwice) {
		const EntityID		idris = spawnColonist("Idris");
		const EntityID		mara = spawnColonist("Mara");
		const ThoughtHandle aboutIdris = give("vented", ThoughtSubject::aboutColonist(idris));
		const ThoughtHandle aboutMara = give("vented", ThoughtSubject::aboutColonist(mara));

		EXPECT_NE(aboutIdris.thought, aboutMara.thought);
		EXPECT_EQ(heldIdsOf(colonist), (std::vector{aboutIdris.thought, aboutMara.thought}));
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), 6.0F);

		EXPECT_TRUE(thoughts->endByCause(colonist, rules.type("vented"), ThoughtSubject::aboutColonist(idris)));
		EXPECT_EQ(heldIdsOf(colonist), (std::vector{aboutMara.thought}));
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), 3.0F);
	}

	TEST_F(ThoughtSystemTest, HeldTotalSumsMixedSigns) {
		const EntityID idris = spawnColonist("Idris");
		give("slept_on_ground");
		give("chatted", ThoughtSubject::aboutColonist(idris));
		give("something_reeks");
		give("saw_new_tree");

		EXPECT_EQ(heldIdsOf(colonist).size(), 3U);
		EXPECT_FLOAT_EQ(heldMoodTotal(recordOf(colonist)), -6.0F);

		advanceGameHours(12.0F);
		EXPECT_FLOAT_EQ(heldMoodTotal(recordOf(colonist)), -14.0F) << "the +8 for 12 hours ran out";
		EXPECT_EQ(recordOf(colonist).record.size(), 4U);
	}

	TEST_F(ThoughtSystemTest, RepeatAfterTheThoughtEndedIsANewRecord) {
		const ThoughtHandle first = give("stubbed_toe");
		give("saw_new_tree");
		EXPECT_EQ(give("stubbed_toe").thought, first.thought) << "a repeat while held folds in";
		advanceGameHours(2.0F);
		const ThoughtHandle again = give("stubbed_toe");

		const Thoughts& record = recordOf(colonist);
		ASSERT_EQ(record.record.size(), 3U);
		EXPECT_EQ(again.thought, 3U);
		for (ThoughtId id = 1; id <= 3; ++id) {
			EXPECT_EQ(findThought(record, id), &record.record[id - 1]);
		}
		EXPECT_EQ(record.record[0].eventType, rules.type("stubbed_toe"));
		EXPECT_TRUE(record.record[0].endedAt.has_value()) << "an ended thought stays in the record";
		EXPECT_EQ(record.record[1].eventType, rules.type("saw_new_tree"));
		EXPECT_EQ(record.record[2].eventType, rules.type("stubbed_toe"));
		EXPECT_EQ(heldIdsOf(colonist), (std::vector{again.thought}));
	}

	TEST_F(ThoughtSystemTest, ColonistsKeepSeparateThoughts) {
		const EntityID other = spawnColonist("Mara");
		give("slept_on_ground");
		give(other, "napped");

		EXPECT_EQ(recordOf(colonist).record.size(), 1U);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -10.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(other)), 2.0F);

		advanceGameHours(2.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -10.0F);
		EXPECT_EQ(heldMoodTotal(recordOf(other)), 0.0F);
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].colonist, other);
	}

	TEST_F(ThoughtSystemTest, EndedObserverEndingAnotherExpiredThoughtEndsItOnce) {
		give("stubbed_toe");
		const ThoughtHandle second = give("napped");
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
		give("stubbed_toe");
		const ThoughtHandle second = give("napped");
		bool				repeated = false;
		thoughts->addEndedObserver([&](EntityID who, const Thought&) {
			if (!repeated) {
				repeated = true;
				give(who, "napped");
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
		give("stubbed_toe");
		give("napped");
		thoughts->addEndedObserver([&](EntityID who, const Thought&) { world.destroyEntity(who); });

		advanceGameHours(3.0F);
		EXPECT_EQ(ended.size(), 1U);
		EXPECT_FALSE(world.isAlive(colonist));
	}

	TEST_F(ThoughtSystemTest, StaleHandleCannotEndAnotherColonistsThought) {
		const ThoughtHandle stale = give("something_reeks");
		world.destroyEntity(colonist);
		const EntityID reuser = spawnColonist("Mara");
		ASSERT_EQ(getIndex(reuser), getIndex(colonist));
		const ThoughtHandle current = give(reuser, "something_reeks");
		ASSERT_EQ(current.thought, stale.thought);

		EXPECT_FALSE(thoughts->end(stale));
		EXPECT_FALSE(thoughts->endByCause(colonist, rules.type("something_reeks"), ThoughtSubject::aboutNothing()));
		EXPECT_EQ(heldMoodTotal(recordOf(reuser)), -4.0F);
		EXPECT_TRUE(ended.empty());
	}

	TEST_F(ThoughtSystemTest, CreateOnADeadColonistAttachesNothing) {
		world.destroyEntity(colonist);
		const ThoughtHandle handle = give("something_reeks");
		EXPECT_EQ(handle.colonist, kInvalidEntity);
		const EntityID reuser = world.createEntity();
		ASSERT_EQ(getIndex(reuser), getIndex(colonist));
		EXPECT_EQ(world.getComponent<Thoughts>(reuser), nullptr);
	}

	TEST_F(ThoughtSystemTest, RepeatAfterTheTimerRanOutIsANewThought) {
		const ThoughtHandle first = give("stubbed_toe");
		const double		expiry = time->simulationSeconds() + kSimulationSecondsPerGameHour;
		time->skipTime(90.0F);
		const ThoughtHandle repeat = give("stubbed_toe");
		EXPECT_NE(repeat.thought, first.thought);

		world.update(0.0F);
		ASSERT_EQ(ended.size(), 1U);
		EXPECT_EQ(ended[0].thought, first.thought);
		EXPECT_DOUBLE_EQ(*findThought(recordOf(colonist), first.thought)->endedAt, expiry);
		EXPECT_EQ(findThought(recordOf(colonist), repeat.thought)->count, 1U);
		EXPECT_EQ(heldMoodTotal(recordOf(colonist)), -1.0F);
	}

	TEST_F(ThoughtSystemTest, EndedEventsAfterABigJumpAreInExpiryOrder) {
		const EntityID		other = spawnColonist("Mara");
		const ThoughtHandle late = give("chatted");
		const ThoughtHandle middle = give(other, "napped");
		const ThoughtHandle early = give("stubbed_toe");

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

		const ThoughtHandle first = give("something_reeks");
		EXPECT_EQ(arrivedTagSeen, kTag) << "the observer being notified stays in place while observers are added";
		EXPECT_EQ(lateArrivedCalls, 0);
		const ThoughtHandle second = give("vented");
		EXPECT_EQ(lateArrivedCalls, kAddedObservers);

		thoughts->end(first);
		EXPECT_EQ(endedTagSeen, kTag);
		EXPECT_EQ(lateEndedCalls, 0);
		thoughts->end(second);
		EXPECT_EQ(lateEndedCalls, kAddedObservers);
	}

	TEST_F(ThoughtSystemTest, UnknownEventTypeCreatesNothing) {
		const ThoughtHandle handle =
			thoughts->create({.colonist = colonist, .eventType = static_cast<ThoughtEventTypeId>(rules.types.typeCount())});
		EXPECT_EQ(handle.colonist, kInvalidEntity);
		EXPECT_EQ(world.getComponent<Thoughts>(colonist), nullptr);
	}

	// --- feelings ---

	TEST_F(ThoughtSystemTest, FeelingFollowsAnAttributeAndFallsBack) {
		EXPECT_EQ(thoughtOf(give("slept_on_ground")).feeling, rules.feeling("annoyed")) << "sanity 10: the fallback";
		const EntityID calm = spawnColonist("Mara");
		attributesOf(calm).sanity = 18.0F;
		const Thought& calmThought = thoughtOf(give(calm, "slept_on_ground"));
		EXPECT_EQ(calmThought.feeling, rules.feeling("resigned"));
		EXPECT_EQ(calmThought.moodEffect->amount, -6.0F) << "the feeling sets the size of the effect";
	}

	TEST(ThoughtFeelingSelection, MoreTermsWinAndFileOrderBreaksTies) {
		testing::TestThoughtTypes rules(R"(
  <thought id="smashed_chair" label="Smashed chair" importance="normal">
    <feeling is="content"/>
    <feeling is="annoyed" when="attribute(civility) &lt; 8"/>
    <feeling is="miserable" when="attribute(sanity) &lt; 8"/>
    <feeling is="furious" when="attribute(civility) &lt; 8 and attribute(sanity) &lt; 8"/>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World world;
		world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const auto	   feelingFor = [&](float civility, float sanity) {
			const EntityID who = world.createEntity();
			world.addComponent<Attributes>(who, testing::attributesAt(10.0F, {{&Attributes::civility, civility}, {&Attributes::sanity, sanity}}));
			const ThoughtHandle handle = thoughts.create({.colonist = who, .eventType = rules.type("smashed_chair")});
			return findThought(*world.getComponent<Thoughts>(who), handle.thought)->feeling;
		};
		EXPECT_EQ(feelingFor(5.0F, 5.0F), rules.feeling("furious")) << "two terms beat one";
		EXPECT_EQ(feelingFor(5.0F, 12.0F), rules.feeling("annoyed"));
		EXPECT_EQ(feelingFor(12.0F, 5.0F), rules.feeling("miserable"));
		EXPECT_EQ(feelingFor(12.0F, 12.0F), rules.feeling("content")) << "the one with no condition is the fallback";

		// Both one-term feelings hold: the one written first wins.
		testing::TestThoughtTypes tieRules(R"(
  <thought id="tie" label="Tie" importance="normal">
    <feeling is="miserable" when="attribute(sanity) &lt; 8"/>
    <feeling is="annoyed" when="attribute(civility) &lt; 8"/>
  </thought>
)");
		World					  tieWorld;
		tieWorld.registerSystem<TimeSystem>();
		ThoughtSystem& tieThoughts = tieWorld.registerSystem<ThoughtSystem>(tieRules.types, kSeed);
		const EntityID tieColonist = tieWorld.createEntity();
		tieWorld.addComponent<Attributes>(tieColonist, testing::attributesAt(10.0F, {{&Attributes::civility, 5.0F}, {&Attributes::sanity, 5.0F}}));
		const ThoughtHandle tie = tieThoughts.create({.colonist = tieColonist, .eventType = tieRules.type("tie")});
		EXPECT_EQ(findThought(*tieWorld.getComponent<Thoughts>(tieColonist), tie.thought)->feeling, tieRules.feeling("miserable"));
	}

	TEST(ThoughtFeelingSelection, UnknownDoesNotHoldAndNoFeelingCreatesNothing) {
		testing::TestThoughtTypes rules(R"(
  <thought id="cold_snap" label="Cold snap" importance="normal">
    <feeling is="content"/>
    <feeling is="miserable" when="wetness > 0.5" mood="-5" hours="2"/>
  </thought>
  <thought id="only_when_calm" label="Only when calm" importance="normal">
    <feeling is="content" when="attribute(sanity) >= 15" mood="+2" hours="2"/>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World world;
		world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const EntityID who = world.createEntity();
		world.addComponent<Attributes>(who, testing::attributesAt(10.0F));

		const ThoughtHandle cold = thoughts.create({.colonist = who, .eventType = rules.type("cold_snap")});
		EXPECT_EQ(findThought(*world.getComponent<Thoughts>(who), cold.thought)->feeling, rules.feeling("content"))
			<< "Unknown is not a match";

		const ThoughtHandle none = thoughts.create({.colonist = who, .eventType = rules.type("only_when_calm")});
		EXPECT_EQ(none.colonist, kInvalidEntity);
		EXPECT_EQ(world.getComponent<Thoughts>(who)->record.size(), 1U) << "no feeling applied, so nothing was recorded";
	}

	// --- while effects and endings ---

	TEST(ThoughtWhileEffect, EndsWhenItsConditionTurnsFalseStaysOnUnknownAndCreatesItsEnding) {
		testing::TestThoughtTypes rules(R"(
  <thought id="steady" label="Steady" importance="normal">
    <feeling is="content" mood="+4" while="attribute(sanity) >= 10" cause="the cause"/>
    <ends thought="wobbly"/>
  </thought>
  <thought id="wobbly" label="Wobbly" importance="trivial">
    <feeling is="relieved"/>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World		   world;
		TimeSystem&	   time = world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const EntityID steady = world.createEntity();
		world.addComponent<Attributes>(steady, testing::attributesAt(10.0F));
		const EntityID unknowable = world.createEntity();
		world.addComponent<Attributes>(unknowable, testing::attributesAt(10.0F));

		const ThoughtHandle held = thoughts.create({.colonist = steady, .eventType = rules.type("steady")});
		thoughts.create({.colonist = unknowable, .eventType = rules.type("steady")});
		world.removeComponent<Attributes>(unknowable);

		world.getComponent<Attributes>(steady)->sanity = 4.0F;
		time.skipTime(14.0F);
		world.update(0.0F);
		EXPECT_TRUE(isHeld(*findThought(*world.getComponent<Thoughts>(steady), held.thought))) << "not checked again until 15 game minutes";

		time.skipTime(1.0F);
		world.update(0.0F);
		const Thoughts& record = *world.getComponent<Thoughts>(steady);
		ASSERT_EQ(record.record.size(), 2U);
		EXPECT_FALSE(isHeld(record.record[0]));
		EXPECT_DOUBLE_EQ(*record.record[0].endedAt, time.simulationSeconds());
		EXPECT_EQ(record.record[1].eventType, rules.type("wobbly")) << "its <ends> thought";

		for (int check = 0; check < 10; ++check) {
			time.skipTime(15.0F);
			world.update(0.0F);
		}
		EXPECT_EQ(heldMoodTotal(*world.getComponent<Thoughts>(unknowable)), 4.0F) << "Unknown keeps it held";
	}

	TEST(ThoughtWhileEffect, AnExplicitEndAndATimedEndAlsoCreateTheEnding) {
		testing::TestThoughtTypes rules(R"(
  <thought id="soaked" label="Soaked" importance="normal">
    <feeling is="miserable" mood="-6" while="wetness > 0.6" cause="the cause"/>
    <ends thought="dried_off"/>
  </thought>
  <thought id="sunburnt" label="Sunburnt" importance="normal">
    <feeling is="annoyed" mood="-2" hours="1"/>
    <ends thought="dried_off"/>
  </thought>
  <thought id="dried_off" label="Dried off" importance="trivial">
    <feeling is="relieved" mood="+1" hours="2"/>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World				 world;
		TimeSystem&			 time = world.registerSystem<TimeSystem>();
		ThoughtSystem&		 thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const EntityID		 who = world.createEntity();
		const ThoughtSubject river = ThoughtSubject::aboutPlace({3.0F, 4.0F});

		thoughts.create({.colonist = who, .eventType = rules.type("soaked"), .subject = river});
		ASSERT_TRUE(thoughts.endByCause(who, rules.type("soaked"), river));
		const Thoughts& record = *world.getComponent<Thoughts>(who);
		ASSERT_EQ(record.record.size(), 2U);
		EXPECT_EQ(record.record[1].eventType, rules.type("dried_off"));
		EXPECT_EQ(record.record[1].subject, river) << "about what the ended thought was about";

		thoughts.create({.colonist = who, .eventType = rules.type("sunburnt")});
		time.skipTime(60.0F);
		world.update(0.0F);
		ASSERT_EQ(world.getComponent<Thoughts>(who)->record.size(), 4U);
		EXPECT_EQ(world.getComponent<Thoughts>(who)->record[3].eventType, rules.type("dried_off"));
	}

	// --- lines ---

	TEST(ThoughtLines, PickTheMostSpecificLineForTheFeelingAndCondition) {
		testing::TestThoughtTypes rules(R"(
  <thought id="soaked" label="Soaked" importance="normal">
    <feeling is="miserable"/>
    <feeling is="furious" when="attribute(civility) &lt; 6"/>
    <line>I'm soaked through.</line>
    <line feeling="furious">One more minute of this...</line>
    <line feeling="furious" when="attribute(strength) >= 15">I will fight this cloud.</line>
  </thought>
  <thought id="quiet" label="Quiet" importance="trivial">
    <feeling is="content"/>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World world;
		world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const auto	   lineFor = [&](Attributes attributes, std::string_view type) {
			const EntityID who = world.createEntity();
			world.addComponent<Attributes>(who, attributes);
			const ThoughtHandle handle = thoughts.create({.colonist = who, .eventType = rules.type(type)});
			return renderThoughtLine(rules.types, *world.getComponent<Thoughts>(who), *findThought(*world.getComponent<Thoughts>(who), handle.thought));
		};
		EXPECT_EQ(lineFor(testing::attributesAt(10.0F), "soaked"), "I'm soaked through.");
		EXPECT_EQ(lineFor(testing::attributesAt(10.0F, {{&Attributes::civility, 3.0F}}), "soaked"), "One more minute of this...");
		EXPECT_EQ(lineFor(testing::attributesAt(10.0F, {{&Attributes::strength, 18.0F}, {&Attributes::civility, 3.0F}}), "soaked"), "I will fight this cloud.");
		EXPECT_EQ(lineFor(testing::attributesAt(10.0F, {{&Attributes::strength, 18.0F}}), "soaked"), "I'm soaked through.") << "the strength line is the furious one's";
		EXPECT_EQ(lineFor(testing::attributesAt(10.0F), "quiet"), "") << "many thoughts have no line";
	}

	TEST(ThoughtLines, AvoidTheLastLineAndAreDeterministicUnderASeed) {
		constexpr std::string_view kQuips = R"(
  <thought id="idle" label="Idle" importance="trivial">
    <feeling is="content"/>
    <line>My butt itches.</line>
    <line>Is that cloud a horse?</line>
    <line>I could eat a whole tree.</line>
  </thought>
)";
		const auto				   pickLines = [&](uint32_t seed) {
			testing::TestThoughtTypes rules(kQuips);
			EXPECT_TRUE(rules.errors.empty());
			World world;
			world.registerSystem<TimeSystem>();
			ThoughtSystem&		  thoughts = world.registerSystem<ThoughtSystem>(rules.types, seed);
			const EntityID		  who = world.createEntity();
			std::vector<uint16_t> lines;
			for (int pick = 0; pick < 30; ++pick) {
				const ThoughtHandle handle = thoughts.create({.colonist = who, .eventType = rules.type("idle")});
				lines.push_back(*findThought(*world.getComponent<Thoughts>(who), handle.thought)->line);
			}
			return lines;
		};
		const std::vector<uint16_t> lines = pickLines(kSeed);
		for (std::size_t index = 1; index < lines.size(); ++index) {
			EXPECT_NE(lines[index], lines[index - 1]) << "pick " << index << " repeated the last line";
		}
		EXPECT_EQ(std::set<uint16_t>(lines.begin(), lines.end()).size(), 3U) << "every line gets used";
		EXPECT_EQ(pickLines(kSeed), lines) << "the same seed picks the same lines";
		EXPECT_NE(pickLines(kSeed + 1), lines);
	}

	TEST(ThoughtLines, SubjectIsRenderedByNameAndALineNamingNoOneIsSkipped) {
		testing::TestThoughtTypes rules(R"(
  <thought id="talked" label="Talked" importance="normal">
    <feeling is="content"/>
    <line>{subject} gets me.</line>
  </thought>
  <thought id="admired" label="Admired" importance="normal">
    <feeling is="content"/>
    <line>Nice {subject}.</line>
    <line>Huh.</line>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World world;
		world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const EntityID ada = world.createEntity();
		const EntityID idris = world.createEntity();
		world.addComponent<Colonist>(idris, Colonist{.name = "Idris"});
		// No asset is defined by this name, so the entity is named by it.
		const EntityID statue = world.createEntity();
		world.addComponent<Appearance>(statue, Appearance{.defName = "TestStatue"});

		const auto lineOf = [&](std::string_view type, ThoughtSubject subject) {
			const ThoughtHandle handle = thoughts.create({.colonist = ada, .eventType = rules.type(type), .subject = subject});
			return renderThoughtLine(rules.types, *world.getComponent<Thoughts>(ada), *findThought(*world.getComponent<Thoughts>(ada), handle.thought));
		};
		EXPECT_EQ(lineOf("talked", ThoughtSubject::aboutColonist(idris)), "Idris gets me.");
		EXPECT_EQ(lineOf("talked", ThoughtSubject::aboutNothing()), "") << "the only line names a subject there is none of";

		std::set<std::string> aboutStatue;
		for (int pick = 0; pick < 2; ++pick) {
			aboutStatue.insert(lineOf("admired", ThoughtSubject::aboutEntity(statue)));
		}
		EXPECT_EQ(aboutStatue, (std::set<std::string>{"Nice TestStatue.", "Huh."}));
		for (int pick = 0; pick < 5; ++pick) {
			EXPECT_EQ(lineOf("admired", ThoughtSubject::aboutPlace({1.0F, 1.0F})), "Huh.") << "a place has no name to fill in";
		}
	}

	TEST(ThoughtLines, TheSubjectsNameOutlivesTheSubjectAndAnUnnamedColonistIsNotNamedByHisDefinition) {
		testing::TestThoughtTypes rules(R"(
  <thought id="talked" label="Talked" importance="normal">
    <feeling is="content"/>
    <line>{subject} gets me.</line>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World world;
		world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const EntityID ada = world.createEntity();
		const EntityID idris = world.createEntity();
		world.addComponent<Colonist>(idris, Colonist{.name = "Idris"});
		const EntityID nameless = world.createEntity();
		world.addComponent<Colonist>(nameless, Colonist{});
		world.addComponent<Appearance>(nameless, Appearance{.defName = "Human"});

		const ThoughtHandle aboutIdris = thoughts.create({.colonist = ada, .eventType = rules.type("talked"), .subject = ThoughtSubject::aboutColonist(idris)});
		world.destroyEntity(idris);
		EXPECT_EQ(renderThoughtLine(rules.types, *world.getComponent<Thoughts>(ada), *findThought(*world.getComponent<Thoughts>(ada), aboutIdris.thought)), "Idris gets me.");

		const ThoughtHandle aboutNameless =
			thoughts.create({.colonist = ada, .eventType = rules.type("talked"), .subject = ThoughtSubject::aboutColonist(nameless)});
		const Thought& unnamed = *findThought(*world.getComponent<Thoughts>(ada), aboutNameless.thought);
		EXPECT_FALSE(unnamed.line.has_value()) << "a colonist with no name has none to fill in, not his definition's";
	}

	TEST(ThoughtWhileEffect, EndsAtItsNextCheckOnceItsSubjectIsGone) {
		testing::TestThoughtTypes rules(R"(
  <thought id="reeks" label="Reeks" importance="normal">
    <feeling is="annoyed" mood="-4" while="wetness > 0" cause="the cause"/>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World		   world;
		TimeSystem&	   time = world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const EntityID who = world.createEntity();
		const EntityID toilet = world.createEntity();
		const EntityID kept = world.createEntity();

		const ThoughtHandle aboutToilet = thoughts.create({.colonist = who, .eventType = rules.type("reeks"), .subject = ThoughtSubject::aboutEntity(toilet)});
		const ThoughtHandle aboutKept = thoughts.create({.colonist = who, .eventType = rules.type("reeks"), .subject = ThoughtSubject::aboutEntity(kept)});
		world.destroyEntity(toilet);
		const EntityID reused = world.createEntity();
		ASSERT_EQ(getIndex(reused), getIndex(toilet)) << "the slot is reused under a new generation";

		time.skipTime(15.0F);
		world.update(0.0F);
		const Thoughts& record = *world.getComponent<Thoughts>(who);
		EXPECT_FALSE(isHeld(*findThought(record, aboutToilet.thought))) << "its subject is gone, so its cause has passed";
		EXPECT_TRUE(isHeld(*findThought(record, aboutKept.thought))) << "Unknown still keeps one whose subject is there";
	}

	TEST(ThoughtRepeats, ARepeatWithNoMoodEffectEndsTheHeldThought) {
		testing::TestThoughtTypes rules(R"(
  <thought id="slept_on_ground" label="Slept on ground" importance="normal">
    <feeling is="annoyed" mood="-10" hours="24"/>
    <feeling is="content" when="attribute(sanity) >= 15"/>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World		   world;
		world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		std::vector<ThoughtId> ended;
		thoughts.addEndedObserver([&](EntityID, const Thought& thought) { ended.push_back(thought.id); });
		const EntityID who = world.createEntity();
		world.addComponent<Attributes>(who, testing::attributesAt(10.0F));

		const ThoughtHandle held = thoughts.create({.colonist = who, .eventType = rules.type("slept_on_ground")});
		world.getComponent<Attributes>(who)->sanity = 16.0F;
		const ThoughtHandle repeat = thoughts.create({.colonist = who, .eventType = rules.type("slept_on_ground")});

		const Thoughts& record = *world.getComponent<Thoughts>(who);
		ASSERT_EQ(record.record.size(), 2U);
		EXPECT_NE(repeat.thought, held.thought) << "the repeat is its own momentary record";
		EXPECT_FALSE(isHeld(*findThought(record, held.thought))) << "the colonist no longer feels it";
		EXPECT_EQ(heldMoodTotal(record), 0.0F);
		EXPECT_EQ(ended, (std::vector{held.thought}));
		EXPECT_EQ(findThought(record, repeat.thought)->feeling, rules.feeling("content"));
	}

	TEST(ThoughtRepeats, ARepeatTakesTheNewFeelingAndAmountAcrossTheWholeStack) {
		testing::TestThoughtTypes rules(R"(
  <thought id="bottled_up" label="Bottled up" importance="normal" stacks="true">
    <feeling is="resigned" mood="-2" hours="12"/>
    <feeling is="furious" when="attribute(civility) &lt; 6" mood="-5" hours="12"/>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World		   world;
		world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const EntityID who = world.createEntity();
		world.addComponent<Attributes>(who, testing::attributesAt(10.0F));

		const ThoughtHandle first = thoughts.create({.colonist = who, .eventType = rules.type("bottled_up")});
		EXPECT_EQ(heldMoodTotal(*world.getComponent<Thoughts>(who)), -2.0F);
		world.getComponent<Attributes>(who)->civility = 3.0F;
		EXPECT_EQ(thoughts.create({.colonist = who, .eventType = rules.type("bottled_up")}).thought, first.thought);

		const Thought& stack = *findThought(*world.getComponent<Thoughts>(who), first.thought);
		EXPECT_EQ(stack.count, 2U);
		EXPECT_EQ(stack.feeling, rules.feeling("furious"));
		ASSERT_TRUE(stack.moodEffect.has_value());
		EXPECT_EQ(stack.moodEffect->amount, -5.0F);
		EXPECT_EQ(heldMoodTotal(*world.getComponent<Thoughts>(who)), -10.0F) << "the new amount counts for every repeat";
	}

	// --- definitions ---

	namespace {
		bool mentions(const std::vector<std::string>& errors, std::string_view text) {
			return std::ranges::any_of(errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
		}
	} // namespace

	TEST(ThoughtTypeDefinitions, EachMistakeIsReported) {
		const auto errorsFor = [](std::string_view body) {
			return testing::TestThoughtTypes(body).errors;
		};

		EXPECT_TRUE(
			mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="giddy"/></thought>)"), "no feeling is called 'giddy'")
		);
		EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><line>Hi.</line></thought>)"), "lists no feeling"));
		EXPECT_TRUE(mentions(
			errorsFor(R"x(<thought id="a" label="A" importance="normal"><feeling is="content" when="trait(nonesuch)"/></thought>)x"), "nonesuch"
		));
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="content" when="attribute(sanity) >"/></thought>)"), "at column"
		));
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="content" mood="-1" while="sparkle > 1" cause="the cause"/></thought>)"), "while: "
		));
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="content"/><line when="nonsense(">Hi.</line></thought>)"),
			"line 1 when"
		));
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="content"/><line feeling="furious">Grr.</line></thought>)"),
			"never feels 'furious'"
		));
		EXPECT_TRUE(mentions(
			errorsFor(
				R"(<thought id="a" label="A" importance="normal"><feeling is="content"/><line>This line goes on and on and on and on, far too long.</line></thought>)"
			),
			"more than the 40"
		));
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="content"/><ends thought="b"/></thought>)"),
			"which is no thought type"
		));
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="content"/><source trigger="needFalls" need="Mojo" threshold="seek"/></thought>)"),
			"source 1: no need is called 'Mojo'"
		));
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="content"/><source trigger="actionFinishes" action="Juggle"/></thought>)"),
			"source 1: no action is called 'Juggle'"
		));
		const std::vector<std::string> cycle = errorsFor(R"(
  <thought id="a" label="A" importance="normal"><feeling is="content"/><ends thought="b"/></thought>
  <thought id="b" label="B" importance="normal"><feeling is="content"/><ends thought="c"/></thought>
  <thought id="c" label="C" importance="normal"><feeling is="content"/><ends thought="a"/></thought>
)");
		ASSERT_EQ(cycle.size(), 1U) << "a cycle is reported once";
		EXPECT_TRUE(mentions(cycle, "makes a cycle (a -> b -> c -> a)"));
	}

	TEST(ThoughtTypeDefinitions, ConditionErrorsGiveTheirPosition) {
		const std::vector<std::string> errors =
			testing::TestThoughtTypes(
				R"(<thought id="a" label="A" importance="normal"><feeling is="content" when="attribute(sanity) >= "/></thought>)"
			)
				.errors;
		ASSERT_EQ(errors.size(), 1U);
		EXPECT_TRUE(mentions(errors, "thoughts.xml: thought 'a' feeling 1 ('content') when: ")) << errors.front();
		EXPECT_TRUE(mentions(errors, "at column 22 in 'attribute(sanity) >= '")) << errors.front();
	}

	TEST(ThoughtTypeDefinitions, AnUnknownSimNameIsAnErrorNamingTheFileTheThoughtAndTheNames) {
		const std::tuple<std::string, std::string, std::string> cases[] = {
			{"need(Hungr) &lt; 30", "'Hungr' is not accepted as argument 1 of 'need' (expected one of", "hunger"},
			{"attribute(luck) > 1", "'luck' is not accepted as argument 1 of 'attribute' (expected one of", ""},
			{"current_task(dance)", "'dance' is not accepted as argument 1 of 'current_task' (expected one of", ""},
		};
		for (const auto& [condition, message, listed] : cases) {
			const std::vector<std::string> errors =
				testing::TestThoughtTypes(
					"", R"(<thought id="modded" label="Modded" importance="normal"><feeling is="content" when=")" + condition + R"("/></thought>)"
				)
					.errors;
			ASSERT_EQ(errors.size(), 1U) << condition;
			EXPECT_TRUE(mentions(errors, "mods/later.xml: thought 'modded' feeling 1 ('content') when: ")) << errors.front();
			EXPECT_TRUE(mentions(errors, message)) << errors.front();
			EXPECT_TRUE(mentions(errors, listed)) << errors.front();
		}
	}

	TEST(ThoughtTypeDefinitions, ACreatureFolderErrorStopsTheRulesAndIsReportedOnce) {
		const std::filesystem::path root =
			std::filesystem::temp_directory_path() / ("worldsim-rules-creatures-" + std::to_string(std::random_device{}()));
		const std::filesystem::path folder = root / "creatures" / "Human";
		std::filesystem::create_directories(folder);
		std::filesystem::copy(testing::repoHuman(), folder, std::filesystem::copy_options::recursive);
		std::ofstream(folder / "herd.xml") << "<Herd/>";

		assets::CreatureConfigFiles human = assets::creatureConfigFiles(folder);
		const ColonistRulesLoad		load = loadColonistRules(testing::repoConfig(), human);
		ASSERT_EQ(load.errors.size(), 1U) << ::testing::PrintToString(load.errors);
		EXPECT_EQ(
			load.errors.front(),
			"[creatures] herd.xml: the root element <Herd> is not one a creature folder holds (<Mood>, <Memory>, <Traits>, <Attributes>, "
			"<Needs>)"
		);
		EXPECT_TRUE(load.rules.traits->traits().empty()) << "the traits never load";
		EXPECT_TRUE(loadColonistRules(testing::repoConfig(), human).errors.empty()) << "reported once, by the first loader";
		std::filesystem::remove_all(root);
	}

	TEST(ThoughtTypeDefinitions, TheShippedRulesLoadClean) {
		assets::CreatureConfigFiles human = assets::creatureConfigFiles(testing::repoHuman());
		const ColonistRulesLoad		load = loadColonistRules(testing::repoConfig(), human);
		for (const std::string& error : load.errors) {
			ADD_FAILURE() << error;
		}
		const ThoughtTypes& types = *load.rules.thoughtTypes;
		for (const std::string_view name :
			 {"ate_good_meal", "slept_on_ground", "accident_in_public", "talked_with_friend", "idle_musing", "bottled_up"}) {
			EXPECT_TRUE(types.findType(name).has_value()) << name;
		}
		EXPECT_TRUE(types.type(*types.findType("bottled_up")).stacks);
		EXPECT_TRUE(std::ranges::any_of(types.sources(), [](const ThoughtSource& source) { return source.trigger == ThoughtTrigger::Idle; }));
		EXPECT_EQ(types.type(*types.findType("accident_in_public")).importance, ThoughtImportance::Major);
		for (const std::string_view name : {"content", "resigned", "furious", "amused"}) {
			EXPECT_TRUE(types.findFeeling(name).has_value()) << name;
		}

		// A feeling-limited line alone would be the only thing that feeling ever says.
		for (std::size_t index = 0; index < types.typeCount(); ++index) {
			const ThoughtType& type = types.type(static_cast<ThoughtEventTypeId>(index));
			for (const ThoughtLine& line : type.lines) {
				if (line.feeling.has_value()) {
					EXPECT_GE(std::ranges::count(type.lines, line.feeling, &ThoughtLine::feeling), 2)
						<< type.name << ": '" << types.feeling(*line.feeling).name << "' has one line";
				}
			}
		}
	}

	TEST(ThoughtTypeDefinitions, EndsMayNotLoopThroughATypeALaterFileReplaces) {
		const std::vector<std::string> self =
			testing::TestThoughtTypes(R"(<thought id="a" label="A" importance="normal"><feeling is="content"/><ends thought="a"/></thought>)").errors;
		ASSERT_EQ(self.size(), 1U);
		EXPECT_TRUE(mentions(self, "makes a cycle (a -> a)")) << self.front();

		constexpr std::string_view kChain = R"(
  <thought id="a" label="A" importance="normal"><feeling is="content"/><ends thought="b"/></thought>
  <thought id="b" label="B" importance="normal"><feeling is="content"/><ends thought="c"/></thought>
  <thought id="c" label="C" importance="normal"><feeling is="content"/></thought>
)";
		EXPECT_TRUE(testing::TestThoughtTypes(kChain).errors.empty()) << "a chain that ends is fine";
		const std::vector<std::string> looped =
			testing::TestThoughtTypes(kChain, R"(<thought id="c" label="C" importance="normal"><feeling is="content"/><ends thought="a"/></thought>)")
				.errors;
		ASSERT_EQ(looped.size(), 1U);
		EXPECT_TRUE(mentions(looped, "makes a cycle (a -> b -> c -> a)")) << looped.front();

		testing::TestThoughtTypes broken(kChain, R"(<thought id="b" label="B" importance="normal"><feeling is="content"/></thought>)");
		ASSERT_TRUE(broken.errors.empty()) << broken.errors.front();
		EXPECT_FALSE(broken.types.type(broken.type("b")).ends.has_value()) << "the later file's b ends nothing";
		EXPECT_EQ(broken.types.type(broken.type("a")).ends, broken.type("b"));
	}

	TEST(ThoughtTypeDefinitions, LineLengthCountsCharactersAndTheSubjectAsItsBudget) {
		const auto errorsFor = [](const std::string& line) {
			return testing::TestThoughtTypes(R"(<thought id="a" label="A" importance="normal"><feeling is="content"/><line>)" + line + "</line></thought>")
				.errors;
		};
		// The budget is 40, {subject} 12.
		std::string accented;
		for (int character = 0; character < 40; ++character) {
			accented += "\xC3\xA9";
		}
		EXPECT_TRUE(errorsFor(accented).empty()) << "40 two-byte characters are 40 characters, not 80";
		EXPECT_TRUE(mentions(errorsFor(accented + "\xC3\xA9"), "is 41 characters"));

		const std::string withSubject = "{subject} is here, and I am glad.......";
		ASSERT_EQ(withSubject.size(), 39U);
		EXPECT_TRUE(mentions(errorsFor(withSubject), "is 42 characters with {subject} as 12, more than the 40")) << "9 written, 12 shown";
		EXPECT_TRUE(errorsFor("{subject} is here, and I am glad").empty()) << "32 written, 35 shown";
	}

	TEST(ThoughtLines, ALongNameIsCutToTheBudgetCleanly) {
		EXPECT_EQ(shortenSubjectName("Idris", 12), "Idris");
		EXPECT_EQ(shortenSubjectName("Bartholomew Quince", 12), "Bartholomew") << "cut at 12, the trailing space dropped";
		EXPECT_EQ(shortenSubjectName("Abcdefghijklmnop", 12), "Abcdefghijkl");
		EXPECT_EQ(shortenSubjectName("\xC3\x89lodie-Fran\xC3\xA7oise", 12), "\xC3\x89lodie-Fran\xC3\xA7") << "cut on a character, not a byte";

		testing::TestThoughtTypes rules(R"(
  <thought id="talked" label="Talked" importance="normal">
    <feeling is="content"/>
    <line>{subject} gets me.</line>
  </thought>
)");
		ASSERT_TRUE(rules.errors.empty()) << rules.errors.front();
		World world;
		world.registerSystem<TimeSystem>();
		ThoughtSystem& thoughts = world.registerSystem<ThoughtSystem>(rules.types, kSeed);
		const EntityID ada = world.createEntity();
		const EntityID longName = world.createEntity();
		world.addComponent<Colonist>(longName, Colonist{.name = "Bartholomew Quince"});
		const ThoughtHandle handle =
			thoughts.create({.colonist = ada, .eventType = rules.type("talked"), .subject = ThoughtSubject::aboutColonist(longName)});
		const Thoughts& record = *world.getComponent<Thoughts>(ada);
		EXPECT_EQ(renderThoughtLine(rules.types, record, *findThought(record, handle.thought)), "Bartholomew gets me.");
		EXPECT_EQ(record.subjectNames, (std::vector<std::string>{"Bartholomew Quince"})) << "the whole name is kept";

		thoughts.create({.colonist = ada, .eventType = rules.type("talked"), .subject = ThoughtSubject::aboutColonist(longName)});
		EXPECT_EQ(record.subjectNames.size(), 1U) << "a name is kept once per colonist, however many thoughts give it";
	}

} // namespace ecs::test
