// Tests for the view config (assets/config/rendering/view.xml, rotatable-placed-items.md section 18): the shipped
// file loads, and a missing file, a missing key, an out-of-range value, and a malformed colour each land in the
// registry's validation report naming what is wrong.

#include "assets/AssetBootstrap.h"
#include "assets/AssetRegistry.h"
#include "assets/AssetRegistry.test.h"
#include "assets/ViewConfig.h"

#include <gtest/gtest.h>

#include <string>

using namespace assets;

namespace {

	// Loads the asset system from an empty definitions folder with `config` as the config root, and returns the
	// report's view problems joined.
	std::string viewProblems(const std::filesystem::path& config) {
		const TempAssetFolder definitions;
		loadAssetSystem({.definitions = definitions.root.string(), .sharedScripts = "", .recipes = "", .config = config.string()}, false);
		std::string joined;
		for (const ValidationIssue& issue : AssetRegistry::Get().getValidationReport().issues) {
			if (issue.field == "view" && issue.severity == Severity::Error) {
				joined += issue.message + "\n";
			}
		}
		AssetRegistry::Get().clearDefinitions();
		return joined;
	}

	// kTestViewXml with `from` replaced by `to`.
	std::string viewXmlWith(const std::string& from, const std::string& to) {
		std::string text = kTestViewXml;
		const size_t at = text.find(from);
		EXPECT_NE(at, std::string::npos) << from;
		return text.replace(at, from.size(), to);
	}

} // namespace

TEST(ViewConfigTest, TheShippedFileLoadsWithoutProblems) {
	EXPECT_EQ(viewProblems(shippedConfigFolder()), "");
	EXPECT_TRUE(GameViewConfig::Get().loaded());
	EXPECT_GT(GameViewConfig::Get().config().heightRatio, 0.0F);
}

TEST(ViewConfigTest, ATestFileLoadsItsValues) {
	ASSERT_TRUE(loadTestViewConfig());
	const ViewConfig& config = GameViewConfig::Get().config();
	EXPECT_FLOAT_EQ(config.heightRatio, 0.35F);
	EXPECT_FLOAT_EQ(config.lightAzimuthDegrees, 315.0F);
	EXPECT_FLOAT_EQ(config.outlineTopColor.a, 0.8F);
	EXPECT_FLOAT_EQ(config.outlineHalfWidthMeters(), 0.015F);
	EXPECT_FLOAT_EQ(config.depthStripMergeMeters, 0.02F);
}

TEST(ViewConfigTest, AMissingFileIsReported) {
	const TempAssetFolder empty;
	const std::string	  problems = viewProblems(empty.root);
	EXPECT_NE(problems.find("rendering/view.xml: not readable XML"), std::string::npos) << problems;
	EXPECT_FALSE(GameViewConfig::Get().loaded());
}

TEST(ViewConfigTest, AMissingKeyAnOutOfRangeValueAndAMalformedColourAreEachReportedByName) {
	const auto problemsWith = [](const std::string& from, const std::string& to) {
		const TempAssetFolder config;
		config.write(kViewConfigFile, viewXmlWith(from, to));
		return viewProblems(config.root);
	};
	const std::string missing = problemsWith("<depthStripRiseMeters>0.1</depthStripRiseMeters>", "");
	EXPECT_NE(missing.find("depthStripRiseMeters is missing"), std::string::npos) << missing;
	const std::string outOfRange = problemsWith("elevationDegrees=\"40\"", "elevationDegrees=\"95\"");
	EXPECT_NE(outOfRange.find("elevationDegrees must be from 0 to 90, got \"95\""), std::string::npos) << outOfRange;
	const std::string notPositive = problemsWith("<heightRatio>0.35</heightRatio>", "<heightRatio>0</heightRatio>");
	EXPECT_NE(notPositive.find("heightRatio must be above 0"), std::string::npos) << notPositive;
	const std::string colour = problemsWith("topColor=\"#33241A\"", "topColor=\"brown\"");
	EXPECT_NE(colour.find("topColor must be a colour, #RRGGBB"), std::string::npos) << colour;
	EXPECT_FALSE(GameViewConfig::Get().loaded());
}
