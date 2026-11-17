#include "MoodSystem.h"

#include "../World.h"
#include "../ThoughtTesting.test.h"
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
		// `wetness` is answered by nothing, so a `while` on it holds until the thought is ended.
		constexpr std::string_view kThoughtTypes = R"(
  <thought id="slept_on_ground" importance="normal"><feeling is="annoyed" mood="-10" hours="24"/></thought>
  <thought id="slept_badly" importance="normal"><feeling is="annoyed" mood="-12" hours="24"/></thought>
  <thought id="vented" importance="normal"><feeling is="delighted" mood="+4" hours="12"/></thought>
  <thought id="vented_long" importance="normal"><feeling is="delighted" mood="+8" hours="12"/></thought>
  <thought id="cheered" importance="normal"><feeling is="delighted" mood="+30" while="wetness > 0"/></thought>
  <thought id="elated" importance="normal"><feeling is="delighted" mood="+60" while="wetness > 0"/></thought>
  <thought id="reeks_5" importance="normal"><feeling is="annoyed" mood="-5" while="wetness > 0"/></thought>
  <thought id="reeks_12" importance="normal"><feeling is="annoyed" mood="-12" while="wetness > 0"/></thought>
  <thought id="reeks_20" importance="normal"><feeling is="annoyed" mood="-20" while="wetness > 0"/></thought>
  <thought id="reeks_30" importance="normal"><feeling is="annoyed" mood="-30" while="wetness > 0"/></thought>
)";

		// Hunger and thirst weigh 1 each and every other need 0, so an empty need is -50 and a need at
		// 50 is -7.5 (penalty (70 - 50) / (70 - 30) x 0.3 = 0.15, half the weight).
		constexpr const char* kTestConfig = R"(<?xml version="1.0"?>
<Mood>
  <Easing pointsPerGameHour="5"/>
  <NeedPenalty comfortable="70" warning="30" penaltyAtWarning="0.3"/>
  <NeedWeights>
    <Need name="Hunger" weight="1"/>
    <Need name="Thirst" weight="1"/>
    <Need name="Energy" weight="0"/>
    <Need name="Bladder" weight="0"/>
    <Need name="Digestion" weight="0"/>
    <Need name="Hygiene" weight="0"/>
    <Need name="Recreation" weight="0"/>
    <Need name="Temperature" weight="0"/>
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
    <Need name="Bladder" weight="0"/>
    <Need name="Digestion" weight="0"/>
    <Need name="Hygiene" weight="0"/>
    <Need name="Recreation" weight="0"/>
    <Need name="Temperature" weight="0"/>
    <Need name="Thirst" weight="0"/>
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
			for (const std::string& error : thoughtTypes.errors) {
				ADD_FAILURE() << error;
			}
			time = &world.registerSystem<TimeSystem>();
			thoughts = &world.registerSystem<ThoughtSystem>(thoughtTypes.types, 1U);
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

		ThoughtHandle give(std::string_view type) { return thoughts->create({.colonist = colonist, .eventType = thoughtTypes.type(type)}); }

		void setNeed(EntityID who, NeedType need, float value) { world.getComponent<NeedsComponent>(who)->get(need).value = value; }

		void advanceGameHours(float hours) {
			time->skipTime(hours * 60.0F);
			world.update(0.0F);
		}

		[[nodiscard]] const Mood& moodOf(EntityID who) const { return *world.getComponent<Mood>(who); }

		[[nodiscard]] static std::vector<std::string> validationMessages() {
			std::vector<std::string> messages;
			for (const assets::ValidationError& error : assets::ConfigValidator::getErrors()) {
				messages.push_back(error.message);
			}
			return messages;
		}

		/// The validator's messages for a config built from kTestConfig with one part swapped out.
		std::vector<std::string> validationMessagesWith(const std::string& original, const std::string& replacement) {
			std::string xml = kTestConfig;
			const size_t at = xml.find(original);
			EXPECT_NE(at, std::string::npos) << original;
			xml.replace(at, original.size(), replacement);
			loadConfig(xml);
			assets::ConfigValidator::clearErrors();
			EXPECT_FALSE(assets::ConfigValidator::validateMood(config));
			return validationMessages();
		}

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

		testing::TestThoughtTypes		   thoughtTypes{kThoughtTypes};
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
		EXPECT_TRUE(moodConfigErrors(shipped).empty());
		const NeedMoodWeights weights = resolveNeedMoodWeights(shipped).weights;
		for (size_t i = 0; i < kNeedCount; ++i) {
			EXPECT_GT(weights[i], 0.0F) << needLabel(static_cast<NeedType>(i));
		}
	}

	TEST_F(MoodSystemTest, ShippedTuningGivesTheOriginalNeedsScore) {
		ASSERT_TRUE(config.loadFromFile(shippedMoodConfig().string()));
		setNeed(colonist, NeedType::Hunger, 50.0F);
		setNeed(colonist, NeedType::Thirst, 20.0F);
		setNeed(colonist, NeedType::Energy, 70.0F);
		setNeed(colonist, NeedType::Bladder, 100.0F);
		setNeed(colonist, NeedType::Digestion, 0.0F);
		setNeed(colonist, NeedType::Hygiene, 30.0F);
		setNeed(colonist, NeedType::Recreation, 90.0F);
		setNeed(colonist, NeedType::Temperature, 10.0F);
		world.update(0.0F);
		// Weights 1.0 hygiene, 1.0 temperature, 0.9 recreation, 0.7 energy, 0.6 hunger, 0.6 thirst, 0.3
		// bladder, 0.3 digestion (5.4 in all). Penalties: hunger 0.15, thirst 0.3 + 0.7 x 10/30, digestion 1,
		// hygiene 0.3, temperature 0.3 + 0.7 x 20/30, the rest 0. Weighted sum 1.776667; 100 x (1 - 1.776667
		// / 5.4) = 67.0988.
		EXPECT_NEAR(moodOf(colonist).target, 67.0988F, 0.001F);
	}

	TEST_F(MoodSystemTest, UnknownAndUnweightedNeedsAreConfigErrors) {
		std::string xml = kTestConfig;
		const std::string hygiene = R"(<Need name="Hygiene" weight="0"/>)";
		xml.replace(xml.find(hygiene), hygiene.size(), R"(<Need name="Hygeine" weight="1"/>)");
		const std::string temperature = R"(<Need name="Temperature" weight="0"/>)";
		xml.erase(xml.find(temperature), temperature.size());
		ASSERT_TRUE(loadConfig(xml));

		const ResolvedNeedMoodWeights resolved = resolveNeedMoodWeights(config);
		EXPECT_EQ(resolved.unknownNeeds, std::vector<std::string>{"Hygeine"});
		EXPECT_EQ(resolved.unweightedNeeds, (std::vector<NeedType>{NeedType::Hygiene, NeedType::Temperature}));
		EXPECT_EQ(
			moodConfigErrors(config),
			(std::vector<std::string>{
				"[Mood] Need 'Hygeine' is weighted but no need has that name",
				"[Mood] Need 'Hygiene' has no weight (add <Need name=\"Hygiene\" weight=\"...\"/>; 0 leaves it out of mood)",
				"[Mood] Need 'Temperature' has no weight (add <Need name=\"Temperature\" weight=\"...\"/>; 0 leaves it out of mood)",
			})
		);
	}

	TEST_F(MoodSystemTest, StartMoodGivesASpawnedColonistItsTargetAtOnce) {
		advanceGameHours(3.0F);
		const EntityID newcomer = spawnColonist();
		setNeed(newcomer, NeedType::Hunger, 0.0F);
		const Mood& mood = world.getSystem<MoodSystem>().startMood(newcomer);
		EXPECT_EQ(&mood, world.getComponent<Mood>(newcomer));
		EXPECT_FLOAT_EQ(mood.target, 50.0F);
		EXPECT_FLOAT_EQ(mood.value, 50.0F);
		EXPECT_EQ(mood.cause, (MoodCause{.kind = MoodCauseKind::Need, .need = NeedType::Hunger}));
		EXPECT_DOUBLE_EQ(mood.easedAt, time->simulationSeconds());
	}

	TEST_F(MoodSystemTest, TargetIsNeedsScorePlusHeldThoughts) {
		setNeed(colonist, NeedType::Hunger, 50.0F);
		give("slept_on_ground");
		give("vented");
		world.update(0.0F);

		const Mood& mood = moodOf(colonist);
		EXPECT_FLOAT_EQ(mood.needContributions[static_cast<size_t>(NeedType::Hunger)], -7.5F);
		EXPECT_FLOAT_EQ(mood.target, 100.0F - 7.5F - 10.0F + 4.0F);
		ASSERT_EQ(mood.thoughtContributions.size(), 2U);
		EXPECT_FLOAT_EQ(mood.thoughtContributions[0].amount, -10.0F);
		EXPECT_FLOAT_EQ(mood.thoughtContributions[1].amount, 4.0F);
	}

	TEST_F(MoodSystemTest, TargetClampsAtBothEnds) {
		give("cheered");
		world.update(0.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).target, 100.0F);

		thoughts->endByCause(colonist, thoughtTypes.type("cheered"), ThoughtSubject::aboutNothing());
		setNeed(colonist, NeedType::Hunger, 0.0F);
		setNeed(colonist, NeedType::Thirst, 0.0F);
		give("reeks_5");
		world.update(0.0F);
		EXPECT_FLOAT_EQ(moodOf(colonist).target, 0.0F);
	}

	TEST_F(MoodSystemTest, ContributorsSumToTargetBeforeClamping) {
		setNeed(colonist, NeedType::Hunger, 50.0F);
		setNeed(colonist, NeedType::Thirst, 20.0F);
		give("slept_on_ground");
		world.update(0.0F);
		EXPECT_FLOAT_EQ(kFullMood + sumOfContributions(moodOf(colonist)), moodOf(colonist).target);

		const ThoughtHandle lift = give("elated");
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
		const ThoughtHandle soaked = give("reeks_12");

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
		give("reeks_20");
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

		give("vented_long");
		world.update(0.0F);
		EXPECT_EQ(moodOf(colonist).cause.kind, MoodCauseKind::None) << "a positive thought is never the cause";

		setNeed(colonist, NeedType::Hunger, 50.0F);
		setNeed(colonist, NeedType::Thirst, 40.0F);
		world.update(0.0F);
		EXPECT_EQ(moodOf(colonist).cause, (MoodCause{.kind = MoodCauseKind::Need, .need = NeedType::Thirst}));

		const ThoughtHandle sleptOnGround = give("slept_badly");
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
		EXPECT_EQ(config.bandFor(11.9F).name, "Collapsing");
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
		give("reeks_30");
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
  <Easing pointsPerGameHour="5"/>
  <NeedPenalty comfortable="30" warning="70" penaltyAtWarning="0.3"/>
  <NeedWeights><Need name="Hunger" weight="1"/><Need name="Hunger" weight="2"/></NeedWeights>
  <Bands><Band name="Content" from="55"/></Bands>
</Mood>)"));
		assets::ConfigValidator::clearErrors();
		EXPECT_FALSE(assets::ConfigValidator::validateMood(config));
		const std::vector<std::string> messages = validationMessages();
		ASSERT_EQ(messages.size(), 3U);
		EXPECT_EQ(
			messages,
			(std::vector<std::string>{
				"NeedPenalty needs 0 < warning < comfortable <= 100",
				"Need 'Hunger' is weighted more than once",
				"The lowest band must start at 0 or below so every mood has a band",
			})
		);
	}

	TEST_F(MoodSystemTest, ValidationRejectsAZeroOrNegativeEasingRate) {
		const std::string easing = R"(<Easing pointsPerGameHour="5"/>)";
		EXPECT_EQ(
			validationMessagesWith(easing, R"(<Easing pointsPerGameHour="0"/>)"),
			std::vector<std::string>{"Easing pointsPerGameHour must be a positive number"}
		);
		EXPECT_EQ(
			validationMessagesWith(easing, R"(<Easing pointsPerGameHour="-2"/>)"),
			std::vector<std::string>{"Easing pointsPerGameHour must be a positive number"}
		);
	}

	TEST_F(MoodSystemTest, ValidationRejectsAMissingWeight) {
		const std::vector<std::string> messages = validationMessagesWith(R"(<Need name="Thirst" weight="1"/>)", R"(<Need name="Thirst"/>)");
		ASSERT_EQ(messages.size(), 1U);
		EXPECT_TRUE(messages.front().ends_with("<Need name=\"Thirst\"> weight is missing")) << messages.front();
	}

	TEST_F(MoodSystemTest, ValidationRejectsBandsStartingAtTheSameMood) {
		EXPECT_EQ(
			validationMessagesWith(R"(<Band name="Very low" from="12"/>)", R"(<Band name="Very low" from="30"/>)"),
			std::vector<std::string>{"Bands 'Low' and 'Very low' start at the same mood"}
		);
	}

} // namespace ecs::test
