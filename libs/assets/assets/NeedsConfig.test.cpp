// Loading needs from data, thresholds and mood cost: the shipped folder, a later file replacing a need,
// and the mistakes the loader reports.

#include "assets/NeedsConfig.h"
#include "assets/ConfigFiles.h"
#include "assets/CreatureConfigFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace assets;

namespace {

	CreatureConfigFiles shippedHuman() {
		// <root>/libs/assets/assets/NeedsConfig.test.cpp -> <root>/assets/world/creatures/Human
		return creatureConfigFiles(
			std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "world" / "creatures" /
			"Human"
		);
	}

	/// A fresh folder holding `body` as needs.xml and, when given, `laterBody` as z-later.xml, which loads after it.
	class NeedsFolder {
	  public:
		explicit NeedsFolder(const std::string& body, const std::string& laterBody = {})
			: folder(
				  std::filesystem::temp_directory_path() /
				  ("worldsim-needs-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "-" +
				   std::to_string(nextFolder++))
			  ) {
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder);
			std::ofstream(folder / "needs.xml") << body;
			if (!laterBody.empty()) {
				std::ofstream(folder / "z-later.xml") << laterBody;
			}
		}
		~NeedsFolder() { std::filesystem::remove_all(folder); }
		NeedsFolder(const NeedsFolder&) = delete;
		NeedsFolder& operator=(const NeedsFolder&) = delete;

		[[nodiscard]] NeedsConfigLoad load() const { return loadNeedsConfig(configFilesIn(folder).files, folder); }

		std::filesystem::path folder;

	  private:
		static inline int nextFolder = 0;
	};

	std::vector<std::string> errorsFor(const std::string& body) {
		return NeedsFolder(body).load().errors;
	}

	bool mentions(const std::vector<std::string>& errors, const std::string& text) {
		return std::ranges::any_of(errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
	}

} // namespace

TEST(NeedsConfig, TheShippedNeedsLoadClean) {
	const CreatureConfigFiles human = shippedHuman();
	ASSERT_TRUE(human.errors.empty()) << ::testing::PrintToString(human.errors);
	const NeedsConfigLoad load = loadNeedsConfig(human.needs, human.folder);
	for (const std::string& error : load.errors) {
		ADD_FAILURE() << error;
	}
	const auto hunger = std::ranges::find(load.config.needs, std::string("Hunger"), &NeedDef::need);
	ASSERT_NE(hunger, load.config.needs.end());
	EXPECT_EQ(hunger->thresholds, (std::array{50.0F, 25.0F, 10.0F}));

	std::vector<std::pair<std::string, float>> costs;
	for (const NeedDef& need : load.config.needs) {
		costs.emplace_back(need.need, need.moodCost);
	}
	EXPECT_EQ(
		costs,
		(std::vector<std::pair<std::string, float>>{
			{"Hunger", 25.0F},
			{"Thirst", 25.0F},
			{"Energy", 20.0F},
			{"Bladder", 10.0F},
			{"Digestion", 10.0F},
			{"Hygiene", 15.0F},
			{"Recreation", 15.0F},
			{"Temperature", 60.0F},
		})
	);
}

TEST(NeedsConfig, ALaterFileReplacesANeedByNameCostIncluded) {
	const NeedsFolder folder(
		R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10" cost="25"/><Need name="Thirst" seek="50" urgent="25" critical="10" cost="25"/></Needs>)",
		R"(<Needs><Need name="Hunger" seek="60" urgent="25" critical="10" cost="50"/></Needs>)"
	);
	const NeedsConfigLoad load = folder.load();
	EXPECT_TRUE(load.errors.empty());
	ASSERT_EQ(load.config.needs.size(), 2U);
	EXPECT_EQ(load.config.needs[0].thresholds[0], 60.0F);
	EXPECT_EQ(load.config.needs[0].moodCost, 50.0F);
	EXPECT_EQ(load.config.needs[0].source, "z-later.xml") << "a file is named relative to the folder";
	EXPECT_EQ(load.config.needs[1].thresholds[0], 50.0F);
	EXPECT_EQ(load.config.needs[1].moodCost, 25.0F);
}

TEST(NeedsConfig, ACostOfZeroLeavesTheNeedOutOfMood) {
	const NeedsConfigLoad load = NeedsFolder(R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10" cost="0"/></Needs>)").load();
	EXPECT_TRUE(load.errors.empty());
	ASSERT_EQ(load.config.needs.size(), 1U);
	EXPECT_EQ(load.config.needs[0].moodCost, 0.0F);
}

TEST(NeedsConfig, MistakesNameTheFileTheNeedAndTheAttributeAndLoadNothingFromTheFile) {
	const NeedsFolder broken(
		R"(<Needs><Need name="Thirst" seek="50" urgent="25" critical="10" cost="25"/><Need name="Hunger" seek="50%" urgent="25" critical="10" cost="25"/></Needs>)"
	);
	const NeedsConfigLoad load = broken.load();
	EXPECT_TRUE(mentions(load.errors, "needs.xml: <Need name=\"Hunger\"> seek must be a number, got \"50%\""));
	EXPECT_TRUE(load.config.needs.empty()) << "a file with any error adds nothing, its good <Need> included";

	EXPECT_TRUE(mentions(errorsFor("<Mood/>"), "the root element is not <Needs>"));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Want name="Hunger"/></Needs>)"), "unknown element <Want>"));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Need seek="50" urgent="25" critical="10" cost="25"/></Needs>)"), "a <Need> has no name"));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10" cost="25" mild="60"/></Needs>)"),
		"<Need name=\"Hunger\">: unknown attribute 'mild'"
	));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<Needs><Need name="Hunger" seek="50" critical="10" cost="25"/></Needs>)"), "<Need name=\"Hunger\"> urgent is missing"
	));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<Needs><Need name="Hunger" seek="150" urgent="25" critical="10" cost="25"/></Needs>)"),
		"<Need name=\"Hunger\"> seek must be a need percentage from 0 to 100, got \"150\""
	));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<Needs><Need name="Hunger" seek="50" urgent="60" critical="10" cost="25"/></Needs>)"), "urgent must be below seek"
	));
	EXPECT_TRUE(mentions(
		errorsFor(
			R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10" cost="25"/><Need name="Hunger" seek="50" urgent="25" critical="10" cost="25"/></Needs>)"
		),
		"<Need name=\"Hunger\"> is given twice"
	));
}

TEST(NeedsConfig, ACostIsRequiredANumberAndZeroOrMore) {
	const auto onlyError = [](const std::string& cost) {
		const std::vector<std::string> errors =
			errorsFor(R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10")" + cost + "/></Needs>");
		EXPECT_EQ(errors.size(), 1U) << cost;
		return errors.empty() ? std::string() : errors.front();
	};
	EXPECT_EQ(onlyError(""), "needs.xml: <Need name=\"Hunger\"> cost is missing");
	EXPECT_EQ(onlyError(R"( cost="-5")"), "needs.xml: <Need name=\"Hunger\"> cost must be zero or more, got \"-5\"");
	EXPECT_EQ(onlyError(R"( cost="6abc")"), "needs.xml: <Need name=\"Hunger\"> cost must be a number, got \"6abc\"");

	const NeedsConfigLoad load = NeedsFolder(
									 R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10" cost="25"/></Needs>)",
									 R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10"/></Needs>)"
	)
									 .load();
	EXPECT_TRUE(mentions(load.errors, "z-later.xml: <Need name=\"Hunger\"> cost is missing"));
	ASSERT_EQ(load.config.needs.size(), 1U);
	EXPECT_EQ(load.config.needs[0].moodCost, 25.0F) << "a later file with an error replaces nothing";
}
