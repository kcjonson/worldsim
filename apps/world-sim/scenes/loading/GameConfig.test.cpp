// The loading scene's config load: a broken human folder stops it before either loader.

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

TEST(GameConfig, ABrokenHumanFolderIsTheLoadsOnlyErrorsAndNeitherLoaderRuns) {
	const std::filesystem::path configRoot = repoRoot() / "assets" / "config";
	const std::filesystem::path shippedHuman = repoRoot() / "assets" / "world" / "creatures" / "Human";

	// Loaded alone as a marker: loadWorkConfig clears it first, so it still being loaded after means that never ran. The
	// whole load is not run here, since its registries are process-wide and the scene tests load their own.
	ASSERT_TRUE(assets::MoodConfig::Get().loadFromFile(shippedHuman, "mood.xml"));

	const std::filesystem::path root =
		std::filesystem::temp_directory_path() / ("worldsim-game-config-" + std::to_string(std::random_device{}()));
	const std::filesystem::path broken = root / "creatures" / "Human";
	std::filesystem::create_directories(broken);
	std::filesystem::copy(shippedHuman, broken, std::filesystem::copy_options::recursive);
	std::ofstream(broken / "herd.xml") << "<Herd/>";

	const world_sim::GameConfigLoad load = world_sim::loadGameConfig(configRoot, broken);
	EXPECT_EQ(
		load.errors,
		std::vector<std::string>{
			"[creatures] herd.xml: the root element <Herd> is not one a creature folder holds (<Mood>, <Memory>, <Traits>, <Attributes>, "
			"<Needs>)"
		}
	);
	EXPECT_TRUE(assets::MoodConfig::Get().loaded()) << "loadWorkConfig, which clears the mood config first, never ran";
	EXPECT_EQ(load.rules.traits, nullptr) << "nor did the colonist rules";
	std::filesystem::remove_all(root);
	assets::MoodConfig::Get().clear();
}
