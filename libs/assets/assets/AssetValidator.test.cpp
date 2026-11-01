// Tests for AssetValidator: load-time asset definition checks.

#include "assets/AssetValidator.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace assets;

namespace {

	namespace fs = std::filesystem;

	// A throwaway asset tree under the temp dir, cleaned up on destruction.
	struct TempAssets {
		fs::path root;

		TempAssets() {
			static std::atomic<int> counter{0};
			const std::string		token = std::to_string(reinterpret_cast<uintptr_t>(&counter)) + "_" + std::to_string(counter++);
			root = fs::temp_directory_path() / ("assetval_" + token);
			fs::create_directories(root);
		}

		~TempAssets() {
			std::error_code ec;
			fs::remove_all(root, ec);
		}

		TempAssets(const TempAssets&) = delete;
		TempAssets& operator=(const TempAssets&) = delete;

		// Write <folder>/<xmlName>.xml (xmlName defaults to the folder name).
		void writeXml(const std::string& folder, const std::string& body, const std::string& xmlName = "") {
			const fs::path dir = root / folder;
			fs::create_directories(dir);
			const std::string name = xmlName.empty() ? folder : xmlName;
			std::ofstream(dir / (name + ".xml")) << body;
		}

		void writeFile(const std::string& relPath, const std::string& content) {
			const fs::path p = root / relPath;
			fs::create_directories(p.parent_path());
			std::ofstream(p) << content;
		}
	};

	std::string oneDef(const std::string& inner) {
		return "<AssetDefinitions><AssetDef>" + inner + "</AssetDef></AssetDefinitions>";
	}

	bool hasIssue(const ValidationReport& r, Severity sev, const std::string& field) {
		for (const auto& i : r.issues) {
			if (i.severity == sev && i.field == field) {
				return true;
			}
		}
		return false;
	}

	bool hasMessageContaining(const ValidationReport& r, Severity sev, const std::string& substr) {
		for (const auto& i : r.issues) {
			if (i.severity == sev && i.message.find(substr) != std::string::npos) {
				return true;
			}
		}
		return false;
	}

} // namespace

TEST(AssetValidatorTest, CleanSimpleDefHasNoErrors) {
	TempAssets t;
	t.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>stone.svg</svgPath>"));
	t.writeFile("Stone/stone.svg", "<svg/>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_EQ(r.errorCount(), 0);
	EXPECT_FALSE(r.hasErrors());
}

TEST(AssetValidatorTest, MissingSvgIsError) {
	TempAssets t;
	t.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>stone.svg</svgPath>"));

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasIssue(r, Severity::Error, "svgPath"));
}

TEST(AssetValidatorTest, MissingScriptIsError) {
	TempAssets t;
	t.writeXml("Tree", oneDef("<defName>Tree</defName><assetType>procedural</assetType><generator><scriptPath>tree.lua</scriptPath></generator>"));

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasIssue(r, Severity::Error, "scriptPath"));
}

TEST(AssetValidatorTest, DuplicateDefNameIsError) {
	TempAssets t;
	t.writeXml("StoneA", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>a.svg</svgPath>"));
	t.writeFile("StoneA/a.svg", "<svg/>");
	t.writeXml("StoneB", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>b.svg</svgPath>"));
	t.writeFile("StoneB/b.svg", "<svg/>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasIssue(r, Severity::Error, "defName"));
}

TEST(AssetValidatorTest, NameFolderMismatchIsWarning) {
	TempAssets t;
	t.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>stone.svg</svgPath>"), "Different");
	t.writeFile("Stone/stone.svg", "<svg/>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasMessageContaining(r, Severity::Warning, "does not match its folder"));
}

TEST(AssetValidatorTest, IgnoredVariationIsWarning) {
	TempAssets t;
	t.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>stone.svg</svgPath><variation><color/></variation>"));
	t.writeFile("Stone/stone.svg", "<svg/>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasIssue(r, Severity::Warning, "variation"));
}

TEST(AssetValidatorTest, VariantCountInRenderingIsWarning) {
	TempAssets t;
	t.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>stone.svg</svgPath><rendering><variantCount>4</variantCount></rendering>"));
	t.writeFile("Stone/stone.svg", "<svg/>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasIssue(r, Severity::Warning, "variantCount"));
}

TEST(AssetValidatorTest, UnknownAssetTypeIsWarning) {
	TempAssets t;
	t.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>bogus</assetType><svgPath>stone.svg</svgPath>"));
	t.writeFile("Stone/stone.svg", "<svg/>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasIssue(r, Severity::Warning, "assetType"));
}

TEST(AssetValidatorTest, OrphanSvgIsWarning) {
	TempAssets t;
	t.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>stone.svg</svgPath>"));
	t.writeFile("Stone/stone.svg", "<svg/>");
	t.writeFile("Stone/leftover.svg", "<svg/>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasMessageContaining(r, Severity::Warning, "orphan"));
}

namespace {

	// Write assets/<folder>/<folder>.xml as a ground def (defName Ground_<folder>) plus its tile SVG.
	void addGround(TempAssets& t, const std::string& folder, const std::string& extra) {
		t.writeXml(folder, oneDef("<defName>Ground_" + folder + "</defName><role>ground</role><assetType>simple</assetType><svgPath>tile.svg</svgPath>" + extra));
		t.writeFile(folder + "/tile.svg", "<svg/>");
	}

	// A tree with a ground def for every land surface (each one is required): Grass carries xtra, the rest
	// only their surface.
	ValidationReport validateGround(const std::string& extra) {
		TempAssets t;
		addGround(t, "Grass", extra);
		for (const char* other : {"Dirt", "Sand", "Rock", "Snow", "Mud", "GrassTall", "GrassShort", "GrassMeadow"}) {
			addGround(t, other, std::string("<surface>") + other + "</surface>");
		}
		return AssetValidator::validate(t.root, "");
	}

} // namespace

TEST(AssetValidatorTest, GroundDefWithDefaultsIsClean) {
	const ValidationReport r = validateGround("<surface>Grass</surface>");
	EXPECT_FALSE(hasIssue(r, Severity::Error, "surface"));
	EXPECT_EQ(r.errorCount(), 0);
	EXPECT_FALSE(hasIssue(r, Severity::Warning, "role")) << "ground is a known role";
}

TEST(AssetValidatorTest, GroundPeriodMustDivideTheChunk) {
	EXPECT_EQ(validateGround("<surface>Grass</surface><period>0.5</period>").errorCount(), 0);
	EXPECT_EQ(validateGround("<surface>Grass</surface><period>512</period><pixelsPerMeter>16</pixelsPerMeter>").errorCount(), 0);
	EXPECT_TRUE(hasIssue(validateGround("<surface>Grass</surface><period>3</period>"), Severity::Error, "period"));
	EXPECT_TRUE(hasIssue(validateGround("<surface>Grass</surface><period>1000</period>"), Severity::Error, "period"));
	EXPECT_TRUE(hasIssue(validateGround("<surface>Grass</surface><period>0</period>"), Severity::Error, "period"));
	EXPECT_TRUE(hasIssue(validateGround("<surface>Grass</surface><period>-4</period>"), Severity::Error, "period"));
}

TEST(AssetValidatorTest, GroundPeriodMustFillWholePixels) {
	// 1/32 m at 16 px/m is half a pixel, so the layer would have no whole size.
	const ValidationReport r = validateGround("<surface>Grass</surface><period>0.03125</period><pixelsPerMeter>16</pixelsPerMeter>");
	EXPECT_TRUE(hasMessageContaining(r, Severity::Error, "whole number of pixels"));
}

TEST(AssetValidatorTest, GroundPixelsPerMeterIsAPowerOfTwoInRange) {
	for (const char* good : {"16", "64", "128", "512"}) {
		EXPECT_EQ(validateGround(std::string("<surface>Grass</surface><pixelsPerMeter>") + good + "</pixelsPerMeter>").errorCount(), 0) << good;
	}
	for (const char* bad : {"8", "100", "0", "1024", "-64", "abc"}) {
		EXPECT_TRUE(hasIssue(validateGround(std::string("<surface>Grass</surface><pixelsPerMeter>") + bad + "</pixelsPerMeter>"), Severity::Error, "pixelsPerMeter"))
			<< bad;
	}
}

TEST(AssetValidatorTest, GroundVariantCountIsOneToThirtyTwo) {
	EXPECT_EQ(validateGround("<surface>Grass</surface><variantCount>1</variantCount>").errorCount(), 0);
	EXPECT_EQ(validateGround("<surface>Grass</surface><variantCount>32</variantCount>").errorCount(), 0);
	EXPECT_TRUE(hasIssue(validateGround("<surface>Grass</surface><variantCount>0</variantCount>"), Severity::Error, "variantCount"));
	EXPECT_TRUE(hasIssue(validateGround("<surface>Grass</surface><variantCount>33</variantCount>"), Severity::Error, "variantCount"));
}

TEST(AssetValidatorTest, GroundSurfaceMustBeALandSurface) {
	EXPECT_TRUE(hasIssue(validateGround(""), Severity::Error, "surface"));
	EXPECT_TRUE(hasIssue(validateGround("<surface>Lava</surface>"), Severity::Error, "surface"));
	EXPECT_TRUE(hasMessageContaining(validateGround("<surface>Water</surface>"), Severity::Error, "not a land surface"));
}

TEST(AssetValidatorTest, TwoGroundDefsForOneSurfaceIsError) {
	TempAssets t;
	addGround(t, "GrassA", "<surface>Grass</surface>");
	addGround(t, "GrassB", "<surface>Grass</surface>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	EXPECT_TRUE(hasMessageContaining(r, Severity::Error, "already paints this surface"));
}

TEST(AssetValidatorTest, LandSurfacesWithoutGroundDefsAreErrorsOnceGroundContentExists) {
	TempAssets t;
	addGround(t, "Grass", "<surface>Grass</surface>");
	addGround(t, "Dirt", "<surface>Dirt</surface>");

	const ValidationReport r = AssetValidator::validate(t.root, "");
	// Ten surfaces, minus Water, minus the two covered.
	EXPECT_EQ(r.errorCount(), 7);
	EXPECT_TRUE(hasMessageContaining(r, Severity::Error, "No ground def paints this land surface"));

	TempAssets none;
	none.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>stone.svg</svgPath>"));
	none.writeFile("Stone/stone.svg", "<svg/>");
	const ValidationReport bare = AssetValidator::validate(none.root, "");
	EXPECT_EQ(bare.errorCount() + bare.warningCount(), 0) << "a library with no ground defs isn't held to them";
}

TEST(AssetValidatorTest, GroundDefWithBiomeRulesIsError) {
	const ValidationReport r = validateGround("<surface>Grass</surface><placement><biome name=\"TemperateGrassland\"/></placement>");
	EXPECT_TRUE(hasIssue(r, Severity::Error, "placement"));
}

TEST(AssetValidatorTest, UnknownGroundRotateIsWarning) {
	EXPECT_TRUE(hasIssue(validateGround("<surface>Grass</surface><rotate>spin</rotate>"), Severity::Warning, "rotate"));
	EXPECT_FALSE(hasIssue(validateGround("<surface>Grass</surface><rotate>quarter</rotate>"), Severity::Warning, "rotate"));
	EXPECT_FALSE(hasIssue(validateGround("<surface>Grass</surface><rotate>mirror</rotate>"), Severity::Warning, "rotate"));
}

TEST(AssetValidatorTest, GroundFieldsOnOtherRolesAreWarnings) {
	TempAssets t;
	t.writeXml("Stone", oneDef("<defName>Stone</defName><assetType>simple</assetType><svgPath>stone.svg</svgPath><period>4</period>"));
	t.writeFile("Stone/stone.svg", "<svg/>");

	EXPECT_TRUE(hasIssue(AssetValidator::validate(t.root, ""), Severity::Warning, "period"));
}

// Guards the shipped library against validation errors, and prints any issues
// (warnings included) so regressions are visible in test output.
TEST(AssetValidatorTest, RealAssetLibraryHasNoErrors) {
	const fs::path root = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
	const fs::path assetsWorld = root / "assets" / "world";
	const fs::path shared = root / "assets" / "shared" / "scripts";
	if (!fs::exists(assetsWorld)) {
		GTEST_SKIP() << "assets/world not found";
	}

	const ValidationReport r = AssetValidator::validate(assetsWorld, shared);
	for (const auto& i : r.issues) {
		std::cout << (i.severity == Severity::Error ? "[ERROR] " : "[warn]  ") << (i.defName.empty() ? "-" : i.defName) << " | " << i.field
				  << " | " << i.message << " | " << i.context << "\n";
	}
	EXPECT_EQ(r.errorCount(), 0) << "the shipped asset library should have no validation errors";
}
