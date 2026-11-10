#include "assets/TreeTraits.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/AssetRegistry.test.h"
#include "assets/VariantSeed.h"
#include "assets/placement/GroveField.h"

#include <vector/Types.h>

#include <gtest/gtest.h>

#include <algorithm>
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

	namespace {

		// A tree whose generator draws one square, sized by math.random (so every seed draws a
		// different one) and moved by the globals: x by variantIndex, y by ageClass. Its mature
		// variant 2 draws 22,000 triangles instead, over a template's 65,535 vertices.
		void writeTestTree(const TempAssetFolder& folder) {
			folder.write("TestTree/TestTree.xml", R"(<?xml version="1.0"?><AssetDefinitions><AssetDef>
  <defName>Flora_TestTree</defName>
  <assetType>procedural</assetType>
  <generator><scriptPath>gen.lua</scriptPath></generator>
  <ageClasses>
    <ageClass name="sapling" weight="0.25" variants="2"/>
    <ageClass name="mature" weight="0.75" variants="3"/>
  </ageClasses>
</AssetDef></AssetDefinitions>)");
			folder.write("TestTree/gen.lua", R"(
if ageClass == "mature" and variantIndex == 2 then
  for i = 1, 22000 do
    local p = Path()
    p:addVertex(i, 0) p:addVertex(i + 0.5, 0) p:addVertex(i, 0.5)
    asset:addPath(p)
  end
  return
end
local s = 0.5 + math.random()
local x = variantIndex * 10
local y = (ageClass == "mature") and 100 or 0
local p = Path()
p:addVertex(x - s, y - s) p:addVertex(x + s, y - s) p:addVertex(x + s, y + s) p:addVertex(x - s, y + s)
asset:addPath(p)
)");
		}

		float minX(const renderer::TessellatedMesh& mesh) {
			float out = mesh.vertices.front().x;
			for (const auto& v : mesh.vertices) {
				out = std::min(out, v.x);
			}
			return out;
		}

	} // namespace

	// One template per (def, age class, variant): the same key is the same mesh, other keys
	// are other meshes, and the generator saw the variant and age class it was built for.
	TEST(TreeTraits, TemplatesAreOnePerAgeClassAndVariant) {
		const TempAssetFolder folder;
		writeTestTree(folder);
		AssetRegistry& registry = AssetRegistry::Get();
		registry.clear();
		registry.loadDefinitionsFromFolder(folder.root.string());

		const TreeTraits sapling0{.ageClass = 0, .variant = 0};
		const TreeTraits mature0{.ageClass = 1, .variant = 0};
		const TreeTraits mature1{.ageClass = 1, .variant = 1};
		const uint32_t	 id = registry.templateId("Flora_TestTree", mature1);
		ASSERT_NE(id, 0U);
		EXPECT_EQ(registry.templateId("Flora_TestTree", mature1), id);
		EXPECT_EQ(registry.getTemplate("Flora_TestTree", mature1), registry.getTemplate(id));

		const renderer::TessellatedMesh* a = registry.getTemplate("Flora_TestTree", sapling0);
		const renderer::TessellatedMesh* b = registry.getTemplate("Flora_TestTree", mature0);
		const renderer::TessellatedMesh* c = registry.getTemplate(id);
		ASSERT_NE(a, nullptr);
		ASSERT_NE(b, nullptr);
		ASSERT_NE(c, nullptr);
		EXPECT_NE(a, b);
		EXPECT_NE(b, c);
		EXPECT_NE(registry.templateId("Flora_TestTree", sapling0), registry.templateId("Flora_TestTree", mature0));
		EXPECT_LT(a->vertices.front().y, 50.0F) << "a sapling sees ageClass 'sapling'";
		EXPECT_GT(b->vertices.front().y, 50.0F) << "a mature tree sees ageClass 'mature'";
		EXPECT_GT(minX(*c), 5.0F) << "variant 1 sees variantIndex 1";
		EXPECT_NE(minX(*b), minX(*c) - 10.0F) << "two variants of one class draw squares from different seeds";

		EXPECT_EQ(registry.templateId("Flora_TestTree", {.ageClass = 1, .variant = 3}), 0U) << "past the class's variants";
		EXPECT_EQ(registry.templateId("Flora_TestTree", {.ageClass = 2, .variant = 0}), 0U) << "past the def's age classes";
		registry.clear();
	}

	// The generation seed is a pure function of the key, the ground tiles' hash of the def and
	// age class at the variant, and 0 for a def with one variant.
	TEST(TreeTraits, GenerationSeedIsStableForAKey) {
		const AssetDefinition def = agedTree();
		const TreeTraits	  key{.ageClass = 1, .variant = 5};
		EXPECT_EQ(AssetRegistry::generationSeed(def, key), AssetRegistry::generationSeed(def, key));
		EXPECT_EQ(AssetRegistry::generationSeed(def, key), variantSeed("Flora_TestAged:mature", 5));
		EXPECT_NE(AssetRegistry::generationSeed(def, key), AssetRegistry::generationSeed(def, {.ageClass = 1, .variant = 4}));
		EXPECT_NE(AssetRegistry::generationSeed(def, {.ageClass = 0, .variant = 0}), AssetRegistry::generationSeed(def, {.ageClass = 1, .variant = 0}));

		AssetDefinition plain;
		plain.defName = "Flora_TestPlain";
		EXPECT_EQ(AssetRegistry::generationSeed(plain, {}), 0U);
		plain.variantCount = 6;
		EXPECT_EQ(AssetRegistry::generationSeed(plain, {.ageClass = 0, .variant = 2}), variantSeed("Flora_TestPlain", 2));
	}

	// A variant over a template's 65,535 vertices fails the load, naming the def, age class,
	// and variant; it never loads as a tree with nothing to draw.
	TEST(TreeTraits, AVariantOverTheVertexLimitIsALoadErrorNamingIt) {
		const TempAssetFolder folder;
		writeTestTree(folder);
		AssetRegistry& registry = AssetRegistry::Get();
		registry.clear();
		registry.loadDefinitionsFromFolder(folder.root.string());

		const ValidationReport& report = registry.getValidationReport();
		const auto				named = std::find_if(report.issues.begin(), report.issues.end(), [](const ValidationIssue& issue) {
			 return issue.severity == Severity::Error && issue.defName == "Flora_TestTree" &&
					issue.message.find("age class 'mature' variant 2") != std::string::npos;
		 });
		ASSERT_NE(named, report.issues.end());
		EXPECT_NE(named->message.find("66000 vertices"), std::string::npos) << named->message;
		EXPECT_EQ(registry.getTemplate("Flora_TestTree", {.ageClass = 1, .variant = 2}), nullptr);
		registry.clear();
	}

} // namespace assets
