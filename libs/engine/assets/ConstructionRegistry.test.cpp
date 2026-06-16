// Tests for ConstructionRegistry, its ConfigValidator extension,
// and the mm-quantization helpers.

#include "assets/ConstructionRegistry.h"
#include "assets/ConfigValidator.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>

using namespace engine::assets;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

	// Derive the project root from __FILE__ at compile time.
	// This file lives at <root>/libs/engine/assets/ConstructionRegistry.test.cpp.
	// Walk up three directories to reach the project root.
	std::filesystem::path projectRoot() {
		std::filesystem::path p = __FILE__; // absolute path to this source file
		return p
			.parent_path()	// libs/engine/assets
			.parent_path()	// libs/engine
			.parent_path()	// libs
			.parent_path(); // project root
	}

	std::filesystem::path constructionConfigFolder() {
		return projectRoot() / "assets" / "config" / "construction";
	}

} // namespace

// ---------------------------------------------------------------------------
// Test fixture
// ---------------------------------------------------------------------------

class ConstructionRegistryTest : public ::testing::Test {
  protected:
	void SetUp() override {
		ConstructionRegistry::Get().clear();
		ConfigValidator::clearErrors();
	}

	void TearDown() override {
		ConstructionRegistry::Get().clear();
		ConfigValidator::clearErrors();
		for (const auto& f : tempFiles) {
			std::remove(f.c_str());
		}
		tempFiles.clear();
	}

	std::string writeTempFile(const std::string& content, const std::string& suffix) {
		static const std::string token = [] {
			std::random_device rd;
			return std::to_string(rd()) + "_" + std::to_string(rd());
		}();
		static std::atomic<int> counter{0};
		std::filesystem::path	path =
			std::filesystem::temp_directory_path() / ("construction_" + token + "_" + std::to_string(counter++) + suffix);
		std::ofstream f(path);
		f << content;
		f.close();
		tempFiles.push_back(path.string());
		return path.string();
	}

	std::vector<std::string> tempFiles;
};

// ---------------------------------------------------------------------------
// Real-file load tests
// ---------------------------------------------------------------------------

TEST_F(ConstructionRegistryTest, Load_RealFiles_Succeeds) {
	std::string folder = constructionConfigFolder().string();
	ASSERT_TRUE(ConstructionRegistry::Get().load(folder)) << "Failed to load from: " << folder;

	EXPECT_TRUE(ConstructionRegistry::Get().materialsLoaded());
	EXPECT_TRUE(ConstructionRegistry::Get().constraintsLoaded());
	EXPECT_TRUE(ConstructionRegistry::Get().snappingLoaded());
	EXPECT_TRUE(ConstructionRegistry::Get().renderingLoaded());
}

// ---------------------------------------------------------------------------
// Rendering style
// ---------------------------------------------------------------------------

TEST_F(ConstructionRegistryTest, Rendering_RealFileValues) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));
	ASSERT_TRUE(ConstructionRegistry::Get().renderingLoaded());

	const auto& r = ConstructionRegistry::Get().rendering();
	EXPECT_FLOAT_EQ(r.foundation.progressAlphaMin, 0.15F);
	EXPECT_FLOAT_EQ(r.foundation.progressAlphaMax, 0.85F);
	EXPECT_FLOAT_EQ(r.foundation.outlineWidthBuilt, 2.0F);
	EXPECT_FLOAT_EQ(r.wall.junctionAlphaBuilt, 0.8F);
	EXPECT_FLOAT_EQ(r.opening.jambWidth, 0.14F);
	EXPECT_FLOAT_EQ(r.opening.mullionSpacingMeters, 0.7F);
	EXPECT_FLOAT_EQ(r.preview.vertexRadiusPx, 4.0F);

	// Glass color parsed from "#80B8E699" (0x80/255 ~= 0.502, 0x99/255 ~= 0.6).
	EXPECT_NEAR(r.opening.glassColor.r, 0.50F, 0.01F);
	EXPECT_NEAR(r.opening.glassColor.a, 0.60F, 0.01F);
}

TEST_F(ConstructionRegistryTest, Rendering_MissingFile_KeepsDefaults) {
	// A missing/unreadable file is tolerated: the built-in defaults are a complete,
	// valid style, so the renderer always has something to draw with.
	EXPECT_TRUE(ConstructionRegistry::Get().loadRendering("/no/such/path/rendering.xml"));
	EXPECT_TRUE(ConstructionRegistry::Get().renderingLoaded());

	const auto& r = ConstructionRegistry::Get().rendering();
	EXPECT_FLOAT_EQ(r.foundation.progressAlphaMax, 0.85F);
	EXPECT_FLOAT_EQ(r.opening.jambWidth, 0.14F);
}

TEST_F(ConstructionRegistryTest, Rendering_ParsesOverridesKeepsOtherDefaults) {
	const std::string xml =
		"<?xml version=\"1.0\"?>\n"
		"<ConstructionRendering>\n"
		"  <Foundation>\n"
		"    <outlineColor>#FF0000FF</outlineColor>\n"
		"    <outlineWidthBuilt>3.5</outlineWidthBuilt>\n"
		"  </Foundation>\n"
		"  <Opening>\n"
		"    <jambWidth>0.20</jambWidth>\n"
		"  </Opening>\n"
		"</ConstructionRendering>\n";
	const std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadRendering(path));

	const auto& r = ConstructionRegistry::Get().rendering();
	// Overridden fields take the file's values.
	EXPECT_FLOAT_EQ(r.foundation.outlineWidthBuilt, 3.5F);
	EXPECT_NEAR(r.foundation.outlineColor.r, 1.0F, 0.01F);
	EXPECT_NEAR(r.foundation.outlineColor.g, 0.0F, 0.01F);
	EXPECT_FLOAT_EQ(r.opening.jambWidth, 0.20F);
	// Unspecified fields keep their built-in defaults.
	EXPECT_FLOAT_EQ(r.foundation.progressAlphaMax, 0.85F);
	EXPECT_FLOAT_EQ(r.wall.junctionAlphaBuilt, 0.8F);
	EXPECT_FLOAT_EQ(r.preview.vertexRadiusPx, 4.0F);
}

TEST_F(ConstructionRegistryTest, Rendering_MalformedRoot_Fails) {
	const std::string path = writeTempFile("<NotTheRoot/>\n", ".xml");
	EXPECT_FALSE(ConstructionRegistry::Get().loadRendering(path));
}

TEST_F(ConstructionRegistryTest, Materials_WoodPresentStoneAbsent) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* wood = ConstructionRegistry::Get().getMaterial("Wood");
	ASSERT_NE(wood, nullptr);
	EXPECT_EQ(wood->name, "Wood");

	// Stone is omitted this slice: no Stone item asset / source exists yet, so a
	// Stone foundation would never finish. Re-add when that economy lands.
	EXPECT_EQ(ConstructionRegistry::Get().getMaterial("Stone"), nullptr);
}

TEST_F(ConstructionRegistryTest, Materials_SaneValues) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* wood = ConstructionRegistry::Get().getMaterial("Wood");
	ASSERT_NE(wood, nullptr);
	EXPECT_GT(wood->costRatePerSquareMeter, 0.0F);
	EXPECT_GT(wood->workRatePerSquareMeter, 0.0F);
	EXPECT_GT(wood->hp, 0.0F);
	EXPECT_GE(wood->flammability, 0.0F);
	EXPECT_LE(wood->flammability, 1.0F);
	EXPECT_GT(wood->speedModifier, 0.0F);
	EXPECT_FALSE(wood->pattern.emitter.empty());
	EXPECT_FALSE(wood->pattern.palette.empty());
}

TEST_F(ConstructionRegistryTest, Constraints_Parsed) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto& c = ConstructionRegistry::Get().constraints();
	EXPECT_FLOAT_EQ(c.pathingClearanceMeters, 0.7F);
	EXPECT_GT(c.minCornerAngleDegrees, 0.0F);
	EXPECT_GT(c.minVertexSpacingMeters, 0.0F);
	EXPECT_GT(c.segmentClearanceMeters, 0.0F);
	EXPECT_GT(c.minAreaSquareMeters, 0.0F);
	EXPECT_GT(c.maxAreaSquareMeters, c.minAreaSquareMeters);
	EXPECT_GE(c.maxPoints, 3);
	EXPECT_GT(c.openingMarginMeters, 0.0F);
	EXPECT_GE(c.refundPercent, 0.0F);
	EXPECT_LE(c.refundPercent, 100.0F);
	EXPECT_GE(c.builderCapBase, 1);
	EXPECT_GT(c.builderCapPerSquareMeter, 0.0F);
}

TEST_F(ConstructionRegistryTest, Snapping_Parsed) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto& s = ConstructionRegistry::Get().snapping();
	EXPECT_GT(s.angleIncrementDegrees, 0.0F);
	EXPECT_GT(s.vertexSnapRadiusMeters, 0.0F);
	EXPECT_GT(s.edgeSnapRadiusMeters, 0.0F);
	EXPECT_GT(s.smartGuideRangeMeters, 0.0F);
	EXPECT_GT(s.originCloseRadiusMeters, 0.0F);
}

TEST_F(ConstructionRegistryTest, MmConversions_Correct) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto& c = ConstructionRegistry::Get().constraints();
	// The spec's root constant is 0.7 m → 700 mm
	EXPECT_EQ(c.pathingClearanceMm, 700);

	// Verify the general rounding rule for the other quantized values
	EXPECT_EQ(c.minVertexSpacingMm, static_cast<int64_t>(std::llround(c.minVertexSpacingMeters * 1000.0)));
	EXPECT_EQ(c.segmentClearanceMm, static_cast<int64_t>(std::llround(c.segmentClearanceMeters * 1000.0)));
	EXPECT_EQ(c.openingMarginMm, static_cast<int64_t>(std::llround(c.openingMarginMeters * 1000.0)));

	const auto& s = ConstructionRegistry::Get().snapping();
	EXPECT_EQ(s.vertexSnapRadiusMm, static_cast<int64_t>(std::llround(s.vertexSnapRadiusMeters * 1000.0)));
	EXPECT_EQ(s.edgeSnapRadiusMm, static_cast<int64_t>(std::llround(s.edgeSnapRadiusMeters * 1000.0)));
	EXPECT_EQ(s.originCloseRadiusMm, static_cast<int64_t>(std::llround(s.originCloseRadiusMeters * 1000.0)));
}

TEST_F(ConstructionRegistryTest, Validation_PassesForRealFiles) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));
	EXPECT_TRUE(ConfigValidator::validateConstruction());
	EXPECT_EQ(ConfigValidator::getErrorCount(), 0);
}

// ---------------------------------------------------------------------------
// Malformed XML rejection tests
// ---------------------------------------------------------------------------

TEST_F(ConstructionRegistryTest, Validation_RejectsMaterialWithEmptyPalette) {
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionMaterials>
  <Foundation>
    <Material name="BadWood">
      <costRatePerSquareMeter>2.0</costRatePerSquareMeter>
      <workRatePerSquareMeter>12.0</workRatePerSquareMeter>
      <hp>40.0</hp>
      <flammability>0.8</flammability>
      <beauty>1.0</beauty>
      <speedModifier>1.15</speedModifier>
      <pattern>
        <emitter>planks</emitter>
        <seed>999</seed>
        <!-- palette intentionally omitted -->
      </pattern>
    </Material>
  </Foundation>
</ConstructionMaterials>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadMaterials(path));

	// Load valid constraints and snapping so they don't interfere
	ASSERT_TRUE(
		ConstructionRegistry::Get().load(constructionConfigFolder().string()) ||
		(ConstructionRegistry::Get().constraintsLoaded() && ConstructionRegistry::Get().snappingLoaded())
	);
	// Force reload constraints/snapping only (materials already loaded above)
	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadConstraints((std::filesystem::path(folder) / "constraints.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(ConstructionRegistryTest, Validation_RejectsZeroWorkRate) {
	// workRatePerSquareMeter omitted -> parses as 0 -> workTotal 0 -> a free
	// instant building. The validator must reject this, not let it through.
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionMaterials>
  <Foundation>
    <Material name="FreeWood">
      <costRatePerSquareMeter>2.0</costRatePerSquareMeter>
      <!-- workRatePerSquareMeter intentionally omitted -->
      <hp>40.0</hp>
      <flammability>0.8</flammability>
      <beauty>1.0</beauty>
      <speedModifier>1.15</speedModifier>
      <pattern>
        <emitter>planks</emitter>
        <seed>999</seed>
        <palette>
          <color>#C8915AFF</color>
        </palette>
      </pattern>
    </Material>
  </Foundation>
</ConstructionMaterials>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadMaterials(path));

	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadConstraints((std::filesystem::path(folder) / "constraints.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(ConstructionRegistryTest, Validation_RejectsInconsistentClearance) {
	// pathingClearance > segmentClearance is invalid
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionConstraints>
  <pathingClearanceMeters>1.5</pathingClearanceMeters>
  <minCornerAngleDegrees>30.0</minCornerAngleDegrees>
  <minVertexSpacingMeters>0.5</minVertexSpacingMeters>
  <segmentClearanceMeters>0.5</segmentClearanceMeters>
  <minAreaSquareMeters>4.0</minAreaSquareMeters>
  <maxAreaSquareMeters>2500.0</maxAreaSquareMeters>
  <maxPoints>32</maxPoints>
  <openingMarginMeters>0.3</openingMarginMeters>
  <refundPercent>50</refundPercent>
  <builderCapBase>1</builderCapBase>
  <builderCapPerSquareMeter>0.1</builderCapPerSquareMeter>
</ConstructionConstraints>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadConstraints(path));

	// Load valid materials and snapping
	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadMaterials((std::filesystem::path(folder) / "materials.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(ConstructionRegistryTest, Validation_RejectsMaxPointsTooLow) {
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionConstraints>
  <pathingClearanceMeters>0.7</pathingClearanceMeters>
  <minCornerAngleDegrees>30.0</minCornerAngleDegrees>
  <minVertexSpacingMeters>0.5</minVertexSpacingMeters>
  <segmentClearanceMeters>1.0</segmentClearanceMeters>
  <minAreaSquareMeters>4.0</minAreaSquareMeters>
  <maxAreaSquareMeters>2500.0</maxAreaSquareMeters>
  <maxPoints>2</maxPoints>
  <openingMarginMeters>0.3</openingMarginMeters>
  <refundPercent>50</refundPercent>
  <builderCapBase>1</builderCapBase>
  <builderCapPerSquareMeter>0.1</builderCapPerSquareMeter>
</ConstructionConstraints>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadConstraints(path));

	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadMaterials((std::filesystem::path(folder) / "materials.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(ConstructionRegistryTest, Validation_RejectsNegativeSnapRadius) {
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionSnapping>
  <angleIncrementDegrees>15.0</angleIncrementDegrees>
  <vertexSnapRadiusMeters>-0.1</vertexSnapRadiusMeters>
  <edgeSnapRadiusMeters>0.3</edgeSnapRadiusMeters>
  <smartGuideRangeMeters>8.0</smartGuideRangeMeters>
  <originCloseRadiusMeters>0.5</originCloseRadiusMeters>
</ConstructionSnapping>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadSnapping(path));

	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadMaterials((std::filesystem::path(folder) / "materials.xml").string());
	ConstructionRegistry::Get().loadConstraints((std::filesystem::path(folder) / "constraints.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(ConstructionRegistryTest, LoadFromMissingFolder_ReturnsFalse) {
	EXPECT_FALSE(ConstructionRegistry::Get().load("/nonexistent/path/to/construction"));
}

TEST_F(ConstructionRegistryTest, Clear_ResetsState) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));
	EXPECT_TRUE(ConstructionRegistry::Get().materialsLoaded());

	ConstructionRegistry::Get().clear();
	EXPECT_FALSE(ConstructionRegistry::Get().materialsLoaded());
	EXPECT_FALSE(ConstructionRegistry::Get().constraintsLoaded());
	EXPECT_FALSE(ConstructionRegistry::Get().snappingLoaded());
	EXPECT_EQ(ConstructionRegistry::Get().getMaterial("Wood"), nullptr);
}

// ---------------------------------------------------------------------------
// Wall thickness preset tests
// ---------------------------------------------------------------------------

TEST_F(ConstructionRegistryTest, WallPresets_WoodHasThreePresets) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* wood = ConstructionRegistry::Get().getMaterial("Wood");
	ASSERT_NE(wood, nullptr);
	EXPECT_EQ(wood->wallThicknesses.size(), 3u);
}

TEST_F(ConstructionRegistryTest, WallPresets_StandardMmConversion) {
	// Standard preset is 0.20 m → 200 mm → 100 half
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* preset = ConstructionRegistry::Get().getThicknessPreset("Wood", "Standard");
	ASSERT_NE(preset, nullptr);
	EXPECT_FLOAT_EQ(preset->thicknessMeters, 0.20F);
	EXPECT_EQ(preset->thicknessMm, 200);
	EXPECT_EQ(preset->halfThicknessMm, 100);
}

TEST_F(ConstructionRegistryTest, WallPresets_LightMmConversion) {
	// Light preset is 0.15 m → 150 mm → 75 half
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* preset = ConstructionRegistry::Get().getThicknessPreset("Wood", "Light");
	ASSERT_NE(preset, nullptr);
	EXPECT_FLOAT_EQ(preset->thicknessMeters, 0.15F);
	EXPECT_EQ(preset->thicknessMm, 150);
	EXPECT_EQ(preset->halfThicknessMm, 75);
}

TEST_F(ConstructionRegistryTest, WallPresets_HeavyMmConversion) {
	// Heavy preset is 0.30 m → 300 mm → 150 half
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* preset = ConstructionRegistry::Get().getThicknessPreset("Wood", "Heavy");
	ASSERT_NE(preset, nullptr);
	EXPECT_FLOAT_EQ(preset->thicknessMeters, 0.30F);
	EXPECT_EQ(preset->thicknessMm, 300);
	EXPECT_EQ(preset->halfThicknessMm, 150);
}

TEST_F(ConstructionRegistryTest, WallPresets_MultipliersPositive) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* wood = ConstructionRegistry::Get().getMaterial("Wood");
	ASSERT_NE(wood, nullptr);
	for (const auto& preset : wood->wallThicknesses) {
		EXPECT_GT(preset.costMultiplier, 0.0F) << "preset: " << preset.name;
		EXPECT_GT(preset.workMultiplier, 0.0F) << "preset: " << preset.name;
		EXPECT_GT(preset.hpMultiplier, 0.0F) << "preset: " << preset.name;
		EXPECT_GE(preset.insulation, 0.0F) << "preset: " << preset.name;
	}
}

TEST_F(ConstructionRegistryTest, WallPresets_GetThicknessPreset_Hit) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* p = ConstructionRegistry::Get().getThicknessPreset("Wood", "Standard");
	EXPECT_NE(p, nullptr);
}

TEST_F(ConstructionRegistryTest, WallPresets_GetThicknessPreset_MissMaterial) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	EXPECT_EQ(ConstructionRegistry::Get().getThicknessPreset("Stone", "Standard"), nullptr);
}

TEST_F(ConstructionRegistryTest, WallPresets_GetThicknessPreset_MissPresetName) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	EXPECT_EQ(ConstructionRegistry::Get().getThicknessPreset("Wood", "NonExistent"), nullptr);
}

TEST_F(ConstructionRegistryTest, WallPresets_GetWallMaterial_Hit) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	EXPECT_NE(ConstructionRegistry::Get().getWallMaterial("Wood"), nullptr);
}

TEST_F(ConstructionRegistryTest, WallPresets_GetWallMaterial_MissNoPresets) {
	// Stone has no wall presets, so getWallMaterial should return nullptr.
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	EXPECT_EQ(ConstructionRegistry::Get().getWallMaterial("Stone"), nullptr);
}

// ---------------------------------------------------------------------------
// Wall constraint tests
// ---------------------------------------------------------------------------

TEST_F(ConstructionRegistryTest, WallConstraints_Parsed) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto& c = ConstructionRegistry::Get().constraints();
	EXPECT_FLOAT_EQ(c.minSegmentLengthMeters, 0.5F);
	EXPECT_FLOAT_EQ(c.minWallJunctionAngleDegrees, 30.0F);
	EXPECT_FLOAT_EQ(c.minParallelClearanceMeters, 0.8F);
}

TEST_F(ConstructionRegistryTest, WallConstraints_MmMirrors) {
	// 0.5 m → 500 mm, 0.8 m → 800 mm
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto& c = ConstructionRegistry::Get().constraints();
	EXPECT_EQ(c.minSegmentLengthMm, 500);
	EXPECT_EQ(c.minParallelClearanceMm, 800);
}

TEST_F(ConstructionRegistryTest, WallConstraints_ParallelClearanceGePathingClearance) {
	// minParallelClearance must be >= pathingClearance (0.7) or else colonists can't pass.
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto& c = ConstructionRegistry::Get().constraints();
	EXPECT_GE(c.minParallelClearanceMeters, c.pathingClearanceMeters);
}

// ---------------------------------------------------------------------------
// Negative-path: malformed wall preset rejected by validateConstruction
// ---------------------------------------------------------------------------

TEST_F(ConstructionRegistryTest, Validation_RejectsWallPresetWithZeroThickness) {
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionMaterials>
  <Foundation>
    <Material name="Wood">
      <costRatePerSquareMeter>2.0</costRatePerSquareMeter>
      <workRatePerSquareMeter>12.0</workRatePerSquareMeter>
      <hp>40.0</hp>
      <flammability>0.8</flammability>
      <beauty>1.0</beauty>
      <speedModifier>1.15</speedModifier>
      <pattern>
        <emitter>planks</emitter>
        <seed>1001</seed>
        <palette>
          <color>#C8915AFF</color>
        </palette>
      </pattern>
    </Material>
  </Foundation>
  <Wall>
    <Material name="Wood">
      <Preset name="BadPreset">
        <!-- thicknessMeters intentionally zero → invalid -->
        <thicknessMeters>0.0</thicknessMeters>
        <costMultiplier>1.0</costMultiplier>
        <workMultiplier>1.0</workMultiplier>
        <hpMultiplier>1.0</hpMultiplier>
        <insulation>0.4</insulation>
      </Preset>
    </Material>
  </Wall>
</ConstructionMaterials>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadMaterials(path));

	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadConstraints((std::filesystem::path(folder) / "constraints.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(ConstructionRegistryTest, Validation_RejectsWallPresetWithNegativeMultiplier) {
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionMaterials>
  <Foundation>
    <Material name="Wood">
      <costRatePerSquareMeter>2.0</costRatePerSquareMeter>
      <workRatePerSquareMeter>12.0</workRatePerSquareMeter>
      <hp>40.0</hp>
      <flammability>0.8</flammability>
      <beauty>1.0</beauty>
      <speedModifier>1.15</speedModifier>
      <pattern>
        <emitter>planks</emitter>
        <seed>1001</seed>
        <palette>
          <color>#C8915AFF</color>
        </palette>
      </pattern>
    </Material>
  </Foundation>
  <Wall>
    <Material name="Wood">
      <Preset name="BadMultiplier">
        <thicknessMeters>0.2</thicknessMeters>
        <!-- negative workMultiplier → invalid -->
        <costMultiplier>1.0</costMultiplier>
        <workMultiplier>-1.0</workMultiplier>
        <hpMultiplier>1.0</hpMultiplier>
        <insulation>0.4</insulation>
      </Preset>
    </Material>
  </Wall>
</ConstructionMaterials>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadMaterials(path));

	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadConstraints((std::filesystem::path(folder) / "constraints.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

// ---------------------------------------------------------------------------
// Opening type tests
// ---------------------------------------------------------------------------

TEST_F(ConstructionRegistryTest, Openings_DoorAndWindowLoad) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto* door = ConstructionRegistry::Get().getOpeningType("Door");
	ASSERT_NE(door, nullptr);
	EXPECT_EQ(door->name, "Door");
	EXPECT_EQ(door->material, "Wood");
	EXPECT_FLOAT_EQ(door->widthMeters, 0.9F);
	EXPECT_EQ(door->widthMm, 900);
	EXPECT_TRUE(door->pathable);
	EXPECT_GT(door->costItems, 0.0F);
	EXPECT_GT(door->workUnits, 0.0F);

	const auto* window = ConstructionRegistry::Get().getOpeningType("Window");
	ASSERT_NE(window, nullptr);
	EXPECT_FLOAT_EQ(window->widthMeters, 0.6F);
	EXPECT_EQ(window->widthMm, 600);
	EXPECT_FALSE(window->pathable);
}

TEST_F(ConstructionRegistryTest, Openings_ListHasTwoTypesInOrder) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));

	const auto& types = ConstructionRegistry::Get().openingTypes();
	ASSERT_EQ(types.size(), 2u);
	// Load order is stable (matches XML order): Door first, Window second.
	EXPECT_EQ(types[0].name, "Door");
	EXPECT_EQ(types[1].name, "Window");
}

TEST_F(ConstructionRegistryTest, Openings_UnknownTypeIsNull) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));
	EXPECT_EQ(ConstructionRegistry::Get().getOpeningType("Skylight"), nullptr);
}

TEST_F(ConstructionRegistryTest, Openings_ClearedOnClear) {
	ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()));
	ASSERT_FALSE(ConstructionRegistry::Get().openingTypes().empty());

	ConstructionRegistry::Get().clear();
	EXPECT_TRUE(ConstructionRegistry::Get().openingTypes().empty());
	EXPECT_EQ(ConstructionRegistry::Get().getOpeningType("Door"), nullptr);
}

TEST_F(ConstructionRegistryTest, Validation_RejectsOpeningWithMissingMaterial) {
	// An opening type referencing a material that does not exist must fail
	// validation (fail-fast on a dangling config reference).
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionMaterials>
  <Foundation>
    <Material name="Wood">
      <costRatePerSquareMeter>2.0</costRatePerSquareMeter>
      <workRatePerSquareMeter>12.0</workRatePerSquareMeter>
      <hp>40.0</hp>
      <flammability>0.8</flammability>
      <beauty>1.0</beauty>
      <speedModifier>1.15</speedModifier>
      <pattern>
        <emitter>planks</emitter>
        <seed>1001</seed>
        <palette>
          <color>#C8915AFF</color>
        </palette>
      </pattern>
    </Material>
  </Foundation>
  <Opening>
    <Type name="GhostDoor">
      <!-- references a material that was never defined -->
      <material>Adamantium</material>
      <widthMeters>0.9</widthMeters>
      <pathable>true</pathable>
      <costItems>3.0</costItems>
      <workUnits>60.0</workUnits>
    </Type>
  </Opening>
</ConstructionMaterials>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadMaterials(path));

	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadConstraints((std::filesystem::path(folder) / "constraints.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(ConstructionRegistryTest, Validation_RejectsOpeningWithZeroWidth) {
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionMaterials>
  <Foundation>
    <Material name="Wood">
      <costRatePerSquareMeter>2.0</costRatePerSquareMeter>
      <workRatePerSquareMeter>12.0</workRatePerSquareMeter>
      <hp>40.0</hp>
      <flammability>0.8</flammability>
      <beauty>1.0</beauty>
      <speedModifier>1.15</speedModifier>
      <pattern>
        <emitter>planks</emitter>
        <seed>1001</seed>
        <palette>
          <color>#C8915AFF</color>
        </palette>
      </pattern>
    </Material>
  </Foundation>
  <Opening>
    <Type name="NoWidthDoor">
      <material>Wood</material>
      <!-- width omitted -> 0 -> invalid -->
      <pathable>true</pathable>
      <costItems>3.0</costItems>
      <workUnits>60.0</workUnits>
    </Type>
  </Opening>
</ConstructionMaterials>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadMaterials(path));

	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadConstraints((std::filesystem::path(folder) / "constraints.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}

TEST_F(ConstructionRegistryTest, Validation_RejectsDuplicateWallPresetNames) {
	std::string xml = R"(<?xml version="1.0"?>
<ConstructionMaterials>
  <Foundation>
    <Material name="Wood">
      <costRatePerSquareMeter>2.0</costRatePerSquareMeter>
      <workRatePerSquareMeter>12.0</workRatePerSquareMeter>
      <hp>40.0</hp>
      <flammability>0.8</flammability>
      <beauty>1.0</beauty>
      <speedModifier>1.15</speedModifier>
      <pattern>
        <emitter>planks</emitter>
        <seed>1001</seed>
        <palette>
          <color>#C8915AFF</color>
        </palette>
      </pattern>
    </Material>
  </Foundation>
  <Wall>
    <Material name="Wood">
      <Preset name="Standard">
        <thicknessMeters>0.2</thicknessMeters>
        <costMultiplier>1.0</costMultiplier>
        <workMultiplier>1.0</workMultiplier>
        <hpMultiplier>1.0</hpMultiplier>
        <insulation>0.4</insulation>
      </Preset>
      <!-- Duplicate name: Standard appears twice → invalid -->
      <Preset name="Standard">
        <thicknessMeters>0.3</thicknessMeters>
        <costMultiplier>1.5</costMultiplier>
        <workMultiplier>1.5</workMultiplier>
        <hpMultiplier>1.4</hpMultiplier>
        <insulation>0.6</insulation>
      </Preset>
    </Material>
  </Wall>
</ConstructionMaterials>)";

	std::string path = writeTempFile(xml, ".xml");
	ASSERT_TRUE(ConstructionRegistry::Get().loadMaterials(path));

	std::string folder = constructionConfigFolder().string();
	ConstructionRegistry::Get().loadConstraints((std::filesystem::path(folder) / "constraints.xml").string());
	ConstructionRegistry::Get().loadSnapping((std::filesystem::path(folder) / "snapping.xml").string());

	EXPECT_FALSE(ConfigValidator::validateConstruction());
	EXPECT_GT(ConfigValidator::getErrorCount(), 0);
}
