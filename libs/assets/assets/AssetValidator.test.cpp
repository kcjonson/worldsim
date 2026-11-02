// Tests for AssetValidator: load-time asset definition checks.

#include "assets/AssetValidator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

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

	// Write assets/<folder>/<folder>.xml as a procedural ground def (defName Ground_<folder>).
	void addGround(TempAssets& t, const std::string& folder, const std::string& extra) {
		t.writeXml(folder, oneDef("<defName>Ground_" + folder + "</defName><role>ground</role><assetType>procedural</assetType>" + extra));
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
	EXPECT_FALSE(hasIssue(r, Severity::Error, "assetType"));
	EXPECT_FALSE(hasIssue(r, Severity::Warning, "role")) << "ground is a known role";
}

TEST(AssetValidatorTest, SvgGroundDefIsNotSupportedYet) {
	// The baker only runs generators (AssetRegistry::buildPaths rejects a non-procedural def).
	for (const char* assetType : {"simple", "Simple"}) {
		TempAssets t;
		t.writeXml(
			"Grass",
			oneDef(
				std::string("<defName>Ground_Grass</defName><role>ground</role><surface>Grass</surface><assetType>") + assetType +
				"</assetType><svgPath>tile.svg</svgPath>"
			)
		);
		t.writeFile("Grass/tile.svg", "<svg/>");

		const ValidationReport r = AssetValidator::validate(t.root, "");
		EXPECT_TRUE(hasIssue(r, Severity::Error, "assetType")) << assetType;
		EXPECT_TRUE(hasMessageContaining(r, Severity::Error, "SVG ground defs aren't supported yet")) << assetType;
	}
}

TEST(AssetValidatorTest, GroundPeriodMustDivideTheChunk) {
	EXPECT_EQ(validateGround("<surface>Grass</surface><period>0.5</period>").errorCount(), 0);
	EXPECT_EQ(validateGround("<surface>Grass</surface><period>64</period><pixelsPerMeter>16</pixelsPerMeter>").errorCount(), 0);
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

TEST(AssetValidatorTest, GroundLayerFitsTheBaker) {
	// The baker takes 1 to 1024 texels along a layer's edge (period x pixelsPerMeter), and rejects the
	// def before allocating anything otherwise.
	for (const char* good :
		 {"<period>64</period><pixelsPerMeter>16</pixelsPerMeter>", "<period>2</period><pixelsPerMeter>512</pixelsPerMeter>"}) {
		EXPECT_EQ(validateGround(std::string("<surface>Grass</surface>") + good).errorCount(), 0) << good;
	}
	for (const char* tooBig : {
			 "<period>512</period><pixelsPerMeter>16</pixelsPerMeter>",
			 "<period>128</period><pixelsPerMeter>16</pixelsPerMeter>",
			 "<period>4</period><pixelsPerMeter>512</pixelsPerMeter>",
			 "<period>8</period><pixelsPerMeter>256</pixelsPerMeter>",
		 }) {
		const ValidationReport r = validateGround(std::string("<surface>Grass</surface>") + tooBig);
		EXPECT_TRUE(hasIssue(r, Severity::Error, "period")) << tooBig;
		EXPECT_TRUE(hasMessageContaining(r, Severity::Error, "from 1 to 1024 pixels")) << tooBig;
		EXPECT_EQ(r.errorCount(), 1) << tooBig;
	}
}

TEST(AssetValidatorTest, GroundPixelsPerMeterIsAPowerOfTwoInRange) {
	for (const char* good : {"16", "64", "128", "512"}) {
		EXPECT_EQ(
			validateGround(std::string("<surface>Grass</surface><period>1</period><pixelsPerMeter>") + good + "</pixelsPerMeter>")
				.errorCount(),
			0
		) << good;
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

TEST(AssetValidatorTest, GroundDefsUseNoMoreLayerSizesThanTheBankHolds) {
	// Each def a 4 m tile, so its pixelsPerMeter sets its layer: 64 to 1024 texels, one texture array a size.
	const auto validateSizes = [](const std::vector<int>& pixelsPerMeter) {
		TempAssets	t;
		const char* surfaces[] = {"Grass", "Dirt", "Sand", "Rock", "Snow", "Mud", "GrassTall", "GrassShort", "GrassMeadow"};
		for (size_t i = 0; i < std::size(surfaces); ++i) {
			const int density = pixelsPerMeter[std::min(i, pixelsPerMeter.size() - 1)];
			addGround(
				t,
				surfaces[i],
				std::string("<surface>") + surfaces[i] + "</surface><pixelsPerMeter>" + std::to_string(density) + "</pixelsPerMeter>"
			);
		}
		return AssetValidator::validate(t.root, "");
	};
	EXPECT_EQ(validateSizes({16, 32, 64, 128}).errorCount(), 0) << "four sizes fill the four arrays";
	const ValidationReport five = validateSizes({16, 32, 64, 128, 256});
	EXPECT_EQ(five.errorCount(), 1);
	EXPECT_TRUE(hasMessageContaining(five, Severity::Error, "5 layer sizes")) << "one more size than the tile shader holds";
}

TEST(AssetValidatorTest, GroundMacroIsFiniteNumbersInRange) {
	const std::string good =
		"<macro><dry>1.08,1.05,1</dry><lush> 0.92, 0.95 ,1 </lush><amplitude>0.5</amplitude><wavelengths>96,24</wavelengths></macro>";
	EXPECT_EQ(validateGround("<surface>Grass</surface>" + good).errorCount(), 0);
	EXPECT_EQ(validateGround("<surface>Grass</surface><macro><amplitude>1</amplitude></macro>").errorCount(), 0)
		<< "children left out keep their defaults";

	for (const char* bad : {
			 "<dry>1,2</dry>",
			 "<dry>1,1,1,1</dry>",
			 "<dry>1,,1</dry>",
			 "<dry>nan,1,1</dry>",
			 "<lush>1,inf,1</lush>",
			 "<lush>-0.1,1,1</lush>",
			 "<lush>1,2.5,1</lush>",
			 "<lush>green</lush>",
			 "<amplitude>nan</amplitude>",
			 "<amplitude>-0.5</amplitude>",
			 "<amplitude>1.5</amplitude>",
			 "<amplitude>0.5x</amplitude>",
			 "<wavelengths>96</wavelengths>",
			 "<wavelengths>0,24</wavelengths>",
			 "<wavelengths>96,1e40</wavelengths>",
		 }) {
		const ValidationReport r = validateGround(std::string("<surface>Grass</surface><macro>") + bad + "</macro>");
		EXPECT_TRUE(hasIssue(r, Severity::Error, "macro")) << bad;
		EXPECT_EQ(r.errorCount(), 1) << bad;
	}
}

TEST(AssetValidatorTest, UnknownGroundRotateIsWarning) {
	EXPECT_TRUE(hasIssue(validateGround("<surface>Grass</surface><rotate>spin</rotate>"), Severity::Warning, "rotate"));
	EXPECT_FALSE(hasIssue(validateGround("<surface>Grass</surface><rotate>quarter</rotate>"), Severity::Warning, "rotate"));
	EXPECT_FALSE(hasIssue(validateGround("<surface>Grass</surface><rotate>mirror</rotate>"), Severity::Warning, "rotate"));
}

TEST(AssetValidatorTest, GroundTransitionRecipesWithinTheirRulesAreClean) {
	const ValidationReport r = validateGround(
		"<surface>Grass</surface>"
		"<transition><width>1.4</width><softness>0.15</softness></transition>"
		"<transition over=\"Dirt\"><width>0.5</width><offset>0.15</offset><softness>0.06</softness><shadow>0.25</shadow></transition>"
		"<transition over=\"Sand\"><width>1.2</width><offset>-0.3</offset><breakup>0.8</breakup><stain>1</stain><ridge>0</ridge><tint>0.5</tint></transition>"
	);
	EXPECT_FALSE(hasIssue(r, Severity::Error, "transition"));
	EXPECT_FALSE(hasIssue(r, Severity::Warning, "transition"));
}

TEST(AssetValidatorTest, GroundTransitionOverMustNameALowerLandSurface) {
	EXPECT_TRUE(hasMessageContaining(validateGround("<surface>Grass</surface><transition over=\"Rock\"/>"), Severity::Error, "below this one"));
	EXPECT_TRUE(hasMessageContaining(validateGround("<surface>Grass</surface><transition over=\"Grass\"/>"), Severity::Error, "below this one"));
	EXPECT_TRUE(hasMessageContaining(validateGround("<surface>Grass</surface><transition over=\"Water\"/>"), Severity::Error, "land surface"));
	EXPECT_TRUE(hasMessageContaining(validateGround("<surface>Grass</surface><transition over=\"Lava\"/>"), Severity::Error, "land surface"));
	EXPECT_FALSE(hasIssue(validateGround("<surface>Grass</surface><transition over=\"GrassShort\"/>"), Severity::Error, "transition"));
}

TEST(AssetValidatorTest, GroundTransitionBandIsCappedAtTheFieldsReach) {
	EXPECT_TRUE(hasMessageContaining(
		validateGround("<surface>Grass</surface><transition><width>1.4</width><offset>0.2</offset></transition>"), Severity::Error, "at most 1.5 m"
	));
	EXPECT_FALSE(hasIssue(validateGround("<surface>Grass</surface><transition><width>1.2</width><offset>0.3</offset></transition>"), Severity::Error, "transition"))
		<< "exactly at the reach";
	// An override is checked as resolved: its offset against the default block's width.
	EXPECT_TRUE(hasMessageContaining(
		validateGround("<surface>Grass</surface><transition><width>1.3</width></transition><transition over=\"Dirt\"><offset>0.25</offset></transition>"),
		Severity::Error,
		"at most 1.5 m"
	));
}

TEST(AssetValidatorTest, GroundTransitionOffsetMustStayInsideTheWidth) {
	EXPECT_TRUE(hasMessageContaining(
		validateGround("<surface>Grass</surface><transition><width>0.3</width><offset>0.3</offset></transition>"), Severity::Error, "strictly between"
	));
	EXPECT_TRUE(hasMessageContaining(
		validateGround("<surface>Grass</surface><transition><width>0.3</width><offset>-0.4</offset></transition>"), Severity::Error, "strictly between"
	));
}

TEST(AssetValidatorTest, GroundTransitionFieldsAreNumbersInRange) {
	for (const char* bad : {"<width>0.01</width>", "<width>1.5</width>", "<softness>0.02</softness>", "<softness>0.4</softness>", "<breakup>0.9</breakup>",
							"<shadow>-0.1</shadow>", "<stain>1.5</stain>", "<ridge>2</ridge>", "<tint>-1</tint>"}) {
		EXPECT_TRUE(hasMessageContaining(validateGround(std::string("<surface>Grass</surface><transition>") + bad + "</transition>"), Severity::Error, "must be from"))
			<< bad;
	}
	EXPECT_TRUE(hasMessageContaining(
		validateGround("<surface>Grass</surface><transition><width>wide</width></transition>"), Severity::Error, "is not a number"
	));
}

TEST(AssetValidatorTest, GroundTransitionDuplicatesAreErrorsAndUnknownFieldsWarnings) {
	EXPECT_TRUE(hasMessageContaining(validateGround("<surface>Grass</surface><transition/><transition/>"), Severity::Error, "second default"));
	EXPECT_TRUE(hasMessageContaining(
		validateGround("<surface>Grass</surface><transition over=\"Dirt\"/><transition over=\"Dirt\"/>"), Severity::Error, "second <transition>"
	));
	const ValidationReport unknown = validateGround("<surface>Grass</surface><transition><glow>1</glow></transition>");
	EXPECT_TRUE(hasMessageContaining(unknown, Severity::Warning, "not a transition field"));
	EXPECT_FALSE(hasIssue(unknown, Severity::Error, "transition"));
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
