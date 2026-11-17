// Loading need thresholds from data: the shipped file, and the mistakes the loader reports.

#include "assets/NeedsConfig.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace assets;

namespace {

	std::filesystem::path shippedNeeds() {
		// <root>/libs/assets/assets/NeedsConfig.test.cpp -> <root>/assets/config/colonists/needs.xml
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "colonists" /
			   "needs.xml";
	}

	std::vector<std::string> errorsFor(const std::string& body) {
		const std::filesystem::path file =
			std::filesystem::temp_directory_path() /
			("worldsim-needs-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + ".xml");
		{
			std::ofstream out(file);
			out << body;
		}
		std::vector<std::string> errors = loadNeedsConfig(file).errors;
		std::filesystem::remove(file);
		return errors;
	}

	bool mentions(const std::vector<std::string>& errors, const std::string& text) {
		return std::ranges::any_of(errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
	}

} // namespace

TEST(NeedsConfig, TheShippedNeedsLoadClean) {
	const NeedsConfigLoad load = loadNeedsConfig(shippedNeeds());
	for (const std::string& error : load.errors) {
		ADD_FAILURE() << error;
	}
	const auto hunger = std::ranges::find(load.config.needs, std::string("Hunger"), &NeedThresholdsDef::need);
	ASSERT_NE(hunger, load.config.needs.end());
	EXPECT_EQ(hunger->thresholds, (std::array{50.0F, 25.0F, 10.0F}));
}

TEST(NeedsConfig, MistakesNameTheNeedAndTheAttribute) {
	EXPECT_TRUE(mentions(errorsFor("<Mood/>"), "the root element is not <Needs>"));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Want name="Hunger"/></Needs>)"), "unknown element <Want>"));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Need seek="50" urgent="25" critical="10"/></Needs>)"), "a <Need> has no name"));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10" mild="60"/></Needs>)"),
		"<Need name=\"Hunger\">: unknown attribute 'mild'"
	));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Need name="Hunger" seek="50" critical="10"/></Needs>)"), "urgent=\"\" is not a need percentage"));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Need name="Hunger" seek="50%" urgent="25" critical="10"/></Needs>)"), "seek=\"50%\""));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Need name="Hunger" seek="50" urgent="60" critical="10"/></Needs>)"), "urgent must be below seek"));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10"/><Need name="Hunger" seek="50" urgent="25" critical="10"/></Needs>)"),
		"is given twice"
	));
}
