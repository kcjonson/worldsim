// Finding a creature folder's config files by their root element.

#include "assets/CreatureConfigFiles.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace assets;

namespace {

	class CreatureConfigFilesTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			root = std::filesystem::temp_directory_path() /
				   ("worldsim-creature-config-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
			folder = root / "creatures" / "Cow";
			std::filesystem::remove_all(root);
			std::filesystem::create_directories(folder);
			write(
				"Cow.xml",
				"<AssetDefinitions><AssetDef><defName>Cow</defName><motion>walk.motion.xml</motion></AssetDef></AssetDefinitions>"
			);
			write("walk.motion.xml", "<Motion/>");
		}

		void TearDown() override { std::filesystem::remove_all(root); }

		void write(const std::string& name, const std::string& body) const { std::ofstream(folder / name) << body; }

		static std::vector<std::string> names(const ConfigFiles& found) {
			std::vector<std::string> named;
			for (const std::filesystem::path& file : found.files) {
				named.push_back(file.filename().string());
			}
			return named;
		}

		std::filesystem::path root;
		std::filesystem::path folder;
	};

} // namespace

TEST_F(CreatureConfigFilesTest, EachFileGoesToItsRootsListInFileNameOrder) {
	write("traits.xml", "<Traits/>");
	write("a-more-traits.xml", "<!-- a comment first --><Traits/>");
	write("mood.xml", "<Mood/>");
	write("memory.xml", "<Memory/>");
	write("attributes.xml", "<Attributes/>");
	write("needs.xml", "<Needs/>");
	write("notes.txt", "not xml");

	const CreatureConfigFiles found = creatureConfigFiles(folder);
	EXPECT_TRUE(found.errors.empty()) << found.errors.front();
	EXPECT_EQ(names(found.traits), (std::vector<std::string>{"a-more-traits.xml", "traits.xml"}));
	EXPECT_EQ(names(found.mood), (std::vector<std::string>{"mood.xml"}));
	EXPECT_EQ(names(found.memory), (std::vector<std::string>{"memory.xml"}));
	EXPECT_EQ(names(found.attributes), (std::vector<std::string>{"attributes.xml"}));
	EXPECT_EQ(names(found.needs), (std::vector<std::string>{"needs.xml"}));
	EXPECT_EQ(found.folder, folder);
}

TEST_F(CreatureConfigFilesTest, TheDefinitionAndItsMotionFileAreNotConfig) {
	const CreatureConfigFiles found = creatureConfigFiles(folder / "");
	EXPECT_TRUE(found.errors.empty()) << found.errors.front();
	EXPECT_TRUE(found.mood.files.empty() && found.traits.files.empty() && found.needs.files.empty());
}

TEST_F(CreatureConfigFilesTest, AnotherRootOrUnreadableXmlIsAnErrorNamingTheFile) {
	write("herd.xml", "<Herd/>");
	write("broken.xml", "<Mood>");
	write("mood.xml", "<Mood/>");

	const CreatureConfigFiles found = creatureConfigFiles(folder);
	ASSERT_EQ(found.errors.size(), 2U);
	EXPECT_EQ(found.errors[0].rfind("broken.xml: not readable XML", 0), 0U) << found.errors[0];
	EXPECT_EQ(
		found.errors[1],
		"herd.xml: the root element <Herd> is not one a creature folder holds (<Mood>, <Memory>, <Traits>, <Attributes>, <Needs>)"
	);
	EXPECT_EQ(names(found.mood), (std::vector<std::string>{"mood.xml"}));
}

TEST_F(CreatureConfigFilesTest, AMissingFolderIsAnError) {
	const CreatureConfigFiles found = creatureConfigFiles(folder / "missing");
	ASSERT_EQ(found.errors.size(), 1U);
	EXPECT_NE(found.errors.front().find("not a folder"), std::string::npos);
}

TEST_F(CreatureConfigFilesTest, OnlyACreatureFoldersConfigRootsAreConfigFiles) {
	write("mood.xml", "<Mood/>");
	write("herd.xml", "<Herd/>");
	EXPECT_TRUE(isCreatureConfigFile(folder / "mood.xml"));
	EXPECT_FALSE(isCreatureConfigFile(folder / "herd.xml"));
	EXPECT_FALSE(isCreatureConfigFile(folder / "walk.motion.xml"));
	std::filesystem::create_directories(root / "flora" / "Bush");
	std::ofstream(root / "flora" / "Bush" / "mood.xml") << "<Mood/>";
	EXPECT_FALSE(isCreatureConfigFile(root / "flora" / "Bush" / "mood.xml")) << "only under creatures/";
}
