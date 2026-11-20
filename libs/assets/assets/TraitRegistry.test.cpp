// Loading the colonist traits from data: the shipped files, merging files, and load errors.

#include "assets/TraitRegistry.h"
#include "assets/ConfigFiles.h"
#include "assets/CreatureConfigFiles.h"

#include <utils/Log.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

using namespace assets;

namespace {

	CreatureConfigFiles shippedHuman() {
		// <root>/libs/assets/assets/TraitRegistry.test.cpp -> <root>/assets/world/creatures/Human
		return creatureConfigFiles(
			std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "world" / "creatures" /
			"Human"
		);
	}

	constexpr const char* kCount = R"(<Count min="1" max="3"/>)";

	class TraitRegistryTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			folder = std::filesystem::temp_directory_path() /
					 ("worldsim-traits-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder);
		}

		void TearDown() override { std::filesystem::remove_all(folder); }

		void write(const std::string& name, const std::string& body) {
			std::ofstream file(folder / name);
			file << "<Traits>" << body << "</Traits>";
		}

		static std::string trait(const std::string& id, const std::string& name = "Name", const std::string& description = "A description.") {
			return "<Trait id=\"" + id + "\" name=\"" + name + "\" description=\"" + description + "\"/>";
		}

		[[nodiscard]] std::vector<std::string> loadFolder() { return registry.loadFromFiles(configFilesIn(folder).files, folder); }

		static bool mentions(const std::vector<std::string>& errors, const std::string& text) {
			return std::ranges::any_of(errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
		}

		TraitRegistry		  registry;
		std::filesystem::path folder;
	};

	struct InfoLog {
		std::vector<std::string> messages;
		static void				 sink(void* context, const foundation::LogRecord& record) {
			 static_cast<InfoLog*>(context)->messages.emplace_back(record.message);
		}
	};

} // namespace

TEST_F(TraitRegistryTest, TheShippedTraitsLoadClean) {
	const CreatureConfigFiles human = shippedHuman();
	ASSERT_TRUE(human.errors.empty()) << human.errors.front();
	const std::vector<std::string> errors = registry.loadFromFiles(human.traits, human.folder);
	for (const std::string& error : errors) {
		ADD_FAILURE() << error;
	}
	std::vector<std::string> ids;
	for (const TraitDef& trait : registry.traits()) {
		ids.push_back(trait.id);
		EXPECT_FALSE(trait.name.empty()) << trait.id;
		EXPECT_FALSE(trait.description.empty()) << trait.id;
	}
	EXPECT_EQ(ids, (std::vector<std::string>{"hard_worker", "night_owl", "gourmand", "ascetic", "volatile"}));
	EXPECT_EQ(registry.count().min, 1);
	EXPECT_EQ(registry.count().max, 3);
	EXPECT_TRUE(registry.exclusive("gourmand", "ascetic"));
	EXPECT_FALSE(registry.exclusive("volatile", "ascetic"));
}

TEST_F(TraitRegistryTest, ValidDefinitionsLoadWithTheirTextCountAndExclusions) {
	write("traits.xml", std::string(kCount) + trait("brave", "Brave", "Faces danger.") + trait("timid") + trait("calm") +
							"<Exclusive>brave timid</Exclusive>");
	ASSERT_TRUE(loadFolder().empty());
	ASSERT_EQ(registry.traits().size(), 3U);
	const TraitDef* brave = registry.find("brave");
	ASSERT_NE(brave, nullptr);
	EXPECT_EQ(brave->name, "Brave");
	EXPECT_EQ(brave->description, "Faces danger.");
	EXPECT_EQ(registry.find("missing"), nullptr);
	EXPECT_TRUE(registry.exclusive("timid", "brave"));
	EXPECT_FALSE(registry.exclusive("brave", "calm"));
}

TEST_F(TraitRegistryTest, ALaterFileReplacesATraitAndTheCountAndLogsIt) {
	write("a_base.xml", R"(<Count min="1" max="3"/>)" + trait("brave", "Brave") + trait("calm"));
	write("b_more.xml", R"(<Count min="2" max="2"/>)" + trait("brave", "Fearless") + trait("quick"));
	InfoLog log;
	foundation::Logger::setSink(&InfoLog::sink, &log);
	const std::vector<std::string> errors = loadFolder();
	foundation::Logger::setSink(nullptr, nullptr);
	ASSERT_TRUE(errors.empty()) << errors.front();
	ASSERT_EQ(registry.traits().size(), 3U);
	EXPECT_EQ(registry.traits()[0].id, "brave");
	EXPECT_EQ(registry.find("brave")->name, "Fearless");
	EXPECT_EQ(registry.count().min, 2);
	EXPECT_EQ(registry.count().max, 2);
#ifdef DEVELOPMENT_BUILD
	EXPECT_TRUE(std::ranges::any_of(log.messages, [](const std::string& message) {
		return message.find("brave") != std::string::npos && message.find("b_more.xml") != std::string::npos &&
			   message.find("a_base.xml") != std::string::npos;
	}));
#endif
}

TEST_F(TraitRegistryTest, ExclusionsFromSeveralFilesAccumulate) {
	write("a.xml", std::string(kCount) + trait("one") + trait("two") + trait("three") + "<Exclusive>one two</Exclusive>");
	write("b.xml", "<Exclusive>two three</Exclusive>");
	ASSERT_TRUE(loadFolder().empty());
	EXPECT_TRUE(registry.exclusive("one", "two"));
	EXPECT_TRUE(registry.exclusive("two", "three"));
	EXPECT_FALSE(registry.exclusive("one", "three"));
}

TEST_F(TraitRegistryTest, ATraitInSeveralExclusiveGroupsIsExcludedFromAllOfTheirMembers) {
	write("a.xml", std::string(kCount) + trait("one") + trait("two") + trait("three") + trait("four") + "<Exclusive>one two</Exclusive><Exclusive>two three</Exclusive>");
	ASSERT_TRUE(loadFolder().empty());
	EXPECT_TRUE(registry.exclusive("two", "one"));
	EXPECT_TRUE(registry.exclusive("two", "three"));
	EXPECT_FALSE(registry.exclusive("one", "three"));
	EXPECT_FALSE(registry.exclusive("four", "two"));
}

TEST_F(TraitRegistryTest, LoadingAgainReplacesWhatWasThere) {
	write("a.xml", std::string(kCount) + trait("brave"));
	ASSERT_TRUE(loadFolder().empty());
	write("a.xml", std::string(kCount) + trait("calm"));
	ASSERT_TRUE(loadFolder().empty());
	EXPECT_EQ(registry.find("brave"), nullptr);
	EXPECT_NE(registry.find("calm"), nullptr);
}

TEST_F(TraitRegistryTest, ATraitNeedsAnIdInSnakeCaseANameAndADescription) {
	write(
		"a.xml",
		std::string(kCount) + trait("Brave") + trait("two words") + trait("_lead") + trait("") + trait("nameless", "") +
			trait("silent", "Silent", "") + trait("fine")
	);
	const std::vector<std::string> errors = loadFolder();
	EXPECT_TRUE(mentions(errors, "a.xml: a <Trait> needs an id in snake_case, not 'Brave'")) << ::testing::PrintToString(errors);
	EXPECT_TRUE(mentions(errors, "not 'two words'"));
	EXPECT_TRUE(mentions(errors, "not '_lead'"));
	EXPECT_TRUE(mentions(errors, "not ''"));
	EXPECT_TRUE(mentions(errors, "trait 'nameless' has no name"));
	EXPECT_TRUE(mentions(errors, "trait 'silent' has no description"));
	EXPECT_EQ(errors.size(), 6U) << ::testing::PrintToString(errors);
}

TEST_F(TraitRegistryTest, ATraitDeclaredTwiceInOneFileIsAnError) {
	write("a.xml", std::string(kCount) + trait("brave") + trait("brave"));
	EXPECT_TRUE(mentions(loadFolder(), "a.xml: trait 'brave' is declared twice in this file"));
}

TEST_F(TraitRegistryTest, CountMustBeWholeNumbersInOrder) {
	write("a.xml", trait("brave") + R"(<Count min="1"/>)");
	EXPECT_EQ(loadFolder(), (std::vector<std::string>{"a.xml: <Count> max is missing"})) << "a rejected <Count> is the only error";

	write("a.xml", trait("brave") + R"(<Count min="1" max="2.5"/>)");
	EXPECT_TRUE(mentions(loadFolder(), "<Count> max must be a whole number from 0 to 2147483647, got \"2.5\""));

	write("a.xml", trait("brave") + R"(<Count min="1" max="3x"/>)");
	EXPECT_TRUE(mentions(loadFolder(), "got \"3x\""));

	write("a.xml", trait("brave") + R"(<Count min="3" max="1"/>)");
	EXPECT_TRUE(mentions(loadFolder(), "<Count> min is 3, more than max 1"));

	write("a.xml", trait("brave") + R"(<Count min="-1" max="1"/>)");
	EXPECT_TRUE(mentions(loadFolder(), "<Count> min must be a whole number from 0 to 2147483647, got \"-1\""));

	write("a.xml", trait("brave") + R"(<Count min="+0" max="+1"/>)");
	EXPECT_TRUE(loadFolder().empty()) << "a leading plus is read, as everywhere";
	EXPECT_EQ(registry.count().max, 1);
}

TEST_F(TraitRegistryTest, ACountOfZeroIsLegal) {
	write("a.xml", R"(<Count min="0" max="2"/>)" + trait("brave") + trait("calm"));
	ASSERT_TRUE(loadFolder().empty());
	EXPECT_EQ(registry.count().min, 0);
}

TEST_F(TraitRegistryTest, ACountIsRequiredAndCannotAskForMoreTraitsThanExist) {
	write("a.xml", trait("brave"));
	EXPECT_TRUE(mentions(loadFolder(), "no file gives <Count> min and max"));

	write("a.xml", R"(<Count min="2" max="3"/>)" + trait("brave"));
	EXPECT_TRUE(mentions(loadFolder(), "<Count> min is 2, more than the 1 declared traits"));
}

TEST_F(TraitRegistryTest, CountMinMustBeReachableUnderTheExclusiveGroups) {
	write("a.xml", R"(<Count min="2" max="3"/>)" + trait("one") + trait("two") + trait("three") + "<Exclusive>one two three</Exclusive>");
	const std::vector<std::string> errors = loadFolder();
	EXPECT_TRUE(mentions(errors, "a.xml: <Count> min is 2, but the <Exclusive> groups leave no way to hold that many of the 3 declared traits at once"))
		<< ::testing::PrintToString(errors);

	write("a.xml", R"(<Count min="2" max="3"/>)" + trait("one") + trait("two") + trait("three") + "<Exclusive>one two</Exclusive><Exclusive>one three</Exclusive>");
	EXPECT_TRUE(loadFolder().empty()) << "two and three can be held together";

	write("a.xml", R"(<Count min="3" max="3"/>)" + trait("one") + trait("two") + trait("three") + "<Exclusive>one two</Exclusive><Exclusive>one three</Exclusive>");
	EXPECT_TRUE(mentions(loadFolder(), "<Count> min is 3"));
}

TEST_F(TraitRegistryTest, AMaxAboveWhatTheGroupsAllowIsLegal) {
	write("a.xml", R"(<Count min="1" max="3"/>)" + trait("one") + trait("two") + trait("three") + "<Exclusive>one two three</Exclusive>");
	ASSERT_TRUE(loadFolder().empty());
	EXPECT_EQ(registry.count().max, 3);
}

TEST_F(TraitRegistryTest, AnUnreachableMinFailsFastWithManyExclusivePairs) {
	std::string pairs;
	for (int pair = 0; pair < 20; ++pair) {
		const std::string left = "left_" + std::to_string(pair);
		const std::string right = "right_" + std::to_string(pair);
		pairs += trait(left) + trait(right) + "<Exclusive>" + left + " " + right + "</Exclusive>";
	}
	write("a.xml", R"(<Count min="21" max="21"/>)" + pairs);
	const auto					   start = std::chrono::steady_clock::now();
	const std::vector<std::string> errors = loadFolder();
	const auto					   elapsed = std::chrono::steady_clock::now() - start;
	EXPECT_TRUE(mentions(errors, "<Count> min is 21, but the <Exclusive> groups leave no way")) << ::testing::PrintToString(errors);
	EXPECT_LT(elapsed, std::chrono::seconds(1));

	write("a.xml", R"(<Count min="20" max="20"/>)" + pairs);
	EXPECT_TRUE(loadFolder().empty()) << "one of each pair is 20";
}

TEST_F(TraitRegistryTest, ATraitIdCannotBeAWordOfTheConditionLanguage) {
	write("a.xml", std::string(kCount) + trait("not") + trait("self") + trait("fine"));
	const std::vector<std::string> errors = loadFolder();
	EXPECT_TRUE(mentions(errors, "a.xml: trait 'not': the id is a word of the condition language (and, or, not, is, self, target)"))
		<< ::testing::PrintToString(errors);
	EXPECT_TRUE(mentions(errors, "a.xml: trait 'self'"));
	EXPECT_EQ(errors.size(), 2U) << ::testing::PrintToString(errors);
}

TEST_F(TraitRegistryTest, AnyProblemLoadsNothing) {
	write("a.xml", std::string(kCount) + trait("brave") + trait("calm"));
	write("b.xml", trait("Loud"));
	ASSERT_FALSE(loadFolder().empty());
	EXPECT_TRUE(registry.traits().empty());
	EXPECT_EQ(registry.find("brave"), nullptr);
	EXPECT_EQ(registry.count().max, 0);
}

TEST_F(TraitRegistryTest, UnknownAttributesAreErrors) {
	write("a.xml", std::string(kCount) + R"(<Trait id="brave" name="Brave" description="Faces danger." color="red"/>)");
	EXPECT_TRUE(mentions(loadFolder(), "a.xml: trait 'brave': unknown attribute 'color'"));

	write("a.xml", R"(<Count min="1" max="2" typo="3"/>)" + trait("brave"));
	EXPECT_TRUE(mentions(loadFolder(), "a.xml: <Count>: unknown attribute 'typo'"));

	write("a.xml", std::string(kCount) + trait("brave") + trait("calm") + R"(<Exclusive id="x">brave calm</Exclusive>)");
	EXPECT_TRUE(mentions(loadFolder(), "unknown attribute 'id'"));
}

TEST_F(TraitRegistryTest, AtLeastOneTraitIsRequired) {
	write("a.xml", kCount);
	EXPECT_TRUE(mentions(loadFolder(), "no file declares a <Trait>"));
}

TEST_F(TraitRegistryTest, ExclusionsNeedTwoKnownDistinctTraits) {
	write("a.xml", std::string(kCount) + trait("brave") + trait("calm") + "<Exclusive>brave</Exclusive><Exclusive>brave brave</Exclusive>" +
					   "<Exclusive>brave ghost</Exclusive>");
	const std::vector<std::string> errors = loadFolder();
	EXPECT_TRUE(mentions(errors, "<Exclusive>brave</Exclusive>: lists at least two trait ids")) << ::testing::PrintToString(errors);
	EXPECT_TRUE(mentions(errors, "<Exclusive>brave brave</Exclusive>: lists a trait id twice"));
	EXPECT_TRUE(mentions(errors, "<Exclusive> names 'ghost', which no file declares as a trait"));
	EXPECT_EQ(errors.size(), 3U) << ::testing::PrintToString(errors);
}

TEST_F(TraitRegistryTest, BrokenFilesAreErrors) {
	write("a.xml", std::string(kCount) + trait("brave") + "<Mystery/>");
	EXPECT_TRUE(mentions(loadFolder(), "a.xml: unknown element <Mystery>"));

	{
		std::ofstream broken(folder / "a.xml");
		broken << "<Traits><Trait";
	}
	EXPECT_TRUE(mentions(loadFolder(), "a.xml: not readable XML"));

	{
		std::ofstream wrongRoot(folder / "a.xml");
		wrongRoot << "<Mood/>";
	}
	EXPECT_TRUE(mentions(loadFolder(), "a.xml: the root element is not <Traits>"));
}
