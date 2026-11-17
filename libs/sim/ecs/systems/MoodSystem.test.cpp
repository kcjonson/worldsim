#include "MoodSystem.h"

#include "../World.h"
#include "ThoughtSystem.h"
#include "TimeSystem.h"

#include <assets/ConfigValidator.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace ecs::test {

	namespace {
		constexpr ThoughtEventTypeId kSleptOnGround = 1;
		constexpr ThoughtEventTypeId kVented = 2;
		constexpr ThoughtEventTypeId kSomethingReeks = 3;
		constexpr FeelingId			 kAnnoyed = 1;
		constexpr FeelingId			 kDelighted = 2;

		// Hunger and thirst weigh 1 each and nothing else counts, so an empty need is -50 and a need at
		// 50 is -7.5 (penalty (70 - 50) / (70 - 30) x 0.3 = 0.15, half the weight).
		constexpr const char* kTestConfig = R"(<?xml version="1.0"?>
<Mood>
  <Easing pointsPerGameHour="5"/>
  <NeedPenalty comfortable="70" warning="30" penaltyAtWarning="0.3"/>
  <NeedWeights>
    <Need name="Hunger" weight="1"/>
    <Need name="Thirst" weight="1"/>
  </NeedWeights>
  <Bands>
    <Band name="Content" from="55"/>
    <Band name="Low" from="30"/>
    <Band name="Very low" from="12"/>
    <Band name="Collapsing" from="0"/>
  </Bands>
</Mood>)";

		constexpr const char* kOtherConfig = R"(<?xml version="1.0"?>
<Mood>
  <Easing pointsPerGameHour="10"/>
  <NeedPenalty comfortable="80" warning="40" penaltyAtWarning="0.5"/>
  <NeedWeights>
    <Need name="Hunger" weight="1"/>
    <Need name="Energy" weight="3"/>
  </NeedWeights>
  <Bands>
    <Band name="Bottom" from="0"/>
    <Band name="High" from="80"/>
    <Band name="Middling" from="40"/>
  </Bands>
</Mood>)";

		std::filesystem::path shippedMoodConfig() {
			// <root>/libs/sim/ecs/systems/MoodSystem.test.cpp -> <root>/assets/config/colonists/mood.xml
			return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path() / "assets" /
				   "config" / "colonists" / "mood.xml";
		}
	} // namespace

	class MoodSystemTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			ASSERT_TRUE(loadConfig(kTestConfig));
			time = &world.registerSystem<TimeSystem>();
			thoughts = &world.registerSystem<ThoughtSystem>();
			world.registerSystem<MoodSystem>(config);
			colonist = spawnColonist();
			world.update(0.0F);
		}

		void TearDown() override {
			for (const auto& path : tempFiles) {
				std::filesystem::remove(path);
			}
			assets::ConfigValidator::clearErrors();
		}

		bool loadConfig(const std::string& xml) {
			static const std::string token = [] {
				std::random_device entropy;
				return std::to_string(entropy()) + "_" + std::to_string(entropy());
			}();
			const std::filesystem::path path =
				std::filesystem::temp_directory_path() / ("moodconfig_" + token + "_" + std::to_string(tempFiles.size()) + ".xml");
			std::ofstream(path) << xml;
			tempFiles.push_back(path);
			return config.loadFromFile(path.string());
		}

		EntityID spawnColonist() {
			const EntityID entity = world.createEntity();
			world.addComponent<NeedsComponent>(entity, NeedsComponent::createDefault());
			return entity;
		}

		void setNeed(EntityID who, NeedType need, float value) { world.getComponent<NeedsComponent>(who)->get(need).value = value; }

		void advanceGameHours(float hours) {
			time->skipTime(hours * 60.0F);
			world.update(0.0F);
		}

		[[nodiscard]] const Mood& moodOf(EntityID who) const { return *world.getComponent<Mood>(who); }

		[[nodiscard]] static float sumOfContributions(const Mood& mood) {
			float sum = 0.0F;
			for (const float contribution : mood.needContributions) {
				sum += contribution;
			}
			for (const MoodThoughtContribution& contribution : mood.thoughtContributions) {
				sum += contribution.amount;
			}
			return sum;
		}

		assets::MoodConfig				   config;
		std::vector<std::filesystem::path> tempFiles;
		World							   world;
		TimeSystem*						   time = nullptr;
		ThoughtSystem*					   thoughts = nullptr;
		EntityID						   colonist = kInvalidEntity;
	};

	TEST_F(MoodSystemTest, ShippedConfigIsValidAndWeightsEveryNeed) {
		assets::MoodConfig shipped;
		ASSERT_TRUE(shipped.loadFromFile(shippedMoodConfig().string()));
		assets::ConfigValidator::clearErrors();
		EXPECT_TRUE(assets::ConfigValidator::validateMood(shipped));
		EXPECT_EQ(shipped.needWeights().size(), kNeedCount) << "every weighted name is a need";
		const NeedMoodWeights weights = resolveNeedMoodWeights(shipped);
		for (size_t i = 0; i < kNeedCount; ++i) {
			EXPECT_GT(weights[i], 0.0F) << needLabel(static_cast<NeedType>(i));
		}
	}

	TEST_F(MoodSystemTest, TargetIsNeedsScorePlusHeldThoughts) {
		setNeed(colonist, NeedType::Hunger, 50.0F);
		thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-10.0F, 24.0F)}
		);
		thoughts->create(
			{.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::timed(4.0F, 12.0F)}
		);
		world.update(0.0F);

		const Mood& mood = moodOf(colonist);
		EXPECT_FLOAT_EQ(mood.needContributions[static_cast<size_t>(NeedType::Hunger)], -7.5F);
		EXPECT_FLOAT_EQ(mood.target, 100.0F - 7.5F - 10.0F + 4.0F);
		ASSERT_EQ(mood.thoughtContributions.size(), 2U);
		EXPECT_FLOAT_EQ(mood.thoughtContributions[0].amount, -10.0F);
		EXPECT_FLOAT_EQ(mood.thoughtContributions[1].amount, 4.0F);
	}

	TEST_F(MoodSystemTest, TargetClampsAtBothEnds) {
		thoughts->create(
			{.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::whileCauseHolds(30.0F)}
		);
		world.update(0.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).target, 100.0F);

		thoughts->endByCause(colonist, kVented, ThoughtSubject::aboutNothing());
		setNeed(colonist, NeedType::Hunger, 0.0F);
		setNeed(colonist, NeedType::Thirst, 0.0F);
		thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-5.0F)}
		);
		world.update(0.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).target, 0.0F);
	}

	TEST_F(MoodSystemTest, ContributorsSumToTargetBeforeClamping) {
		setNeed(colonist, NeedType::Hunger, 50.0F);
		setNeed(colonist, NeedType::Thirst, 20.0F);
		thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-10.0F, 24.0F)}
		);
		world.update(0.0F);
		EXPECT_FLOAT_EQ(kFullMood + sumOfContributions(moodOf(colonist)), moodOf(colonist).target);

		const ThoughtHandle lift = thoughts->create(
			{.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::whileCauseHolds(60.0F)}
		);
		ASSERT_NE(lift.thought, 0U);
		world.update(0.0F);
		const Mood& mood = moodOf(colonist);
		const float unclamped = kFullMood + sumOfContributions(mood);
		EXPECT_GT(unclamped, kFullMood);
		EXPECT_FLOAT_EQ(mood.target, kFullMood);
	}

	TEST_F(MoodSystemTest, NewColonistStartsAtTarget) {
		advanceGameHours(3.0F);
		const EntityID newcomer = spawnColonist();
		setNeed(newcomer, NeedType::Hunger, 0.0F);
		world.update(0.0F);
		EXPECT_FLOAT_EQ(moodOf(newcomer).target, 50.0F);
		EXPECT_FLOAT_EQ(moodOf(newcomer).value, 50.0F);
	}

	TEST_F(MoodSystemTest, EasesAtTheConfiguredRateAndNeverOvershoots) {
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 100.0F);
		const ThoughtHandle soaked = thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-12.0F)}
		);

		advanceGameHours(1.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).target, 88.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 95.0F);
		advanceGameHours(1.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 90.0F);
		advanceGameHours(1.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 88.0F) << "stops at the target rather than passing it";
		advanceGameHours(5.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 88.0F);

		thoughts->end(soaked);
		advanceGameHours(0.5F);
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 90.5F) << "rises at the same rate";
		advanceGameHours(10.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 100.0F);
	}

	TEST_F(MoodSystemTest, NoMovementWithoutGameTime) {
		thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-20.0F)}
		);
		world.update(0.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).target, 80.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 100.0F);

		time->pause();
		for (int frame = 0; frame < 10; ++frame) {
			world.update(1.0F);
		}
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 100.0F) << "paused, no game time passes";

		time->setSpeed(GameSpeed::VeryFast);
		world.update(6.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).value, 95.0F) << "6 real seconds at 10x is one game hour";
	}

	TEST_F(MoodSystemTest, CauseIsTheLargestNegativeNeedOrThought) {
		world.update(0.0F);
		EXPECT_EQ(moodOf(colonist).cause.kind, MoodCauseKind::None);

		thoughts->create(
			{.colonist = colonist, .eventType = kVented, .feeling = kDelighted, .moodEffect = MoodEffect::timed(8.0F, 12.0F)}
		);
		world.update(0.0F);
		EXPECT_EQ(moodOf(colonist).cause.kind, MoodCauseKind::None) << "a positive thought is never the cause";

		setNeed(colonist, NeedType::Hunger, 50.0F);
		setNeed(colonist, NeedType::Thirst, 40.0F);
		world.update(0.0F);
		EXPECT_EQ(moodOf(colonist).cause, (MoodCause{.kind = MoodCauseKind::Need, .need = NeedType::Thirst}));

		const ThoughtHandle sleptOnGround = thoughts->create(
			{.colonist = colonist, .eventType = kSleptOnGround, .feeling = kAnnoyed, .moodEffect = MoodEffect::timed(-12.0F, 24.0F)}
		);
		world.update(0.0F);
		EXPECT_EQ(moodOf(colonist).cause, (MoodCause{.kind = MoodCauseKind::Thought, .thought = sleptOnGround.thought}));

		setNeed(colonist, NeedType::Hunger, 0.0F);
		world.update(0.0F);
		EXPECT_EQ(moodOf(colonist).cause, (MoodCause{.kind = MoodCauseKind::Need, .need = NeedType::Hunger}));
	}

	TEST_F(MoodSystemTest, BandBoundaries) {
		EXPECT_EQ(config.bandFor(100.0F).name, "Content");
		EXPECT_EQ(config.bandFor(55.0F).name, "Content");
		EXPECT_EQ(config.bandFor(54.0F).name, "Low");
		EXPECT_EQ(config.bandFor(54.9F).name, "Low");
		EXPECT_EQ(config.bandFor(30.0F).name, "Low");
		EXPECT_EQ(config.bandFor(29.0F).name, "Very low");
		EXPECT_EQ(config.bandFor(12.0F).name, "Very low");
		EXPECT_EQ(config.bandFor(11.0F).name, "Collapsing");
		EXPECT_EQ(config.bandFor(0.0F).name, "Collapsing");
	}

	TEST_F(MoodSystemTest, ConfigValuesAreReadNotConstants) {
		ASSERT_TRUE(loadConfig(kOtherConfig));
		assets::ConfigValidator::clearErrors();
		ASSERT_TRUE(assets::ConfigValidator::validateMood(config));

		EXPECT_EQ(config.bandFor(80.0F).name, "High");
		EXPECT_EQ(config.bandFor(79.0F).name, "Middling");
		EXPECT_EQ(config.bandFor(40.0F).name, "Middling");
		EXPECT_EQ(config.bandFor(39.0F).name, "Bottom");

		setNeed(colonist, NeedType::Energy, 60.0F);
		thoughts->create(
			{.colonist = colonist, .eventType = kSomethingReeks, .feeling = kAnnoyed, .moodEffect = MoodEffect::whileCauseHolds(-30.0F)}
		);
		advanceGameHours(1.0F);
		const Mood& mood = moodOf(colonist);
		// Energy at 60 is halfway down the mild part of an 80 / 40 curve: penalty 0.25, weight 3 of 4.
		EXPECT_FLOAT_EQ(mood.needContributions[static_cast<size_t>(NeedType::Energy)], -18.75F);
		EXPECT_FLOAT_EQ(mood.needContributions[static_cast<size_t>(NeedType::Thirst)], 0.0F) << "thirst weighs nothing here";
		EXPECT_FLOAT_EQ(mood.target, 100.0F - 18.75F - 30.0F);
		EXPECT_FLOAT_EQ(mood.value, 90.0F) << "10 points per game hour";
	}

	TEST_F(MoodSystemTest, ValidationRejectsABrokenConfig) {
		ASSERT_TRUE(loadConfig(R"(<?xml version="1.0"?>
<Mood>
  <NeedPenalty comfortable="30" warning="70" penaltyAtWarning="0.3"/>
  <NeedWeights><Need name="Hunger" weight="1"/><Need name="Hunger" weight="2"/></NeedWeights>
  <Bands><Band name="Content" from="55"/></Bands>
</Mood>)"));
		assets::ConfigValidator::clearErrors();
		EXPECT_FALSE(assets::ConfigValidator::validateMood(config));
		// No rate, a backwards curve, a need weighted twice, and no band below 55.
		EXPECT_EQ(assets::ConfigValidator::getErrorCount(), 4U);
	}

} // namespace ecs::test
