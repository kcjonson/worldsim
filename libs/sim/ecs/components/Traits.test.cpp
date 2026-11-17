#include "Traits.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace ecs;

namespace {

	std::filesystem::path shippedTraits() {
		return std::filesystem::path(SIM_TEST_REPO_DIR) / "assets" / "config" / "colonists" / "traits";
	}

	/// A registry loaded from `body`, the inside of a <Traits> file.
	assets::TraitRegistry registryOf(const std::string& body) {
		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "worldsim-roll-traits";
		std::filesystem::remove_all(folder);
		std::filesystem::create_directories(folder);
		{
			std::ofstream file(folder / "traits.xml");
			file << "<Traits>" << body << "</Traits>";
		}
		assets::TraitRegistry registry;
		const std::vector<std::string> errors = registry.loadFromFolder(folder);
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
	assets::TraitRegistry registry;
	ASSERT_TRUE(registry.loadFromFolder(shippedTraits()).empty());
	std::set<std::size_t> counts;
	for (uint32_t seed = 0; seed < 300; ++seed) {
		std::mt19937 rng(seed);
		const Traits traits = rollTraits(registry, rng);
		counts.insert(traits.ids.size());
		EXPECT_GE(traits.ids.size(), 1U);
		EXPECT_LE(traits.ids.size(), 3U);
		EXPECT_EQ(std::set(traits.ids.begin(), traits.ids.end()).size(), traits.ids.size());
		for (const std::string& id : traits.ids) {
			EXPECT_NE(registry.find(id), nullptr) << id;
		}
	}
	EXPECT_EQ(counts, (std::set<std::size_t>{1, 2, 3}));
}

TEST(RollTraits, ExclusivePairsNeverLandOnOneColonist) {
	assets::TraitRegistry registry;
	ASSERT_TRUE(registry.loadFromFolder(shippedTraits()).empty());
	bool sawGourmand = false;
	bool sawAscetic = false;
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
	assets::TraitRegistry registry;
	ASSERT_TRUE(registry.loadFromFolder(shippedTraits()).empty());
	for (uint32_t seed = 0; seed < 50; ++seed) {
		std::mt19937 first(seed);
		std::mt19937 second(seed);
		EXPECT_EQ(rollTraits(registry, first).ids, rollTraits(registry, second).ids) << "seed " << seed;
	}
}

TEST(RollTraits, FewerComeBackWhenExclusionsLeaveTooFewToDraw) {
	const assets::TraitRegistry registry =
		registryOf(R"(<Count min="3" max="3"/>)" + traitXml("one") + traitXml("two") + traitXml("three") + "<Exclusive>one two three</Exclusive>");
	for (uint32_t seed = 0; seed < 20; ++seed) {
		std::mt19937 rng(seed);
		EXPECT_EQ(rollTraits(registry, rng).ids.size(), 1U);
	}
}

TEST(RollTraits, AFixedCountGivesExactlyThatMany) {
	const assets::TraitRegistry registry =
		registryOf(R"(<Count min="2" max="2"/>)" + traitXml("one") + traitXml("two") + traitXml("three") + traitXml("four"));
	for (uint32_t seed = 0; seed < 20; ++seed) {
		std::mt19937 rng(seed);
		EXPECT_EQ(rollTraits(registry, rng).ids.size(), 2U);
	}
}

TEST(RollTraits, ACountOfZeroGivesNone) {
	const assets::TraitRegistry registry = registryOf(R"(<Count min="0" max="0"/>)" + traitXml("one"));
	std::mt19937				rng(1);
	EXPECT_TRUE(rollTraits(registry, rng).ids.empty());
}
