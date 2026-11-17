// Loading the condition vocabulary from data: the shipped files, merging files, and load errors.

#include "assets/ConditionConfig.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace assets;

namespace {

	std::filesystem::path shippedConditions() {
		// <root>/libs/assets/assets/ConditionConfig.test.cpp -> <root>/assets/config/conditions
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "conditions";
	}

	class ConditionConfigTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			folder = std::filesystem::temp_directory_path() /
					 ("worldsim-condition-config-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder);
		}

		void TearDown() override { std::filesystem::remove_all(folder); }

		void write(const std::string& name, const std::string& body) {
			std::filesystem::create_directories((folder / name).parent_path());
			std::ofstream file(folder / name);
			file << "<ConditionVocabulary>" << body << "</ConditionVocabulary>";
		}

		static bool mentions(const std::vector<std::string>& errors, const std::string& text) {
			return std::ranges::any_of(errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
		}

		std::filesystem::path folder;
	};

} // namespace

TEST_F(ConditionConfigTest, TheShippedVocabularyLoadsClean) {
	const ConditionConfigLoad load = loadConditionConfig(shippedConditions());
	for (const std::string& error : load.errors) {
		ADD_FAILURE() << error;
	}
	EXPECT_FALSE(load.config.terms.empty());
	EXPECT_TRUE(load.config.night.has_value());
	const std::vector<std::string>& tasks = load.config.symbolSets.at("task");
	EXPECT_NE(std::ranges::find(tasks, "work"), tasks.end());
	const std::vector<std::string>& kinds = load.config.symbolSets.at("kind");
	EXPECT_NE(std::ranges::find(kinds, "bed"), kinds.end());
}

TEST_F(ConditionConfigTest, FilesInSubfoldersAddToTheSameVocabulary) {
	write("base.xml", R"(<Symbols name="weather">rain</Symbols><Night start="20" end="6"/>
		<Term name="weather_is" result="Truth"><Parameter type="Symbol" symbols="weather"/></Term>)");
	write("mods/storms/more.xml", R"(<Symbols name="weather">storm</Symbols><Kind name="well" capability="Drinkable"/>
		<Term name="lightning" result="Number"/>)");
	const ConditionConfigLoad load = loadConditionConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	EXPECT_EQ(load.config.terms.size(), 2U);
	EXPECT_EQ(load.config.symbolSets.at("weather"), (std::vector<std::string>{"rain", "storm"}));
	EXPECT_EQ(load.config.symbolSets.at("kind"), (std::vector<std::string>{"well"}));
	ASSERT_EQ(load.config.kinds.size(), 1U);
	EXPECT_EQ(load.config.kinds[0].capability, CapabilityType::Drinkable);
}

TEST_F(ConditionConfigTest, MistakesAreLoadErrors) {
	write("a.xml", R"(<Night start="20" end="6"/><Symbols name="task">haul</Symbols>
		<Term name="twice" result="Truth"/>
		<Term name="bad_type" result="Boolean"/>
		<Term name="no_set" result="Truth"><Parameter type="Symbol"/></Term>
		<Term name="missing_set" result="Truth"><Parameter type="Symbol" symbols="nowhere"/></Term>
		<Kind name="pond" capability="Wet"/>
		<TaskGroup name="busy">haul dance</TaskGroup>
		<Symbols name="kind">rock</Symbols>
		<Bogus/>)");
	write("b.xml", R"(<Term name="twice" result="Number"/><Night start="21" end="5"/>)");
	const std::vector<std::string> errors = loadConditionConfig(folder).errors;
	EXPECT_TRUE(mentions(errors, "term 'twice' is already declared in a.xml"));
	EXPECT_TRUE(mentions(errors, "'Boolean' is not a type"));
	EXPECT_TRUE(mentions(errors, "term 'no_set' parameter 1 is a Symbol"));
	EXPECT_TRUE(mentions(errors, "\"nowhere\", which no file declares"));
	EXPECT_TRUE(mentions(errors, "'Wet' is not a capability"));
	EXPECT_TRUE(mentions(errors, "'dance' is not a name in the \"task\" symbols"));
	EXPECT_TRUE(mentions(errors, "kinds are declared with <Kind>"));
	EXPECT_TRUE(mentions(errors, "unknown element <Bogus>"));
	EXPECT_TRUE(mentions(errors, "<Night> is already given"));
	EXPECT_EQ(errors.size(), 9U);
}

TEST_F(ConditionConfigTest, NightHoursAreRequired) {
	write("a.xml", R"(<Term name="calm" result="Truth"/>)");
	EXPECT_TRUE(mentions(loadConditionConfig(folder).errors, "<Night>"));
	EXPECT_FALSE(loadConditionConfig(folder / "missing").errors.empty());
}
