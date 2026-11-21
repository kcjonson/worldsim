// The loading scene's config load: a broken or incomplete human folder stops it before either loader.

#include "GameConfig.h"

#include <assets/MoodConfig.h>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace {

	std::filesystem::path repoRoot() {
		// <root>/apps/world-sim/scenes/loading/GameConfig.test.cpp -> <root>
		return std::filesystem::path(__FILE__).lexically_normal().parent_path().parent_path().parent_path().parent_path().parent_path();
	}

} // namespace

namespace {

	/// A copy of the shipped human's folder under a fresh temp root, removed with the fixture.
	class GameConfigTest : public ::testing::Test {
	  protected:
		void SetUp() override {
			root = std::filesystem::temp_directory_path() / ("worldsim-game-config-" + std::to_string(std::random_device{}()));
			human = root / "creatures" / "Human";
			std::filesystem::create_directories(human);
			std::filesystem::copy(shippedHuman(), human, std::filesystem::copy_options::recursive);
			// A marker: loadWorkConfig clears the mood config first and, given a config root that does not exist,
			// fails without loading it again, so it still being loaded after means loadWorkConfig never ran.
			ASSERT_TRUE(assets::MoodConfig::Get().loadFromFile(shippedHuman(), "mood.xml"));
		}

		void TearDown() override {
			std::filesystem::remove_all(root);
			assets::MoodConfig::Get().clear();
		}

		static std::filesystem::path shippedHuman() { return repoRoot() / "assets" / "world" / "creatures" / "Human"; }

		/// The load with a config root that does not exist: any loader that runs adds an error of its own.
		[[nodiscard]] world_sim::GameConfigLoad load() const { return world_sim::loadGameConfig(root / "no-such-config", human); }

		std::filesystem::path root;
		std::filesystem::path human;
	};

} // namespace

TEST_F(GameConfigTest, ABrokenHumanFolderIsTheLoadsOnlyErrorsAndNeitherLoaderRuns) {
	std::ofstream(human / "herd.xml") << "<Herd/>";

	const world_sim::GameConfigLoad result = load();
	EXPECT_EQ(
		result.errors,
		std::vector<std::string>{
			"[creatures] herd.xml: the root element <Herd> is not one a creature folder holds (<Mood>, <Memory>, <Traits>, <Attributes>, "
			"<Needs>)"
		}
	);
	EXPECT_TRUE(assets::MoodConfig::Get().loaded()) << "loadWorkConfig never ran";
	EXPECT_EQ(result.rules.traits, nullptr) << "nor did the colonist rules";
}

TEST_F(GameConfigTest, EachKindTheHumanLacksIsOneErrorAndNeitherLoaderRuns) {
	std::filesystem::remove(human / "needs.xml");
	std::filesystem::remove(human / "memory.xml");

	const world_sim::GameConfigLoad result = load();
	EXPECT_EQ(
		result.errors,
		(std::vector<std::string>{
			"[creatures] creatures/Human: no file has a <Memory> root",
			"[creatures] creatures/Human: no file has a <Needs> root",
		})
	);
	EXPECT_TRUE(assets::MoodConfig::Get().loaded()) << "loadWorkConfig never ran";
	EXPECT_EQ(result.rules.traits, nullptr) << "nor did the colonist rules";
}
