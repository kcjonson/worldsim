// Finding a creature folder's config files by their root element, and the one error each fault gives.

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
			write("mood.xml", "<Mood/>");
			write("memory.xml", "<Memory/>");
			write("traits.xml", "<Traits/>");
			write("attributes.xml", "<Attributes/>");
			write("needs.xml", "<Needs/>");
		}

		void TearDown() override { std::filesystem::remove_all(root); }

		void write(const std::string& name, const std::string& body) const { std::ofstream(folder / name) << body; }

		/// The folder's one error; fails the test when there is not exactly one.
		[[nodiscard]] std::string onlyError() const {
			const CreatureConfigFiles found = creatureConfigFiles(folder);
			EXPECT_EQ(found.errors.size(), 1U) << ::testing::PrintToString(found.errors);
			return found.errors.size() == 1 ? found.errors.front() : std::string();
		}

		static std::vector<std::string> names(const std::vector<std::filesystem::path>& files) {
			std::vector<std::string> named;
			for (const std::filesystem::path& file : files) {
				named.push_back(file.filename().string());
			}
			return named;
		}

		std::filesystem::path root;
		std::filesystem::path folder;
	};

	constexpr const char* kRoots = "(<Mood>, <Memory>, <Traits>, <Attributes>, <Needs>)";

} // namespace

TEST_F(CreatureConfigFilesTest, EachFileGoesToItsRootsListInFileNameOrder) {
	write("a-more-traits.xml", "<!-- a comment first --><Traits/>");
	write("notes.txt", "not xml");

	const CreatureConfigFiles found = creatureConfigFiles(folder);
	EXPECT_TRUE(found.errors.empty()) << ::testing::PrintToString(found.errors);
	EXPECT_EQ(names(found.traits), (std::vector<std::string>{"a-more-traits.xml", "traits.xml"}));
	EXPECT_EQ(names(found.mood), (std::vector<std::string>{"mood.xml"}));
	EXPECT_EQ(names(found.memory), (std::vector<std::string>{"memory.xml"}));
	EXPECT_EQ(names(found.attributes), (std::vector<std::string>{"attributes.xml"}));
	EXPECT_EQ(names(found.needs), (std::vector<std::string>{"needs.xml"}));
	EXPECT_EQ(found.folder, folder);
}

TEST_F(CreatureConfigFilesTest, TheDefinitionAndItsMotionFileAreNotConfig) {
	const CreatureConfigFiles found = creatureConfigFiles(folder / "");
	EXPECT_TRUE(found.errors.empty()) << ::testing::PrintToString(found.errors);
	EXPECT_EQ(names(found.mood), (std::vector<std::string>{"mood.xml"}));
}

TEST_F(CreatureConfigFilesTest, AnotherRootIsOneErrorNamingTheFile) {
	write("herd.xml", "<Herd/>");
	EXPECT_EQ(onlyError(), std::string("[creatures] herd.xml: the root element <Herd> is not one a creature folder holds ") + kRoots);
}

TEST_F(CreatureConfigFilesTest, ABrokenOrMisRootedMoodFileIsOneErrorNamingIt) {
	write("mood.xml", "<Mood><Easing pointsPerGameHour=\"5\">");
	EXPECT_EQ(onlyError().rfind("[creatures] mood.xml: not readable XML: ", 0), 0U);

	write("mood.xml", "<Mod/>");
	EXPECT_EQ(onlyError(), std::string("[creatures] mood.xml: the root element <Mod> is not one a creature folder holds ") + kRoots);

	write("mood.xml", "<Mood/>");
	write("memory.xml", "<Memory>");
	EXPECT_EQ(onlyError().rfind("[creatures] memory.xml: not readable XML: ", 0), 0U);
}

TEST_F(CreatureConfigFilesTest, AKindWithNoFileIsOneErrorNamingTheFolder) {
	std::filesystem::remove(folder / "needs.xml");
	EXPECT_EQ(onlyError(), "[creatures] " + folder.generic_string() + ": no file has a <Needs> root");

	write("needs.xml", "<Needs/>");
	std::filesystem::remove(folder / "traits.xml");
	EXPECT_EQ(onlyError(), "[creatures] " + folder.generic_string() + ": no file has a <Traits> root");
}

TEST_F(CreatureConfigFilesTest, ASecondMoodFileIsOneError) {
	write("z-mood.xml", "<Mood/>");
	EXPECT_EQ(onlyError(), "[creatures] " + folder.generic_string() + ": more than one file has a <Mood> root: mood.xml, z-mood.xml");
}

TEST_F(CreatureConfigFilesTest, ASubfolderIsAnErrorNamingIt) {
	std::filesystem::create_directories(folder / "mods");
	std::ofstream(folder / "mods" / "traits.xml") << "<Traits/>";
	EXPECT_EQ(onlyError(), "[creatures] mods: a subfolder, which a creature folder may not have");
}

TEST_F(CreatureConfigFilesTest, AnUnreadableDefinitionIsTheOneError) {
	write("Cow.xml", "<AssetDefinitions><AssetDef>");
	const std::string error = onlyError();
	EXPECT_EQ(error.rfind("[creatures] Cow.xml: not readable XML: ", 0), 0U) << error;
	EXPECT_EQ(error.find("walk.motion.xml"), std::string::npos) << "nothing derived from the definition it could not read";
}

TEST_F(CreatureConfigFilesTest, AMissingFolderIsAnError) {
	const CreatureConfigFiles found = creatureConfigFiles(folder / "missing");
	ASSERT_EQ(found.errors.size(), 1U);
	EXPECT_EQ(found.errors.front(), "[creatures] " + (folder / "missing").generic_string() + ": not a folder");
}

TEST_F(CreatureConfigFilesTest, ErrorsAreTakenOnceAndStopEveryLoader) {
	write("herd.xml", "<Herd/>");
	CreatureConfigFiles		 found = creatureConfigFiles(folder);
	std::vector<std::string> first;
	std::vector<std::string> second;
	EXPECT_TRUE(takeErrors(found, first));
	EXPECT_TRUE(takeErrors(found, second));
	EXPECT_EQ(first.size(), 1U);
	EXPECT_TRUE(second.empty());

	std::filesystem::remove(folder / "herd.xml");
	CreatureConfigFiles clean = creatureConfigFiles(folder);
	EXPECT_FALSE(takeErrors(clean, first));
}

TEST_F(CreatureConfigFilesTest, OnlyFilesDirectlyInACreatureFolderAreTheCreatures) {
	EXPECT_TRUE(isInCreatureFolder(folder / "mood.xml"));
	EXPECT_TRUE(isInCreatureFolder(folder / "herd.xml"));
	EXPECT_FALSE(isInCreatureFolder(root / "flora" / "Bush" / "mood.xml")) << "only under creatures/";
	EXPECT_FALSE(isInCreatureFolder(folder / "mods" / "traits.xml"));
}
