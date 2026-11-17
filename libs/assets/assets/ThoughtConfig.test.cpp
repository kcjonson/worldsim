// Loading thought types and feelings from data: the shipped files, a later file replacing an earlier
// one, and the mistakes the loader reports.

#include "assets/ThoughtConfig.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace assets;

namespace {

	std::filesystem::path shippedThoughts() {
		// <root>/libs/assets/assets/ThoughtConfig.test.cpp -> <root>/assets/config/thoughts
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "thoughts";
	}

	constexpr const char* kTuning = R"(<tuning maxLineLength="48" maxSubjectNameLength="12" whileCheckMinutes="15"/>)";

	class ThoughtConfigTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			folder = std::filesystem::temp_directory_path() /
					 ("worldsim-thought-config-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder);
		}

		void TearDown() override { std::filesystem::remove_all(folder); }

		void write(const std::string& name, const std::string& body) {
			std::filesystem::create_directories((folder / name).parent_path());
			std::ofstream file(folder / name);
			file << "<thoughts>" << body << "</thoughts>";
		}

		/// The errors from a folder holding the tuning and `body`.
		std::vector<std::string> errorsFor(const std::string& body) {
			write("base.xml", std::string(kTuning) + body);
			return loadThoughtConfig(folder).errors;
		}

		static bool mentions(const std::vector<std::string>& errors, const std::string& text) {
			return std::ranges::any_of(errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
		}

		std::filesystem::path folder;
	};

} // namespace

TEST_F(ThoughtConfigTest, TheShippedThoughtsLoadClean) {
	const ThoughtConfigLoad load = loadThoughtConfig(shippedThoughts());
	for (const std::string& error : load.errors) {
		ADD_FAILURE() << error;
	}
	ASSERT_TRUE(load.config.tuning.has_value());
	EXPECT_GE(load.config.feelings.size(), 10U);
	EXPECT_GE(load.config.types.size(), 6U);
}

TEST_F(ThoughtConfigTest, ReadsEveryPartOfAThoughtType) {
	write("base.xml", std::string(kTuning) + R"(
  <feeling id="miserable" label="Miserable"/>
  <feeling id="furious" label="Furious"/>
  <thought id="soaked" label="Got soaked" importance="major" stacks="true">
    <feeling is="miserable" mood="-6" while="wetness > 0.6" cause="soaked"/>
    <feeling is="furious" when="attribute(civility) &lt; 6" mood="+4.5" hours="2"/>
    <feeling is="miserable" when="attribute(sanity) > 15"/>
    <line>I'm soaked through.</line>
    <line feeling="furious" when="is_night">One more minute of this...</line>
    <ends thought="dried_off"/>
  </thought>
)");
	const ThoughtConfigLoad load = loadThoughtConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	EXPECT_EQ(load.config.tuning->maxLineLength, 48U);
	EXPECT_EQ(load.config.tuning->whileCheckMinutes, 15.0F);
	ASSERT_EQ(load.config.feelings.size(), 2U);
	EXPECT_EQ(load.config.tuning->maxSubjectNameLength, 12U);
	EXPECT_EQ(load.config.feelings[1].id, "furious");
	EXPECT_EQ(load.config.feelings[1].label, "Furious");

	ASSERT_EQ(load.config.types.size(), 1U);
	const ThoughtTypeDef& soaked = load.config.types[0];
	EXPECT_EQ(soaked.label, "Got soaked");
	EXPECT_EQ(soaked.importance, ThoughtImportance::Major);
	EXPECT_TRUE(soaked.stacks);
	EXPECT_EQ(soaked.ends, "dried_off");
	EXPECT_EQ(soaked.source, "base.xml");
	ASSERT_EQ(soaked.feelings.size(), 3U);
	EXPECT_EQ(soaked.feelings[0].mood->amount, -6.0F);
	EXPECT_EQ(soaked.feelings[0].mood->whileCondition, "wetness > 0.6");
	EXPECT_EQ(soaked.feelings[0].mood->cause, "soaked");
	EXPECT_FALSE(soaked.feelings[0].mood->hours.has_value());
	EXPECT_EQ(soaked.feelings[1].when, "attribute(civility) < 6");
	EXPECT_EQ(soaked.feelings[1].mood->amount, 4.5F);
	EXPECT_EQ(soaked.feelings[1].mood->hours, 2.0F);
	EXPECT_FALSE(soaked.feelings[2].mood.has_value());
	ASSERT_EQ(soaked.lines.size(), 2U);
	EXPECT_EQ(soaked.lines[1].text, "One more minute of this...");
	EXPECT_EQ(soaked.lines[1].feeling, "furious");
	EXPECT_EQ(soaked.lines[1].when, "is_night");
}

TEST_F(ThoughtConfigTest, ALaterFileReplacesAFeelingATypeAndTheTuning) {
	write("base.xml", std::string(kTuning) + R"(
  <feeling id="content" label="Content"/>
  <thought id="ate" label="Ate" importance="normal"><feeling is="content" mood="+5" hours="6"/></thought>
  <thought id="napped" label="Napped" importance="trivial"><feeling is="content"/></thought>
)");
	write("mods/tasty/more.xml", R"(
  <tuning maxLineLength="60" maxSubjectNameLength="20" whileCheckMinutes="5"/>
  <feeling id="content" label="Content"/>
  <thought id="ate" label="Ate" importance="major"><feeling is="content" mood="+9" hours="3"/></thought>
)");
	const ThoughtConfigLoad load = loadThoughtConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	ASSERT_EQ(load.config.feelings.size(), 1U);
	EXPECT_EQ(load.config.feelings[0].source, "mods/tasty/more.xml");
	ASSERT_EQ(load.config.types.size(), 2U);
	EXPECT_EQ(load.config.types[0].id, "ate");
	EXPECT_EQ(load.config.types[0].importance, ThoughtImportance::Major);
	EXPECT_EQ(load.config.types[0].feelings[0].mood->amount, 9.0F);
	EXPECT_EQ(load.config.types[0].source, "mods/tasty/more.xml");
	EXPECT_EQ(load.config.types[1].id, "napped");
	EXPECT_EQ(load.config.tuning->maxLineLength, 60U);
	EXPECT_EQ(load.config.tuning->maxSubjectNameLength, 20U);
	EXPECT_EQ(load.config.tuning->whileCheckMinutes, 5.0F);
}

TEST_F(ThoughtConfigTest, TheSameIdTwiceInOneFileIsAnError) {
	const std::vector<std::string> errors = errorsFor(R"(
  <feeling id="content" label="Content"/>
  <feeling id="content" label="Content"/>
  <thought id="ate" label="Ate" importance="normal"><feeling is="content"/></thought>
  <thought id="ate" label="Ate" importance="normal"><feeling is="content"/></thought>
)");
	EXPECT_TRUE(mentions(errors, "base.xml: feeling 'content' is declared twice in this file"));
	EXPECT_TRUE(mentions(errors, "base.xml: thought 'ate' is declared twice in this file"));
}

TEST_F(ThoughtConfigTest, MoodEffectsTakeHoursOrWhileButNotBoth) {
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="-2"/></thought>)"), "one of the two"));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="-2" hours="1" while="is_night" cause="the cause"/></thought>)"),
		"one of the two"
	));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" hours="1"/></thought>)"), "need a mood amount"));
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="0" hours="1"/></thought>)"), "other than 0")
	);
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="lots" hours="1"/></thought>)"), "other than 0")
	);
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="-1" hours="-3"/></thought>)"), "not a positive number"
	));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="-2" while="is_night"/></thought>)"), "has no cause="
	));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="-2" hours="1" cause="night"/></thought>)"), "needs while"
	));
}

TEST_F(ThoughtConfigTest, FeelingsAndTypesNeedALabel) {
	EXPECT_TRUE(mentions(errorsFor(R"(<feeling id="x"/>)"), "feeling 'x' has no label="));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x"/></thought>)"), "thought 'a' has no label="));
}

// The log writes "Soaked. Miserable. -6 mood, while soaked", so a label or cause of its own period would double it.
TEST_F(ThoughtConfigTest, ALabelOrCauseEndingInAPeriodIsAnError) {
	EXPECT_TRUE(mentions(errorsFor(R"(<feeling id="x" label="Miserable."/>)"), "base.xml: feeling 'x': label=\"Miserable.\" ends in a period"));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" label="Got soaked." importance="normal"><feeling is="x"/></thought>)"),
		"base.xml: thought 'a': label=\"Got soaked.\" ends in a period"
	));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="-2" while="is_night" cause="the dark."/></thought>)"),
		"cause=\"the dark.\" ends in a period"
	));
}

TEST_F(ThoughtConfigTest, ShapeMistakesAreReported) {
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" label="A" importance="huge"><feeling is="x"/></thought>)"), "is not trivial, normal, or major")
	);
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" label="A"><feeling is="x"/></thought>)"), "is not trivial, normal, or major"));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" label="A" importance="normal" stacks="yes"><feeling is="x"/></thought>)"), "stacks=\"yes\""));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" label="A" importance="normal" source="idle"><feeling is="x"/></thought>)"), "unknown attribute 'source'"
	));
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" hour="1"/></thought>)"), "unknown attribute 'hour'")
	);
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling/></thought>)"), "has no is="));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x"/><line/></thought>)"), "line 1 has no text"));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x"/><ends/></thought>)"), "<ends> has no thought="));
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x"/><sound/></thought>)"), "unknown element <sound>")
	);
	EXPECT_TRUE(mentions(errorsFor(R"(<thought importance="normal"><feeling is="x"/></thought>)"), "a <thought> has no id"));
	EXPECT_TRUE(mentions(errorsFor(R"(<feeling id="x" label="X" icon="a"/>)"), "feeling 'x': unknown attribute 'icon'"));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" label="A" importance="normal" animation="rain_drop"><feeling is="x"/></thought>)"), "unknown attribute 'animation'"
	));
	EXPECT_TRUE(mentions(errorsFor(R"(<mood/>)"), "unknown element <mood>"));
}

TEST_F(ThoughtConfigTest, TuningIsRequiredAndChecked) {
	write("base.xml", R"(<feeling id="x" label="X"/>)");
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "no file gives <tuning>"));

	write("base.xml", R"(<tuning maxLineLength="0" maxSubjectNameLength="12" whileCheckMinutes="15"/>)");
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "maxLineLength must be a whole number"));

	write("base.xml", R"(<tuning maxLineLength="40" maxSubjectNameLength="12" whileCheckMinutes="0"/>)");
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "whileCheckMinutes must be a positive number"));

	write("base.xml", R"(<tuning maxLineLength="40" whileCheckMinutes="15"/>)");
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "maxSubjectNameLength must be a whole number"));

	write("base.xml", R"(<tuning maxLineLength="40" maxSubjectNameLength="2.5" whileCheckMinutes="15"/>)");
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "maxSubjectNameLength must be a whole number"));
}

TEST_F(ThoughtConfigTest, NumbersThatAreNotFiniteAreRefused) {
	for (const std::string number : {"nan", "inf", "-inf", "+nan", "infinity"}) {
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood=")" + number + R"(" hours="1"/></thought>)"),
			"other than 0"
		)) << "mood " << number;
		EXPECT_TRUE(mentions(
			errorsFor(R"(<thought id="a" label="A" importance="normal"><feeling is="x" mood="-1" hours=")" + number + R"("/></thought>)"),
			"not a positive number"
		)) << "hours " << number;

		write("base.xml", R"(<tuning maxLineLength=")" + number + R"(" maxSubjectNameLength="12" whileCheckMinutes="15"/>)");
		EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "maxLineLength must be a whole number")) << "maxLineLength " << number;
		write("base.xml", R"(<tuning maxLineLength="40" maxSubjectNameLength=")" + number + R"(" whileCheckMinutes="15"/>)");
		EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "maxSubjectNameLength must be a whole number"))
			<< "maxSubjectNameLength " << number;
		write("base.xml", R"(<tuning maxLineLength="40" maxSubjectNameLength="12" whileCheckMinutes=")" + number + R"("/>)");
		EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "whileCheckMinutes must be a positive number"))
			<< "whileCheckMinutes " << number;
	}
}

TEST_F(ThoughtConfigTest, ARootThatIsNotThoughtsIsAnError) {
	{
		std::ofstream file(folder / "wrong.xml");
		file << "<ConditionVocabulary/>";
	}
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "wrong.xml: the root element is not <thoughts>"));
	EXPECT_TRUE(mentions(loadThoughtConfig(folder / "missing").errors, "not a folder"));
}

TEST_F(ThoughtConfigTest, ReadsEachSourceTrigger) {
	write("base.xml", std::string(kTuning) + R"(
  <feeling id="content" label="Content"/>
  <thought id="sources" label="Sources" importance="normal">
    <feeling is="content"/>
    <source trigger="needFalls" need="Hunger" threshold="urgent"/>
    <source trigger="needRises" need="Thirst" threshold="seek"/>
    <source trigger="actionStarts" action="Craft"/>
    <source trigger="actionFinishes" action="Sleep" on="ground"/>
    <source trigger="actionFinishes" action="Craft" on="sleepable" first="true"/>
    <source trigger="firstSight" of="harvestable"/>
    <source trigger="recipeLearned"/>
    <source trigger="idle" everyMinutes="90" chance="0.5"/>
  </thought>
)");
	const ThoughtConfigLoad load = loadThoughtConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	const std::vector<ThoughtSourceDef>& sources = load.config.types.front().sources;
	ASSERT_EQ(sources.size(), 8U);
	EXPECT_EQ(sources[0].trigger, ThoughtTrigger::NeedFalls);
	EXPECT_EQ(sources[0].need, "Hunger");
	EXPECT_EQ(sources[0].threshold, 1U);
	EXPECT_EQ(sources[1].trigger, ThoughtTrigger::NeedRises);
	EXPECT_EQ(sources[1].threshold, 0U);
	EXPECT_EQ(sources[2].action, "Craft");
	EXPECT_TRUE(sources[2].thing.empty());
	EXPECT_EQ(sources[3].thing, "ground");
	EXPECT_EQ(sources[4].thing, "sleepable");
	EXPECT_TRUE(sources[4].first);
	EXPECT_EQ(sources[5].trigger, ThoughtTrigger::FirstSight);
	EXPECT_EQ(sources[5].thing, "harvestable");
	EXPECT_EQ(sources[6].trigger, ThoughtTrigger::RecipeLearned);
	EXPECT_EQ(sources[7].everyMinutes, 90.0F);
	EXPECT_EQ(sources[7].chance, 0.5F);
}

TEST_F(ThoughtConfigTest, SourceMistakesNameTheirAttribute) {
	const auto errorsForSource = [this](const std::string& source) {
		return errorsFor(R"(<feeling id="content" label="Content"/><thought id="a" label="A" importance="normal"><feeling is="content"/>)" + source + "</thought>");
	};
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="sneezes"/>)"), "source 1: trigger=\"sneezes\""));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="needFalls" threshold="seek"/>)"), "has no need="));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="needFalls" need="Hunger" threshold="peckish"/>)"), "threshold=\"peckish\""));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="actionFinishes"/>)"), "has no action="));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="actionFinishes" action="Sleep" on="sofa"/>)"), "on=\"sofa\""));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="actionFinishes" action="Craft" first="yes"/>)"), "first=\"yes\""));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="firstSight" of="ground"/>)"), "of=\"ground\""));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="idle" chance="0.5"/>)"), "everyMinutes=\"\""));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="idle" everyMinutes="60" chance="1.5"/>)"), "chance=\"1.5\""));
	EXPECT_TRUE(mentions(errorsForSource(R"(<source trigger="recipeLearned" need="Hunger"/>)"), "unknown attribute 'need'"));
}
