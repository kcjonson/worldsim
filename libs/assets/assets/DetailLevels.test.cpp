// Mesh detail levels: the level a screen density draws, the <detailLevels> schema, the fallback
// to a ready level, and the registry building a template at each level while everything that
// isn't drawing stays on the reference level.

#include "assets/AssetRegistry.h"
#include "assets/DetailLevels.h"
#include "assets/TreeDefSchema.h"

#include <world/camera/WorldCamera.h>

#include <gtest/gtest.h>
#include <pugixml.hpp>

#include <array>
#include <filesystem>
#include <vector>

using namespace assets;

namespace {

	constexpr std::array<float, 6> kOakLevels = {2.0F, 4.0F, 8.0F, 24.0F, 40.0F, 80.0F};
	constexpr float				   kGamePixelsPerMeter = 8.0F; // GameScene's

	std::vector<float> parse(const char* xml, std::vector<TreeDefIssue>& issues) {
		pugi::xml_document doc;
		EXPECT_TRUE(doc.load_string(xml));
		return readDetailLevels(doc.child("AssetDef"), issues);
	}

	// Loads the oak from its one file, or skips when the repo's assets aren't beside the source.
	const AssetDefinition* loadOak() {
		namespace fs = std::filesystem;
		const fs::path root = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
		const fs::path oak = root / "assets" / "world" / "flora" / "OakTree" / "OakTree.xml";
		if (!fs::exists(oak)) {
			return nullptr;
		}
		AssetRegistry& reg = AssetRegistry::Get();
		reg.clearDefinitions();
		reg.setSharedScriptsPath(root / "assets" / "shared" / "scripts");
		reg.loadDefinitions(oak.string());
		return reg.getDefinition("Flora_TreeOak");
	}

} // namespace

// Every camera zoom step, at the game's 8 px/m: the highest level at or below the density, else the lowest.
TEST(DetailLevelsTest, EveryZoomStepPicksItsLevel) {
	constexpr std::array<float, 13> kExpected = {2, 4, 4, 8, 8, 8, 24, 24, 40, 40, 80, 80, 80};
	static_assert(world::kZoomLevels.size() == kExpected.size());
	for (size_t i = 0; i < world::kZoomLevels.size(); ++i) {
		const float density = kGamePixelsPerMeter * world::kZoomLevels[i];
		EXPECT_EQ(kOakLevels[detailLevelFor(kOakLevels, density)], kExpected[i]) << "zoom " << world::kZoomLevels[i];
	}
	EXPECT_EQ(detailLevelFor(kOakLevels, 1.0F), 0) << "below every level: the lowest";
	EXPECT_EQ(detailLevelFor({}, 50.0F), 0) << "no levels: the one mesh";
	EXPECT_EQ(kOakLevels[referenceDetailLevel(kOakLevels)], 24.0F);
	EXPECT_EQ(kGamePixelsPerMeter * world::kZoomLevels[world::kDefaultZoomIndex], kReferencePixelsPerMeter);
}

TEST(DetailLevelsTest, NearestReadyLevelPrefersTheFinerOfATie) {
	EXPECT_EQ(nearestReadyLevel(0b01000, 4), 3) << "zooming in: the reference until 80 arrives";
	EXPECT_EQ(nearestReadyLevel(0b01000, 0), 3) << "zooming out: the reference until 2 arrives";
	EXPECT_EQ(nearestReadyLevel(0b01010, 2), 3) << "equally close: the finer";
	EXPECT_EQ(nearestReadyLevel(0b11111, 2), 2) << "ready: itself";
	EXPECT_EQ(nearestReadyLevel(0, 2), std::nullopt);
}

TEST(DetailLevelsTest, SchemaReadsAscendingDensitiesOnAProceduralDef) {
	std::vector<TreeDefIssue> issues;
	EXPECT_EQ(
		parse("<AssetDef><assetType>procedural</assetType><detailLevels>2, 4,8 , 24,40, 80</detailLevels></AssetDef>", issues),
		std::vector<float>(kOakLevels.begin(), kOakLevels.end())
	);
	EXPECT_TRUE(issues.empty());
	EXPECT_TRUE(parse("<AssetDef><assetType>procedural</assetType></AssetDef>", issues).empty());
	EXPECT_TRUE(issues.empty()) << "no levels is one mesh, not an issue";

	for (const char* bad : {
			 "<AssetDef><assetType>procedural</assetType><detailLevels>4, 2</detailLevels></AssetDef>",
			 "<AssetDef><assetType>procedural</assetType><detailLevels>2, 2</detailLevels></AssetDef>",
			 "<AssetDef><assetType>procedural</assetType><detailLevels>0, 2</detailLevels></AssetDef>",
			 "<AssetDef><assetType>procedural</assetType><detailLevels>2, x</detailLevels></AssetDef>",
			 "<AssetDef><assetType>procedural</assetType><detailLevels></detailLevels></AssetDef>",
			 "<AssetDef><assetType>procedural</assetType><detailLevels>1,2,3,4,5,6,7,8,9</detailLevels></AssetDef>",
			 "<AssetDef><assetType>simple</assetType><detailLevels>2, 4</detailLevels></AssetDef>",
		 }) {
		issues.clear();
		EXPECT_TRUE(parse(bad, issues).empty()) << bad;
		EXPECT_EQ(issues.size(), 1U) << bad;
	}
}

// A template's id has no level in it: the same (def, age class, variant) is one id however it is
// drawn, and each level is a different mesh of it, the same every time it is built.
TEST(DetailLevelsTest, OakTemplateHasOneIdAndAStableMeshPerLevel) {
	const AssetDefinition* def = loadOak();
	if (def == nullptr) {
		GTEST_SKIP() << "assets/world not found";
	}
	AssetRegistry&	 reg = AssetRegistry::Get();
	const TreeTraits mature = AssetRegistry::matureTraits(*def);
	const uint32_t	 id = reg.templateId(def->defName, mature);
	ASSERT_NE(id, 0U);
	EXPECT_EQ(reg.templateId(def->defName, mature), id);
	EXPECT_NE(reg.templateId(def->defName, {.ageClass = mature.ageClass, .variant = 1}), id);
	ASSERT_EQ(reg.detailLevels(id).size(), kOakLevels.size());

	size_t previousTriangles = 0;
	for (uint8_t level = 0; level < kOakLevels.size(); ++level) {
		const AssetRegistry::BuiltTemplate built = reg.buildTemplateLevel(id, level);
		ASSERT_TRUE(built.mesh.has_value()) << built.error;
		EXPECT_GT(built.mesh->getTriangleCount(), previousTriangles) << "level " << int(level) << " is finer than the last";
		previousTriangles = built.mesh->getTriangleCount();
		EXPECT_EQ(reg.buildTemplateLevel(id, level).mesh->vertices, built.mesh->vertices) << "level " << int(level);
	}
	EXPECT_FALSE(reg.buildTemplateLevel(id, static_cast<uint8_t>(kOakLevels.size())).mesh.has_value());
}

// What isn't drawing comes from the reference level: the kept mesh (silhouette, reach, routing)
// is the reference level's, the collision rect is the same at every level, and building other
// levels moves neither the reach nor the silhouette.
TEST(DetailLevelsTest, OakNonDrawingDataIsTheReferenceLevels) {
	const AssetDefinition* def = loadOak();
	if (def == nullptr) {
		GTEST_SKIP() << "assets/world not found";
	}
	AssetRegistry&	 reg = AssetRegistry::Get();
	const uint32_t	 id = reg.templateId(def->defName, AssetRegistry::matureTraits(*def));
	const uint8_t	 reference = referenceDetailLevel(def->detailLevels);
	const auto*		 kept = reg.getTemplate(id);
	ASSERT_NE(kept, nullptr);
	const AssetRegistry::BuiltTemplate atReference = reg.buildTemplateLevel(id, reference);
	ASSERT_TRUE(atReference.mesh.has_value());
	EXPECT_EQ(kept->vertices, atReference.mesh->vertices);

	const float				 reach = reg.maxReachMeters();
	const AssetSilhouette*	 silhouette = reg.getSilhouette(id);
	const glm::vec2			 center = silhouette->boundsCenterMeters;
	for (uint8_t level = 0; level < def->detailLevels.size(); ++level) {
		EXPECT_EQ(reg.buildTemplateLevel(id, level).collision, atReference.collision) << "level " << int(level);
	}
	EXPECT_EQ(reg.maxReachMeters(), reach);
	EXPECT_EQ(reg.getSilhouette(id)->boundsCenterMeters, center);
	EXPECT_EQ(reg.getTemplate(id), kept);
}
