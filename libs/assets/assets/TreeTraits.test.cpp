#include "assets/TreeTraits.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/placement/GroveField.h"

#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

namespace assets {
	namespace {

		constexpr uint64_t kSeed = 424242;

		AssetDefinition agedTree() {
			AssetDefinition def;
			def.defName = "Flora_TestAged";
			def.ageClasses = {
				{.name = "sapling", .weight = 0.25F, .variants = 4},
				{.name = "mature", .weight = 0.55F, .variants = 8},
				{.name = "veteran", .weight = 0.20F, .variants = 4},
			};
			return def;
		}

		// Positions spread over 20 km, far wider than the age field's patches.
		std::vector<glm::vec2> spreadPositions(size_t count) {
			std::mt19937						  rng(99);
			std::uniform_real_distribution<float> coord(-10000.0F, 10000.0F);
			std::vector<glm::vec2>				  out(count);
			for (glm::vec2& p : out) {
				p = {coord(rng), coord(rng)};
			}
			return out;
		}

	} // namespace

	TEST(TreeTraits, DeterministicAndSharedWithinADecimeterCell) {
		const AssetDefinition def = agedTree();
		for (const glm::vec2 p : spreadPositions(200)) {
			EXPECT_EQ(computeTreeTraits(def, p, kSeed), computeTreeTraits(def, p, kSeed));
		}
		// Both in cell (123, 400) of the 0.1 m grid colonist memory uses.
		EXPECT_EQ(computeTreeTraits(def, {12.31F, 40.07F}, kSeed), computeTreeTraits(def, {12.39F, 40.01F}, kSeed));
	}

	// Over 10,000 trees the age mix is the def's weights within 0.03 each, and every
	// variant of an age class turns up at least half as often as an even share. Thickets
	// lean young, so the mix there has more saplings than the weights.
	TEST(TreeTraits, AgeMixHitsTheWeightsAndVariantsSpread) {
		constexpr double kWeightTolerance = 0.03;
		const AssetDefinition def = agedTree();
		const std::vector<glm::vec2> positions = spreadPositions(10000);

		std::array<int, 3>				ages{};
		std::array<std::vector<int>, 3> variants{std::vector<int>(4), std::vector<int>(8), std::vector<int>(4)};
		for (const glm::vec2 p : positions) {
			const TreeTraits t = computeTreeTraits(def, p, kSeed);
			ASSERT_LT(t.ageClass, 3);
			ASSERT_LT(t.variant, def.ageClasses[t.ageClass].variants);
			++ages[t.ageClass];
			++variants[t.ageClass][t.variant];
		}
		for (size_t i = 0; i < ages.size(); ++i) {
			EXPECT_NEAR(static_cast<double>(ages[i]) / positions.size(), def.ageClasses[i].weight, kWeightTolerance) << def.ageClasses[i].name;
			const double evenShare = static_cast<double>(ages[i]) / variants[i].size();
			for (const int n : variants[i]) {
				EXPECT_GE(n, evenShare * 0.5) << def.ageClasses[i].name;
			}
		}

		int thicketTrees = 0;
		int thicketSaplings = 0;
		for (const glm::vec2 p : spreadPositions(200000)) {
			if (grove::thicketWeight(grove::groveValue(p.x, p.y, kSeed)) > 0.5F) {
				++thicketTrees;
				thicketSaplings += computeTreeTraits(def, p, kSeed).ageClass == 0 ? 1 : 0;
			}
		}
		ASSERT_GT(thicketTrees, 500);
		EXPECT_GT(static_cast<double>(thicketSaplings) / thicketTrees, def.ageClasses[0].weight + 0.1);
	}

	TEST(TreeTraits, DefWithoutAgeClassesHasOneClassOfItsVariants) {
		AssetDefinition def;
		def.defName = "Flora_TestPlain";
		def.variantCount = 6;
		std::array<int, 6> seen{};
		for (const glm::vec2 p : spreadPositions(600)) {
			const TreeTraits t = computeTreeTraits(def, p, kSeed);
			EXPECT_EQ(t.ageClass, 0);
			ASSERT_LT(t.variant, 6);
			++seen[t.variant];
		}
		for (const int n : seen) {
			EXPECT_GT(n, 0);
		}
		def.variantCount = 1;
		EXPECT_EQ(computeTreeTraits(def, {3.0F, 4.0F}, kSeed), TreeTraits{});
	}

	// The loader reads <ageClasses> and the placement scale range, and refuses a block that
	// breaks the schema, leaving the def as it would be without it.
	TEST(TreeTraits, LoaderReadsAgeClassesAndScale) {
		const std::filesystem::path xml = std::filesystem::temp_directory_path() / "tree_traits_loader_test.xml";
		std::ofstream(xml) << R"(<AssetDefinitions>
  <AssetDef><defName>Aged</defName>
    <placement><scale min="0.93" max="1.07"/></placement>
    <ageClasses>
      <ageClass name="sapling" weight="0.25" variants="4" yieldScale="0.25" workScale="0.35"/>
      <ageClass name="mature" weight="0.75" variants="8"/>
    </ageClasses>
  </AssetDef>
  <AssetDef><defName>Broken</defName>
    <placement><scale min="2" max="1"/></placement>
    <ageClasses><ageClass name="only" weight="0.5" variants="3"/></ageClasses>
  </AssetDef>
</AssetDefinitions>)";
		AssetRegistry& registry = AssetRegistry::Get();
		registry.clear();
		ASSERT_TRUE(registry.loadDefinitions(xml.string()));
		std::filesystem::remove(xml);

		const AssetDefinition* aged = registry.getDefinition("Aged");
		ASSERT_NE(aged, nullptr);
		ASSERT_EQ(aged->ageClasses.size(), 2U);
		EXPECT_EQ(aged->ageClasses[0].name, "sapling");
		EXPECT_FLOAT_EQ(aged->ageClasses[0].weight, 0.25F);
		EXPECT_EQ(aged->ageClasses[0].variants, 4);
		EXPECT_FLOAT_EQ(aged->ageClasses[0].yieldScale, 0.25F);
		EXPECT_FLOAT_EQ(aged->ageClasses[0].workScale, 0.35F);
		EXPECT_FLOAT_EQ(aged->ageClasses[1].yieldScale, 1.0F);
		EXPECT_FLOAT_EQ(aged->placement.scale.min, 0.93F);
		EXPECT_FLOAT_EQ(aged->placement.scale.max, 1.07F);

		const AssetDefinition* broken = registry.getDefinition("Broken");
		ASSERT_NE(broken, nullptr);
		EXPECT_TRUE(broken->ageClasses.empty());
		EXPECT_FLOAT_EQ(broken->placement.scale.min, 0.8F);
		EXPECT_FLOAT_EQ(broken->placement.scale.max, 1.2F);
		registry.clear();
	}

} // namespace assets
