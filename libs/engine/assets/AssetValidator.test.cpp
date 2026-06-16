// Tests for AssetValidator: load-time asset definition checks.

#include "assets/AssetValidator.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace engine::assets;

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
