#include "Traits.h"

#include "../ShippedConfig.test.h"

#include <assets/ConfigFiles.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <string>

using namespace ecs;

namespace {

	/// A registry loaded from `body`, the inside of a <Traits> file, through a folder of this test's own:
	/// other worktrees run sim-tests at the same time.
	assets::TraitRegistry registryOf(const std::string& body) {
		static const std::string	processToken = std::to_string(std::random_device{}());
		const std::filesystem::path folder =
			std::filesystem::temp_directory_path() /
			("worldsim-roll-traits-" + processToken + "-" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
		std::filesystem::remove_all(folder);
		std::filesystem::create_directories(folder);
		{
			std::ofstream file(folder / "traits.xml");
			file << "<Traits>" << body << "</Traits>";
		}
		assets::TraitRegistry		   registry;
		const std::vector<std::string> errors = registry.loadFromFiles(assets::configFilesIn(folder).files, folder);
		std::filesystem::remove_all(folder);
		EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
		return registry;
	}

	std::string traitXml(const std::string& id) {
		return "<Trait id=\"" + id + "\" name=\"Name\" description=\"Description.\"/>";
	}

	std::vector<std::string> sorted(std::vector<std::string> ids) {
		std::ranges::sort(ids);
		return ids;
	}

} // namespace

TEST(RollTraits, TheCountStaysWithinTheRangeAndNoTraitRepeats) {
	const assets::TraitRegistry registry = ecs::testing::shippedTraits();
	std::set<std::size_t>		counts;
	for (uint32_t seed = 0; seed < 300; ++seed) {
		std::mt19937 rng(seed);
		const Traits traits = rollTraits(registry, rng);
		counts.insert(traits.ids.size());
		EXPECT_EQ(std::set(traits.ids.begin(), traits.ids.end()).size(), traits.ids.size());
		for (const std::string& id : traits.ids) {
			EXPECT_NE(registry.find(id), nullptr) << id;
		}
	}
	EXPECT_EQ(counts, (std::set<std::size_t>{1, 2, 3}));
}

TEST(RollTraits, ExclusivePairsNeverLandOnOneColonist) {
	const assets::TraitRegistry registry = ecs::testing::shippedTraits();
	bool						sawGourmand = false;
	bool						sawAscetic = false;
	for (uint32_t seed = 0; seed < 500; ++seed) {
		std::mt19937 rng(seed);
		const Traits traits = rollTraits(registry, rng);
		const bool	 gourmand = std::ranges::find(traits.ids, "gourmand") != traits.ids.end();
		const bool	 ascetic = std::ranges::find(traits.ids, "ascetic") != traits.ids.end();
		EXPECT_FALSE(gourmand && ascetic) << "seed " << seed;
		sawGourmand = sawGourmand || gourmand;
		sawAscetic = sawAscetic || ascetic;
	}
	EXPECT_TRUE(sawGourmand);
	EXPECT_TRUE(sawAscetic);
}

TEST(RollTraits, TheSameSeedGivesTheSameTraits) {
	const assets::TraitRegistry registry = ecs::testing::shippedTraits();
	for (uint32_t seed = 0; seed < 50; ++seed) {
		std::mt19937 first(seed);
		std::mt19937 second(seed);
		EXPECT_EQ(rollTraits(registry, first).ids, rollTraits(registry, second).ids) << "seed " << seed;
	}
}

TEST(RollTraits, TheMinimumIsAlwaysRolledEvenWhenADrawCouldStrandIt) {
	const assets::TraitRegistry registry = registryOf(
		R"(<Count min="2" max="2"/>)" + traitXml("a") + traitXml("b") + traitXml("c") + "<Exclusive>a b</Exclusive><Exclusive>a c</Exclusive>"
	);
	for (uint32_t seed = 0; seed < 50; ++seed) {
		std::mt19937 rng(seed);
		EXPECT_EQ(sorted(rollTraits(registry, rng).ids), (std::vector<std::string>{"b", "c"})) << "seed " << seed;
	}
}

TEST(RollTraits, AMaxAboveWhatFitsGivesAsManyAsFit) {
	const assets::TraitRegistry registry = registryOf(
		R"(<Count min="1" max="3"/>)" + traitXml("a") + traitXml("b") + traitXml("c") + "<Exclusive>a b</Exclusive><Exclusive>a c</Exclusive>"
	);
	std::set<std::size_t> counts;
	for (uint32_t seed = 0; seed < 100; ++seed) {
		std::mt19937		   rng(seed);
		const Traits		   traits = rollTraits(registry, rng);
		const std::set<std::string> held(traits.ids.begin(), traits.ids.end());
		counts.insert(traits.ids.size());
		EXPECT_FALSE(held.contains("a") && held.size() > 1) << "seed " << seed;
	}
	EXPECT_EQ(counts, (std::set<std::size_t>{1, 2})) << "a roll of 2 or 3 gives b and c";
}

TEST(RollTraits, ACountOfZeroGivesNone) {
	const assets::TraitRegistry registry = registryOf(R"(<Count min="0" max="0"/>)" + traitXml("one"));
	std::mt19937				rng(1);
	EXPECT_TRUE(rollTraits(registry, rng).ids.empty());
}
