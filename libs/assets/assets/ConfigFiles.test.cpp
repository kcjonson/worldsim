// Finding a config folder's files in load order.

#include "assets/ConfigFiles.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

	class ConfigFilesTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			folder = std::filesystem::temp_directory_path() /
					 ("worldsim-config-files-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
			std::filesystem::remove_all(folder);
			std::filesystem::create_directories(folder);
		}

		void TearDown() override { std::filesystem::remove_all(folder); }

		void touch(const std::string& name) {
			std::filesystem::create_directories((folder / name).parent_path());
			std::ofstream file(folder / name);
		}

		std::vector<std::string> relativeNames(const assets::ConfigFiles& found) const {
			std::vector<std::string> names;
			for (const std::filesystem::path& file : found.files) {
				names.push_back(std::filesystem::relative(file, folder).generic_string());
			}
			return names;
		}

		std::filesystem::path folder;
	};

} // namespace

TEST_F(ConfigFilesTest, TheFoldersOwnFilesComeFirstThenSubfoldersInPathOrder) {
	touch("zeta.xml");
	touch("alpha.xml");
	touch("mods/b/late.xml");
	touch("mods/a/early.xml");
	touch("aaa/first.xml");
	touch("notes.txt");

	const assets::ConfigFiles found = assets::configFilesIn(folder);
	ASSERT_FALSE(found.error.has_value()) << *found.error;
	EXPECT_EQ(
		relativeNames(found),
		(std::vector<std::string>{"alpha.xml", "zeta.xml", "aaa/first.xml", "mods/a/early.xml", "mods/b/late.xml"})
	);
}

TEST_F(ConfigFilesTest, ATrailingSeparatorOnTheFolderKeepsItsOwnFilesFirst) {
	touch("zeta.xml");
	touch("aaa/first.xml");

	const assets::ConfigFiles found = assets::configFilesIn(folder / "");
	ASSERT_FALSE(found.error.has_value()) << *found.error;
	EXPECT_EQ(relativeNames(found), (std::vector<std::string>{"zeta.xml", "aaa/first.xml"}));
}

TEST_F(ConfigFilesTest, AMissingFolderIsAnError) {
	const assets::ConfigFiles found = assets::configFilesIn(folder / "missing");
	ASSERT_TRUE(found.error.has_value());
	EXPECT_NE(found.error->find("not a folder"), std::string::npos) << *found.error;
	EXPECT_TRUE(found.files.empty());
}
