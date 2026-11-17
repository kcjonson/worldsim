// Loading need thresholds from data: the shipped folder, a later file replacing a need, and the mistakes
// the loader reports.

#include "assets/NeedsConfig.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>

using namespace assets;

namespace {

	std::filesystem::path shippedNeeds() {
		// <root>/libs/assets/assets/NeedsConfig.test.cpp -> <root>/assets/config/colonists/needs
		return std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path() / "assets" / "config" / "colonists" /
			   "needs";
	}

	/// A fresh folder holding `body` as needs.xml and, when given, `laterBody` as mods/later.xml.
	class NeedsFolder {
	  public:
		explicit NeedsFolder(const std::string& body, const std::string& laterBody = {})
			: folder(
				  std::filesystem::temp_directory_path() /
				  ("worldsim-needs-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()) + "-" +
				   std::to_string(nextFolder++))
			  ) {
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder / "mods");
			std::ofstream(folder / "needs.xml") << body;
			if (!laterBody.empty()) {
				std::ofstream(folder / "mods" / "later.xml") << laterBody;
			}
		}
		~NeedsFolder() { std::filesystem::remove_all(folder); }
		NeedsFolder(const NeedsFolder&) = delete;
		NeedsFolder& operator=(const NeedsFolder&) = delete;

		[[nodiscard]] NeedsConfigLoad load() const { return loadNeedsConfig(folder); }

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
	const NeedsConfigLoad load = loadNeedsConfig(shippedNeeds());
	for (const std::string& error : load.errors) {
		ADD_FAILURE() << error;
	}
	const auto hunger = std::ranges::find(load.config.needs, std::string("Hunger"), &NeedThresholdsDef::need);
	ASSERT_NE(hunger, load.config.needs.end());
	EXPECT_EQ(hunger->thresholds, (std::array{50.0F, 25.0F, 10.0F}));
}

TEST(NeedsConfig, ALaterFileReplacesANeedByName) {
	const NeedsFolder folder(
		R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10"/><Need name="Thirst" seek="50" urgent="25" critical="10"/></Needs>)",
		R"(<Needs><Need name="Hunger" seek="60" urgent="25" critical="10"/></Needs>)"
	);
	const NeedsConfigLoad load = folder.load();
	EXPECT_TRUE(load.errors.empty());
	ASSERT_EQ(load.config.needs.size(), 2U);
	EXPECT_EQ(load.config.needs[0].thresholds[0], 60.0F);
	EXPECT_EQ(load.config.needs[0].source, "mods/later.xml") << "a file is named relative to the needs folder, with forward slashes";
	EXPECT_EQ(load.config.needs[1].thresholds[0], 50.0F);
}

TEST(NeedsConfig, MistakesNameTheFileTheNeedAndTheAttributeAndLoadNothingFromTheFile) {
	const NeedsFolder broken(
		R"(<Needs><Need name="Thirst" seek="50" urgent="25" critical="10"/><Need name="Hunger" seek="50%" urgent="25" critical="10"/></Needs>)"
	);
	const NeedsConfigLoad load = broken.load();
	EXPECT_TRUE(mentions(load.errors, "needs.xml: <Need name=\"Hunger\"> seek must be a number, got \"50%\""));
	EXPECT_TRUE(load.config.needs.empty()) << "a file with any error adds nothing, its good <Need> included";

	EXPECT_TRUE(mentions(errorsFor("<Mood/>"), "the root element is not <Needs>"));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Want name="Hunger"/></Needs>)"), "unknown element <Want>"));
	EXPECT_TRUE(mentions(errorsFor(R"(<Needs><Need seek="50" urgent="25" critical="10"/></Needs>)"), "a <Need> has no name"));
	EXPECT_TRUE(mentions(
		errorsFor(R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10" mild="60"/></Needs>)"),
		"<Need name=\"Hunger\">: unknown attribute 'mild'"
	));
	EXPECT_TRUE(
		mentions(errorsFor(R"(<Needs><Need name="Hunger" seek="50" critical="10"/></Needs>)"), "<Need name=\"Hunger\"> urgent is missing")
	);
	EXPECT_TRUE(mentions(
		errorsFor(R"(<Needs><Need name="Hunger" seek="150" urgent="25" critical="10"/></Needs>)"),
		"<Need name=\"Hunger\"> seek must be a need percentage from 0 to 100, got \"150\""
	));
	EXPECT_TRUE(
		mentions(errorsFor(R"(<Needs><Need name="Hunger" seek="50" urgent="60" critical="10"/></Needs>)"), "urgent must be below seek")
	);
	EXPECT_TRUE(mentions(
		errorsFor(
			R"(<Needs><Need name="Hunger" seek="50" urgent="25" critical="10"/><Need name="Hunger" seek="50" urgent="25" critical="10"/></Needs>)"
		),
		"<Need name=\"Hunger\"> is given twice"
	));
}
