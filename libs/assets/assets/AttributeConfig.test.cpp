// Loading the colonist attribute ranges from data: files that merge, and each kind of load error.

#include "assets/AttributeConfig.h"
#include "assets/ConfigFiles.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <string>

using namespace assets;

namespace {

	constexpr std::array<std::string_view, 2> kNames = {"strength", "work_ethic"};

	class AttributeConfigTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			folder = std::filesystem::temp_directory_path() /
					 ("worldsim-attributes-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder);
		}

		void TearDown() override { std::filesystem::remove_all(folder); }

		void write(const std::string& name, const std::string& body) {
			std::ofstream file(folder / name);
			file << "<Attributes>" << body << "</Attributes>";
		}

		[[nodiscard]] AttributeConfigLoad load() const { return loadAttributeConfig(configFilesIn(folder).files, folder, kNames); }

		static bool mentions(const AttributeConfigLoad& load, const std::string& text) {
			return std::ranges::any_of(load.errors, [&](const std::string& error) { return error.find(text) != std::string::npos; });
		}

		std::filesystem::path folder;
	};

	constexpr const char* kBoth = R"(<Attribute name="strength" min="6" max="14"/><Attribute name="work_ethic" min="2" max="18"/>)";

} // namespace

TEST_F(AttributeConfigTest, LoadsEveryNameInTheOrderGivenAndALaterFileReplaces) {
	write("a.xml", kBoth);
	write("b.xml", R"(<Attribute name="work_ethic" min="5" max="7"/>)");
	const AttributeConfigLoad result = load();
	ASSERT_TRUE(result.errors.empty()) << result.errors.front();
	ASSERT_EQ(result.ranges.size(), 2U);
	EXPECT_FLOAT_EQ(result.ranges[0].max, 14.0F);
	EXPECT_FLOAT_EQ(result.ranges[1].min, 5.0F);
	EXPECT_FLOAT_EQ(result.ranges[1].max, 7.0F);
}

TEST_F(AttributeConfigTest, EachProblemNamesItsFileAndAttributeAndLoadsNothing) {
	const std::vector<std::pair<std::string, std::string>> cases = {
		{R"(<Attribute name="luck" min="1" max="2"/>)", "bad.xml: <Attribute name=\"luck\">: no such attribute"},
		{R"(<Attribute name="strength" min="1" max="2" mean="1"/>)", "bad.xml: <Attribute name=\"strength\">: unknown attribute 'mean'"},
		{R"(<Attribute name="strength" min="1"/>)", "bad.xml: <Attribute name=\"strength\"> max is missing"},
		{R"(<Attribute name="strength" min="1" max="lots"/>)", "bad.xml: <Attribute name=\"strength\"> max must be a number"},
		{R"(<Attribute name="strength" min="9" max="3"/>)", "bad.xml: <Attribute name=\"strength\">: min"},
		{R"(<Attribute name="strength" min="-1" max="3"/>)", "bad.xml: <Attribute name=\"strength\">: min"},
		{R"(<Attribute name="strength" min="1" max="21"/>)", "bad.xml: <Attribute name=\"strength\">: min"},
		{R"(<Attribute name="strength" min="1" max="2"/><Attribute name="strength" min="1" max="2"/>)",
		 "bad.xml: <Attribute name=\"strength\"> is given twice"},
		{R"(<Range name="strength"/>)", "bad.xml: unknown element <Range>"},
		{"stray words", "bad.xml: unexpected text inside <Attributes>"},
	};
	for (const auto& [body, expected] : cases) {
		write("a.xml", kBoth);
		write("bad.xml", body);
		const AttributeConfigLoad result = load();
		EXPECT_TRUE(mentions(result, expected)) << expected << " in " << (result.errors.empty() ? "no errors" : result.errors.front());
		EXPECT_TRUE(result.ranges.empty()) << expected;
	}
}

TEST_F(AttributeConfigTest, ABadFileIsTheOnlyErrorForTheAttributesItHeld) {
	write("a.xml", R"(<Attribute name="strength" min="6" max="14"/>)");
	write("bad.xml", R"(<Attribute name="work_ethic" min="9" max="3"/>)");
	const AttributeConfigLoad result = load();
	ASSERT_EQ(result.errors.size(), 1U);
	EXPECT_NE(result.errors.front().find("bad.xml"), std::string::npos);
}

TEST_F(AttributeConfigTest, AnAttributeNoFileGivesIsAnError) {
	write("a.xml", R"(<Attribute name="strength" min="6" max="14"/>)");
	const AttributeConfigLoad missing = load();
	EXPECT_TRUE(mentions(missing, "no file gives <Attribute name=\"work_ethic\">"));
	EXPECT_TRUE(missing.ranges.empty());
}
