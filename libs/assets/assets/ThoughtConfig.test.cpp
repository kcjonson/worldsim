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

	constexpr const char* kTuning = R"(<tuning maxLineLength="48" whileCheckMinutes="15"/>)";

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
  <feeling id="miserable" icon="feeling_miserable" expression="droop"/>
  <feeling id="furious" icon="feeling_furious" expression="scowl"/>
  <thought id="soaked" importance="major" stacks="true" animation="rain_drop">
    <feeling is="miserable" mood="-6" while="wetness > 0.6"/>
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
	EXPECT_EQ(load.config.feelings[1].icon, "feeling_furious");
	EXPECT_EQ(load.config.feelings[1].expression, "scowl");

	ASSERT_EQ(load.config.types.size(), 1U);
	const ThoughtTypeDef& soaked = load.config.types[0];
	EXPECT_EQ(soaked.importance, ThoughtImportance::Major);
	EXPECT_TRUE(soaked.stacks);
	EXPECT_EQ(soaked.animation, "rain_drop");
	EXPECT_EQ(soaked.ends, "dried_off");
	EXPECT_EQ(soaked.source, "base.xml");
	ASSERT_EQ(soaked.feelings.size(), 3U);
	EXPECT_EQ(soaked.feelings[0].mood->amount, -6.0F);
	EXPECT_EQ(soaked.feelings[0].mood->whileCondition, "wetness > 0.6");
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
  <feeling id="content" icon="feeling_content" expression="smile"/>
  <thought id="ate" importance="normal"><feeling is="content" mood="+5" hours="6"/></thought>
  <thought id="napped" importance="trivial"><feeling is="content"/></thought>
)");
	write("mods/tasty/more.xml", R"(
  <tuning maxLineLength="60" whileCheckMinutes="5"/>
  <feeling id="content" icon="feeling_content_mod" expression="beam"/>
  <thought id="ate" importance="major"><feeling is="content" mood="+9" hours="3"/></thought>
)");
	const ThoughtConfigLoad load = loadThoughtConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	ASSERT_EQ(load.config.feelings.size(), 1U);
	EXPECT_EQ(load.config.feelings[0].icon, "feeling_content_mod");
	ASSERT_EQ(load.config.types.size(), 2U);
	EXPECT_EQ(load.config.types[0].id, "ate");
	EXPECT_EQ(load.config.types[0].importance, ThoughtImportance::Major);
	EXPECT_EQ(load.config.types[0].feelings[0].mood->amount, 9.0F);
	EXPECT_EQ(load.config.types[0].source, "mods/tasty/more.xml");
	EXPECT_EQ(load.config.types[1].id, "napped");
	EXPECT_EQ(load.config.tuning->maxLineLength, 60U);
	EXPECT_EQ(load.config.tuning->whileCheckMinutes, 5.0F);
}

TEST_F(ThoughtConfigTest, TheSameIdTwiceInOneFileIsAnError) {
	const std::vector<std::string> errors = errorsFor(R"(
  <feeling id="content" icon="a" expression="b"/>
  <feeling id="content" icon="c" expression="d"/>
  <thought id="ate" importance="normal"><feeling is="content"/></thought>
  <thought id="ate" importance="normal"><feeling is="content"/></thought>
)");
	EXPECT_TRUE(mentions(errors, "base.xml: feeling 'content' is declared twice in this file"));
	EXPECT_TRUE(mentions(errors, "base.xml: thought 'ate' is declared twice in this file"));
}

TEST_F(ThoughtConfigTest, MoodEffectsTakeHoursOrWhileButNotBoth) {
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x" mood="-2"/></thought>)"), "one of the two"));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" importance="normal"><feeling is="x" mood="-2" hours="1" while="is_night"/></thought>)"),
		"one of the two"
	));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x" hours="1"/></thought>)"), "need a mood amount"));
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x" mood="0" hours="1"/></thought>)"), "other than 0")
	);
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x" mood="lots" hours="1"/></thought>)"), "other than 0")
	);
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" importance="normal"><feeling is="x" mood="-1" hours="-3"/></thought>)"), "not a positive number"
	));
}

TEST_F(ThoughtConfigTest, ShapeMistakesAreReported) {
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" importance="huge"><feeling is="x"/></thought>)"), "is not trivial, normal, or major")
	);
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a"><feeling is="x"/></thought>)"), "is not trivial, normal, or major"));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" importance="normal" stacks="yes"><feeling is="x"/></thought>)"), "stacks=\"yes\""));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<thought id="a" importance="normal" source="idle"><feeling is="x"/></thought>)"), "unknown attribute 'source'"
	));
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x" hour="1"/></thought>)"), "unknown attribute 'hour'")
	);
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling/></thought>)"), "has no is="));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x"/><line/></thought>)"), "line 1 has no text"));
	EXPECT_TRUE(mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x"/><ends/></thought>)"), "<ends> has no thought="));
	EXPECT_TRUE(
		mentions(errorsFor(R"(<thought id="a" importance="normal"><feeling is="x"/><sound/></thought>)"), "unknown element <sound>")
	);
	EXPECT_TRUE(mentions(errorsFor(R"(<thought importance="normal"><feeling is="x"/></thought>)"), "a <thought> has no id"));
	EXPECT_TRUE(mentions(errorsFor(R"(<feeling id="x" icon="a"/>)"), "needs an icon and an expression"));
	EXPECT_TRUE(mentions(errorsFor(R"(<mood/>)"), "unknown element <mood>"));
}

TEST_F(ThoughtConfigTest, TuningIsRequiredAndChecked) {
	write("base.xml", R"(<feeling id="x" icon="a" expression="b"/>)");
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "no file gives <tuning>"));

	write("base.xml", R"(<tuning maxLineLength="0" whileCheckMinutes="15"/>)");
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "maxLineLength must be a whole number"));

	write("base.xml", R"(<tuning maxLineLength="40" whileCheckMinutes="0"/>)");
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "whileCheckMinutes must be a positive number"));
}

TEST_F(ThoughtConfigTest, ARootThatIsNotThoughtsIsAnError) {
	{
		std::ofstream file(folder / "wrong.xml");
		file << "<ConditionVocabulary/>";
	}
	EXPECT_TRUE(mentions(loadThoughtConfig(folder).errors, "wrong.xml: the root element is not <thoughts>"));
	EXPECT_TRUE(mentions(loadThoughtConfig(folder / "missing").errors, "not a folder"));
}
