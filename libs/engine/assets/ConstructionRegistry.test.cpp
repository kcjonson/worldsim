// Tests for ConstructionRegistry, its ConfigValidator extension,
// and the mm-quantization helpers.

#include "assets/ConfigValidator.h"
#include "assets/ConstructionRegistry.h"

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
    std::filesystem::path p = __FILE__;            // absolute path to this source file
    return p.parent_path()  // libs/engine/assets
            .parent_path()  // libs/engine
            .parent_path()  // libs
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
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("construction_" + token + "_" + std::to_string(counter++) + suffix);
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
    ASSERT_TRUE(ConstructionRegistry::Get().load(folder))
        << "Failed to load from: " << folder;

    EXPECT_TRUE(ConstructionRegistry::Get().materialsLoaded());
    EXPECT_TRUE(ConstructionRegistry::Get().constraintsLoaded());
    EXPECT_TRUE(ConstructionRegistry::Get().snappingLoaded());
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
    EXPECT_EQ(c.minVertexSpacingMm,
              static_cast<int64_t>(std::llround(c.minVertexSpacingMeters * 1000.0)));
    EXPECT_EQ(c.segmentClearanceMm,
              static_cast<int64_t>(std::llround(c.segmentClearanceMeters * 1000.0)));
    EXPECT_EQ(c.openingMarginMm,
              static_cast<int64_t>(std::llround(c.openingMarginMeters * 1000.0)));

    const auto& s = ConstructionRegistry::Get().snapping();
    EXPECT_EQ(s.vertexSnapRadiusMm,
              static_cast<int64_t>(std::llround(s.vertexSnapRadiusMeters * 1000.0)));
    EXPECT_EQ(s.edgeSnapRadiusMm,
              static_cast<int64_t>(std::llround(s.edgeSnapRadiusMeters * 1000.0)));
    EXPECT_EQ(s.originCloseRadiusMm,
              static_cast<int64_t>(std::llround(s.originCloseRadiusMeters * 1000.0)));
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
    ASSERT_TRUE(ConstructionRegistry::Get().load(constructionConfigFolder().string()) ||
                (ConstructionRegistry::Get().constraintsLoaded() &&
                 ConstructionRegistry::Get().snappingLoaded()));
    // Force reload constraints/snapping only (materials already loaded above)
    std::string folder = constructionConfigFolder().string();
    ConstructionRegistry::Get().loadConstraints(
        (std::filesystem::path(folder) / "constraints.xml").string());
    ConstructionRegistry::Get().loadSnapping(
        (std::filesystem::path(folder) / "snapping.xml").string());

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
    ConstructionRegistry::Get().loadConstraints(
        (std::filesystem::path(folder) / "constraints.xml").string());
    ConstructionRegistry::Get().loadSnapping(
        (std::filesystem::path(folder) / "snapping.xml").string());

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
    ConstructionRegistry::Get().loadMaterials(
        (std::filesystem::path(folder) / "materials.xml").string());
    ConstructionRegistry::Get().loadSnapping(
        (std::filesystem::path(folder) / "snapping.xml").string());

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
    ConstructionRegistry::Get().loadMaterials(
        (std::filesystem::path(folder) / "materials.xml").string());
    ConstructionRegistry::Get().loadSnapping(
        (std::filesystem::path(folder) / "snapping.xml").string());

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
    ConstructionRegistry::Get().loadMaterials(
        (std::filesystem::path(folder) / "materials.xml").string());
    ConstructionRegistry::Get().loadConstraints(
        (std::filesystem::path(folder) / "constraints.xml").string());

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
