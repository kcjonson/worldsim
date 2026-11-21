// The loading scene's config load: a broken or incomplete human folder stops it before either loader.

#include "GameConfig.h"

#include <assets/ActionTypeRegistry.h>
#include <assets/ConfigValidator.h>
#include <assets/ConstructionRegistry.h>
#include <assets/MemoryConfig.h>
#include <assets/MoodConfig.h>
#include <assets/PriorityConfig.h>
#include <assets/WorkTypeRegistry.h>

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
		[[nodiscard]] world_sim::GameConfigLoad load() const {
			return world_sim::loadGameConfig(root / "no-such-config", root / "creatures");
		}

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
			"<Needs>, <creature>, <behavior>)"
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

namespace {

	/// The whole shipped config copied beside the human, so every loader runs; the work config's registries are
	/// cleared after, as the load fills them.
	class GameConfigNamesTest : public GameConfigTest {
	  protected:
		void SetUp() override {
			GameConfigTest::SetUp();
			std::filesystem::copy(repoRoot() / "assets" / "config", root / "config", std::filesystem::copy_options::recursive);
		}

		void TearDown() override {
			GameConfigTest::TearDown();
			assets::ActionTypeRegistry::Get().clear();
			assets::WorkTypeRegistry::Get().clear();
			assets::PriorityConfig::Get().clear();
			assets::ConstructionRegistry::Get().clear();
			assets::MemoryConfig::Get().clear();
			assets::ConfigValidator::clearErrors();
		}

		/// A behavior naming `kind` in a query, `trait` in a scale row, and `thought` in a think step.
		void behaviorNaming(const std::string& kind, const std::string& trait, const std::string& thought) const {
			std::filesystem::create_directories(root / "config" / "behaviors" / "test");
			std::ofstream(root / "config" / "behaviors" / "test" / "names.xml") << R"(<behavior id="names"><idle chain="look"/>
  <chain id="look"><go query="near"/><think thought=")" << thought << R"(" to="self"/></chain>
  <query id="near"><from kind=")" << kind << R"("/><weigh fact="distance" by="-1"/><scale trait=")"
																				<< trait << R"(" fact="distance" by="2"/></query>
</behavior>)";
		}

		[[nodiscard]] world_sim::GameConfigLoad loadAll() const { return world_sim::loadGameConfig(root / "config", root / "creatures"); }
	};

} // namespace

TEST_F(GameConfigNamesTest, EntriesNamingAnUnknownKindTraitOrThoughtFailTheLoad) {
	behaviorNaming("food", "night_owl", "ate_good_meal");
	const world_sim::GameConfigLoad known = loadAll();
	EXPECT_TRUE(known.errors.empty()) << ::testing::PrintToString(known.errors);
	EXPECT_TRUE(known.entries.findChain("test/look").valid()) << "a known kind, trait, and thought load clean";

	behaviorNaming("fod", "night_owel", "ate_god_meal");
	const world_sim::GameConfigLoad unknown = loadAll();
	EXPECT_EQ(
		unknown.errors,
		(std::vector<std::string>{
			"[behaviors] test/names.xml: <query id=\"near\"> row 1 <from> kind \"fod\" names no kind",
			"[behaviors] test/names.xml: <query id=\"near\"> row 3 <scale> trait \"night_owel\" names no trait",
			"[behaviors] test/names.xml: <chain id=\"look\"> step 2 <think> thought \"ate_god_meal\" names no thought type",
		})
	);
	EXPECT_FALSE(unknown.entries.findChain("test/look").valid()) << "a failed load leaves the library empty";
}
