// Loading the condition vocabulary from data: the shipped files, merging files, and load errors.

#include "assets/ConditionConfig.h"

#include <utils/Log.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

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
	const std::vector<std::string>& tasks = load.config.symbolSets.at("task").names;
	EXPECT_NE(std::ranges::find(tasks, "work"), tasks.end());
	const std::vector<std::string>& kinds = load.config.symbolSets.at("kind").names;
	EXPECT_NE(std::ranges::find(kinds, "bed"), kinds.end());
}

TEST_F(ConditionConfigTest, FilesInSubfoldersAddToTheSameVocabulary) {
	write("base.xml", R"(<Symbols name="weather">rain</Symbols><Night start="20" end="6"/>
		<Term name="weather_is" result="Truth"><Parameter type="Symbol" symbols="weather"/></Term>)");
	write("mods/storms/more.xml", R"(<Symbols name="weather">storm</Symbols><Kind name="well" capability="drinkable"/>
		<Term name="lightning" result="Number"/>)");
	const ConditionConfigLoad load = loadConditionConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	EXPECT_EQ(load.config.terms.size(), 2U);
	EXPECT_EQ(load.config.symbolSets.at("weather").names, (std::vector<std::string>{"rain", "storm"}));
	EXPECT_EQ(load.config.symbolSets.at("kind").names, (std::vector<std::string>{"well"}));
	ASSERT_EQ(load.config.kinds.size(), 1U);
	EXPECT_EQ(load.config.kinds[0].capability, CapabilityType::Drinkable);
}

TEST_F(ConditionConfigTest, MistakesAreLoadErrors) {
	write("a.xml", R"(<Night start="20" end="6"/>
		<Term name="bad_type" result="Boolean"/>
		<Term name="no_set" result="Truth"><Parameter type="Symbol"/></Term>
		<Term name="missing_set" result="Truth"><Parameter type="Symbol" symbols="nowhere"/></Term>
		<Kind name="pond" capability="Wet"/>
		<Kind name="well" capability="Drinkable"/>
		<Symbols name="kind">rock</Symbols>
		<Symbols name="trait">brave</Symbols>
		<Symbols name="mood" open="maybe"/>
		<TaskGroup name="busy">haul</TaskGroup>
		<Bogus/>)");
	write("b.xml", R"(<TaskGroup name="busy">craft</TaskGroup>)");
	const std::vector<std::string> errors = loadConditionConfig(folder).errors;
	EXPECT_TRUE(mentions(errors, "'Boolean' is not a type"));
	EXPECT_TRUE(mentions(errors, "term 'no_set' parameter 1 is a Symbol"));
	EXPECT_TRUE(mentions(errors, "\"nowhere\", which no file declares"));
	EXPECT_TRUE(mentions(errors, "'Wet' is not a capability"));
	EXPECT_TRUE(mentions(errors, "'Drinkable' is not a capability"));
	EXPECT_TRUE(mentions(errors, "a.xml: the \"kind\" names come only from the <Kind> declarations"));
	EXPECT_TRUE(mentions(errors, "a.xml: the \"trait\" names come only from the trait definitions"));
	EXPECT_TRUE(mentions(errors, "open=\"maybe\""));
	EXPECT_TRUE(mentions(errors, "b.xml: task group 'busy' is already declared in a.xml"));
	EXPECT_TRUE(mentions(errors, "unknown element <Bogus>"));
	EXPECT_EQ(errors.size(), 10U);
}

TEST_F(ConditionConfigTest, AFileDeclaringOrOpeningAnOwnedSetIsAnErrorNamingIt) {
	write("base.xml", R"(<Night start="20" end="6"/>)");
	for (const OwnedSymbolSet& owned : kOwnedSymbolSets) {
		const std::string set(owned.name);
		for (const std::string& declaration :
			 {"<Symbols name=\"" + set + "\">morale</Symbols>", "<Symbols name=\"" + set + "\" open=\"true\"/>"}) {
			write("mods/typo/conditions.xml", declaration);
			const ConditionConfigLoad load = loadConditionConfig(folder);
			ASSERT_EQ(load.errors.size(), 1U) << declaration;
			EXPECT_TRUE(
				mentions(load.errors, "mods/typo/conditions.xml: the \"" + set + "\" names come only from " + std::string(owned.owner))
			) << load.errors.front();
			EXPECT_TRUE(load.config.symbolSets.at(set).names.empty()) << declaration;
			EXPECT_FALSE(load.config.symbolSets.at(set).open) << declaration;
		}
	}
}

TEST_F(ConditionConfigTest, NightHoursAreRequired) {
	write("a.xml", R"(<Term name="calm" result="Truth"/>)");
	EXPECT_TRUE(mentions(loadConditionConfig(folder).errors, "<Night>"));
	EXPECT_FALSE(loadConditionConfig(folder / "missing").errors.empty());
}

TEST_F(ConditionConfigTest, NightHoursMustBeNumbers) {
	const std::vector<std::pair<std::string, std::string>> cases = {
		{R"(start="abc" end="6")", "a.xml: <Night> start must be a number, got \"abc\""},
		{R"(start="20" end="6x")", "a.xml: <Night> end must be a number, got \"6x\""},
		{R"(start="" end="6")", "a.xml: <Night> start must be a number, got \"\""},
		{R"(end="6")", "a.xml: <Night> start is missing"},
		{R"(start="24" end="6")", "a.xml: <Night> start must be an hour from 0 up to 24, got \"24\""},
		{R"(start="20" end="6" length="10")", "a.xml: <Night>: unknown attribute 'length'"},
	};
	for (const auto& [night, error] : cases) {
		write("a.xml", "<Night " + night + "/>");
		const ConditionConfigLoad load = loadConditionConfig(folder);
		EXPECT_TRUE(mentions(load.errors, error)) << night;
		EXPECT_FALSE(load.config.night.has_value()) << night;
	}
}

TEST_F(ConditionConfigTest, KindsNameCapabilitiesAsAssetDefinitionsSpellThem) {
	write("a.xml", R"(<Night start="20" end="6"/><Kind name="well" capability="drinkable"/><Kind name="pile" capability="waste"/>)");
	const ConditionConfigLoad load = loadConditionConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	ASSERT_EQ(load.config.kinds.size(), 2U);
	EXPECT_EQ(load.config.kinds[0].capability, CapabilityType::Drinkable);
	EXPECT_EQ(load.config.kinds[1].capability, CapabilityType::Waste);
}

TEST_F(ConditionConfigTest, TheShippedVocabularyListsNoEngineNames) {
	const ConditionConfigLoad load = loadConditionConfig(shippedConditions());
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	const auto& sets = load.config.symbolSets;
	for (const OwnedSymbolSet& owned : kOwnedSymbolSets) {
		EXPECT_FALSE(sets.at(std::string(owned.name)).open) << owned.name;
	}
	EXPECT_TRUE(sets.at("need").names.empty());
	EXPECT_TRUE(sets.at("attribute").names.empty());
	EXPECT_TRUE(sets.at("trait").names.empty());
	EXPECT_EQ(sets.at("task").names, (std::vector<std::string>{"work"}));
	for (const char* open : {"behavior", "event", "mood_cause"}) {
		EXPECT_TRUE(sets.at(open).open) << open;
		EXPECT_TRUE(sets.at(open).names.empty()) << open;
	}
	EXPECT_FALSE(sets.at("resource").open);
	const auto colonist = std::ranges::find(load.config.kinds, "colonist", &ConditionKindDef::name);
	ASSERT_NE(colonist, load.config.kinds.end());
	EXPECT_FALSE(colonist->capability.has_value());
	EXPECT_EQ(colonist->mover, "Human");
}

TEST_F(ConditionConfigTest, AKindMapsToACapabilityOrAMoverDefinition) {
	write("a.xml", R"(<Night start="20" end="6"/><Kind name="deer" mover="Deer"/><Kind name="well" capability="drinkable"/>)");
	write("mods/herds/kinds.xml", R"(<Kind name="well" mover="WellKeeper"/>)");
	const ConditionConfigLoad load = loadConditionConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	ASSERT_EQ(load.config.kinds.size(), 2U);
	EXPECT_EQ(load.config.kinds[0].mover, "Deer");
	EXPECT_FALSE(load.config.kinds[0].capability.has_value());
	EXPECT_EQ(load.config.kinds[1].mover, "WellKeeper") << "a later file replaces the whole mapping";
	EXPECT_FALSE(load.config.kinds[1].capability.has_value());

	write("a.xml", R"(<Night start="20" end="6"/><Kind name="both" capability="edible" mover="Deer"/><Kind name="blank" mover=""/>)");
	std::filesystem::remove_all(folder / "mods");
	const std::vector<std::string> errors = loadConditionConfig(folder).errors;
	EXPECT_TRUE(mentions(errors, "kind 'both': a kind is a capability or a mover, not both"));
	EXPECT_TRUE(mentions(errors, "kind 'blank': mover=\"\" names no definition"));
	EXPECT_EQ(errors.size(), 2U);
}

TEST_F(ConditionConfigTest, TheFolderLoadsBeforeItsSubfolders) {
	write("z_base.xml", R"(<Night start="20" end="6"/><Kind name="water" capability="drinkable"/>)");
	write("a_mod/more.xml", R"(<Night start="21" end="5"/><Kind name="water" capability="edible"/>)");
	const ConditionConfigLoad load = loadConditionConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	ASSERT_TRUE(load.config.night.has_value());
	EXPECT_EQ(load.config.night->start, 21.0F);
	ASSERT_EQ(load.config.kinds.size(), 1U);
	EXPECT_EQ(load.config.kinds[0].capability, CapabilityType::Edible);
}

TEST_F(ConditionConfigTest, ALaterFileAddsSymbolsTermsAndKinds) {
	write("base.xml", R"(<Night start="20" end="6"/><Symbols name="weather">rain</Symbols>
		<Term name="calm" result="Truth"/><Kind name="water" capability="drinkable"/>)");
	write("mods/storms/more.xml", R"(<Symbols name="weather">rain storm</Symbols>
		<Term name="lightning" result="Number"/><Kind name="bed" capability="sleepable"/>)");
	const ConditionConfigLoad load = loadConditionConfig(folder);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	EXPECT_EQ(load.config.symbolSets.at("weather").names, (std::vector<std::string>{"rain", "storm"}));
	EXPECT_EQ(load.config.terms.size(), 2U);
	EXPECT_EQ(load.config.kinds.size(), 2U);
}

namespace {

	struct OverrideLog {
		std::vector<std::string> messages;
		static void				 sink(void* context, const foundation::LogRecord& record) {
			 if (record.level == foundation::LogLevel::Info && std::strstr(record.message, "replaces") != nullptr) {
				 static_cast<OverrideLog*>(context)->messages.emplace_back(record.message);
			 }
		}
	};

} // namespace

TEST_F(ConditionConfigTest, ALaterFileReplacesAKindMappingAndTheNightAndSaysSo) {
	write("base.xml", R"(<Night start="20" end="6"/><Kind name="water" capability="drinkable"/><Kind name="fire"/>)");
	write("mods/fire/kinds.xml", R"(<Kind name="water" capability="edible"/><Kind name="fire" capability="craftable"/>
		<Night start="22" end="4"/>)");
	OverrideLog log;
	foundation::Logger::setSink(&OverrideLog::sink, &log);
	const ConditionConfigLoad load = loadConditionConfig(folder);
	foundation::Logger::setSink(nullptr, nullptr);
	ASSERT_TRUE(load.errors.empty()) << load.errors.front();
	ASSERT_EQ(load.config.kinds.size(), 2U);
	EXPECT_EQ(load.config.kinds[0].capability, CapabilityType::Edible);
	EXPECT_EQ(load.config.kinds[1].capability, CapabilityType::Craftable);
	ASSERT_TRUE(load.config.night.has_value());
	EXPECT_EQ(load.config.night->start, 22.0F);
	EXPECT_EQ(load.config.night->end, 4.0F);
#ifdef DEVELOPMENT_BUILD
	ASSERT_EQ(log.messages.size(), 3U);
	for (const std::string& message : log.messages) {
		EXPECT_NE(message.find("base.xml"), std::string::npos) << message;
		EXPECT_NE(message.find("mods/fire/kinds.xml"), std::string::npos) << message;
	}
#endif
}

TEST_F(ConditionConfigTest, RedeclaringATermIdenticallyIsAcceptedAndDifferentlyIsAnError) {
	write("base.xml", R"(<Night start="20" end="6"/><Symbols name="weather">rain</Symbols>
		<Term name="same" result="Number"><Parameter type="Symbol" symbols="weather"/></Term>
		<Term name="retyped" result="Truth"/>
		<Term name="regrown" result="Truth"><Parameter type="Number"/></Term>)");
	write("mods/x/terms.xml", R"(<Term name="same" result="Number"><Parameter type="Symbol" symbols="weather"/></Term>
		<Term name="retyped" result="Number"/>
		<Term name="regrown" result="Truth"><Parameter type="Number"/><Parameter type="Number"/></Term>)");
	const ConditionConfigLoad load = loadConditionConfig(folder);
	EXPECT_TRUE(mentions(load.errors, "mods/x/terms.xml: term 'retyped' is declared in base.xml with a different signature"));
	EXPECT_TRUE(mentions(load.errors, "mods/x/terms.xml: term 'regrown' is declared in base.xml with a different signature"));
	EXPECT_EQ(load.errors.size(), 2U);
	EXPECT_EQ(load.config.terms.size(), 3U);
}
