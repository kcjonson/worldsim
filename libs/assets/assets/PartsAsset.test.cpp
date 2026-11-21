// Tests for parts defs (rotatable-placed-items.md, sections 4, 5, 6.1, 11.1): the workbench, a stool, a round
// part, and concave and pinched colliders load; each validation rule refuses its bad fixture; and a parts def,
// which has no template yet, is safe to ask for one. Fixtures are written to a temp folder per test.

#include "assets/AssetRegistry.h"
#include "assets/AssetRegistry.test.h"
#include "assets/AssetValidator.h"
#include "assets/PartsAsset.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace assets;

namespace {

	std::string number(float value) {
		char buffer[32];
		std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
		return buffer;
	}

	// An SVG whose viewBox has the proportions of width x height (100 units a metre), filled edge to edge.
	std::string svg(float width, float height, const std::string& inner = "") {
		const std::string boxWidth = number(width * 100.0F);
		const std::string boxHeight = number(height * 100.0F);
		return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " + boxWidth + " " + boxHeight + "\"><rect x=\"0\" y=\"0\" width=\"" +
			   boxWidth + "\" height=\"" + boxHeight + "\" fill=\"#808080\"/>" + inner + "</svg>";
	}

	std::string partsDef(const std::string& name, const std::string& inner) {
		return "<?xml version=\"1.0\"?><AssetDefinitions><AssetDef><defName>" + name + "</defName><assetType>parts</assetType>" + inner +
			   "</AssetDef></AssetDefinitions>";
	}

	std::string rect(float minX, float minY, float maxX, float maxY) {
		return "<rect minX=\"" + number(minX) + "\" minY=\"" + number(minY) + "\" maxX=\"" + number(maxX) + "\" maxY=\"" + number(maxY) + "\"/>";
	}

	// A plain block part: a rect footprint, a height, plain colours.
	std::string block(const std::string& name, float minX, float minY, float maxX, float maxY, float bottom, float top, const std::string& extra = "") {
		return "<part name=\"" + name + "\"><footprint>" + rect(minX, minY, maxX, maxY) + "</footprint><height bottom=\"" + number(bottom) +
			   "\" top=\"" + number(top) + "\"/><top color=\"#9A6A3F\"/>" + (top > bottom ? "<sides color=\"#9A6A3F\"/>" : "") + extra + "</part>";
	}

	std::string place(const std::string& part, float x = 0.0F, float y = 0.0F, float turnDegrees = 0.0F) {
		return "<place part=\"" + part + "\" x=\"" + number(x) + "\" y=\"" + number(y) + "\" turnDegrees=\"" + number(turnDegrees) + "\"/>";
	}

	double area(const std::vector<glm::vec2>& ring) {
		double doubled = 0.0;
		for (size_t i = 0; i < ring.size(); ++i) {
			const glm::vec2& from = ring[i];
			const glm::vec2& to = ring[(i + 1) % ring.size()];
			doubled += static_cast<double>(from.x) * to.y - static_cast<double>(to.x) * from.y;
		}
		return doubled * 0.5;
	}

	double area(const ItemArea& region) {
		double total = 0.0;
		for (const AreaPolygon& polygon : region.polygons) {
			total += area(polygon.outer);
			for (const auto& hole : polygon.holes) {
				total += area(hole);
			}
		}
		return total;
	}

	class PartsAssetTest : public ::testing::Test {
	  protected:
		void TearDown() override { AssetRegistry::Get().clearDefinitions(); }

		const AssetDefinition* load(const std::string& name) {
			AssetRegistry& registry = AssetRegistry::Get();
			registry.clearDefinitions();
			registry.loadDefinitionsFromFolder(folder.root.string());
			return registry.getDefinition(name);
		}

		void writeDef(const std::string& name, const std::string& inner) { folder.write(name + "/" + name + ".xml", partsDef(name, inner)); }

		std::vector<std::string> issues(const std::string& defName, Severity severity = Severity::Error) const {
			std::vector<std::string> out;
			for (const ValidationIssue& issue : AssetRegistry::Get().getValidationReport().issues) {
				if (issue.defName == defName && issue.severity == severity) {
					out.push_back(issue.message);
				}
			}
			return out;
		}

		::testing::AssertionResult hasIssue(const std::string& defName, const std::string& part, Severity severity = Severity::Error) const {
			const std::vector<std::string> found = issues(defName, severity);
			if (std::any_of(found.begin(), found.end(), [&](const std::string& message) { return message.find(part) != std::string::npos; })) {
				return ::testing::AssertionSuccess();
			}
			std::string all;
			for (const std::string& message : found) {
				all += "\n  " + message;
			}
			return ::testing::AssertionFailure() << "no issue containing '" << part << "' for " << defName << "; got:" << all;
		}

		// The workbench of section 4.4, every drawing at its face's proportions.
		void writeWorkbench(const std::string& extraParts = "", const std::string& extraPlacements = "") {
			const std::string dir = "Workbench/";
			folder.write(dir + "carcass-front.svg", svg(2.84F, 0.72F));
			folder.write(dir + "carcass-back.svg", svg(2.84F, 0.72F));
			folder.write(dir + "carcass-end.svg", svg(0.84F, 0.72F));
			folder.write(dir + "worktop-top.svg", svg(3.0F, 1.0F, "<g data-lod=\"60\"><rect x=\"10\" y=\"10\" width=\"20\" height=\"20\" fill=\"#000\"/></g>"));
			folder.write(dir + "worktop-end.svg", svg(1.0F, 0.08F));
			folder.write(dir + "vise-top.svg", svg(0.32F, 0.3F));
			folder.write(dir + "vise-front.svg", svg(0.32F, 0.18F));
			folder.write(dir + "vise-end.svg", svg(0.3F, 0.18F));
			folder.write(dir + "toolbox-top.svg", svg(0.46F, 0.28F));
			folder.write(dir + "toolbox-front.svg", svg(0.46F, 0.2F));
			folder.write(dir + "toolbox-back.svg", svg(0.46F, 0.2F));
			folder.write(dir + "toolbox-end.svg", svg(0.28F, 0.2F));
			writeDef(
				"Workbench",
				"<category>Furniture</category><handsRequired>2</handsRequired><capabilities><craftable/></capabilities>"
				"<detailLevels>8, 24, 60</detailLevels><parts>"
				"<part name=\"carcass\"><footprint>" + rect(-1.42F, -0.42F, 1.42F, 0.42F) + "</footprint><height bottom=\"0\" top=\"0.72\"/>"
				"<top color=\"#9A6A3F\"/><sides color=\"#9A6A3F\"><face side=\"front\" drawing=\"carcass-front.svg\"/>"
				"<face side=\"back\" drawing=\"carcass-back.svg\"/><face side=\"left right\" drawing=\"carcass-end.svg\"/></sides></part>"
				"<part name=\"worktop\"><footprint>" + rect(-1.5F, -0.5F, 1.5F, 0.5F) + "</footprint><height bottom=\"0.72\" top=\"0.8\"/>"
				"<top drawing=\"worktop-top.svg\"/><sides color=\"#B38654\"><face side=\"left right\" drawing=\"worktop-end.svg\"/></sides></part>"
				"<part name=\"vise\"><footprint>" + rect(-0.16F, -0.15F, 0.16F, 0.15F) + "</footprint><height bottom=\"0.8\" top=\"0.98\"/>"
				"<top drawing=\"vise-top.svg\"/><sides color=\"#8D9193\"><face side=\"front back\" drawing=\"vise-front.svg\"/>"
				"<face side=\"left right\" drawing=\"vise-end.svg\"/></sides></part>"
				"<part name=\"toolbox\"><footprint>" + rect(-0.23F, -0.14F, 0.23F, 0.14F) + "</footprint><height bottom=\"0.8\" top=\"1.0\"/>"
				"<top drawing=\"toolbox-top.svg\"/><sides color=\"#8B4A37\"><face side=\"front\" drawing=\"toolbox-front.svg\"/>"
				"<face side=\"back\" drawing=\"toolbox-back.svg\"/><face side=\"left right\" drawing=\"toolbox-end.svg\"/></sides></part>" +
					extraParts + "</parts><placements>" + place("carcass") + place("worktop") + place("vise", 1.18F, 0.39F) +
					place("toolbox", -1.09F, -0.28F) + extraPlacements + "</placements>"
				"<useAreas>" + rect(-1.2F, 0.5F, 1.2F, 1.2F) + "</useAreas><clearance>" + rect(-1.5F, 0.5F, 1.5F, 1.1F) + "</clearance>"
			);
		}

		TempAssetFolder folder;
	};

} // namespace

TEST_F(PartsAssetTest, TheWorkbenchLoadsIntoPartsDrawingsAreasAndACollider) {
	writeWorkbench();
	const AssetDefinition* def = load("Workbench");
	ASSERT_NE(def, nullptr);
	EXPECT_EQ(def->assetType, AssetType::Parts);
	EXPECT_EQ(def->detailLevels, (std::vector<float>{8.0F, 24.0F, 60.0F}));
	EXPECT_TRUE(issues("Workbench").empty()) << ::testing::PrintToString(issues("Workbench"));
	EXPECT_TRUE(issues("Workbench", Severity::Warning).empty()) << ::testing::PrintToString(issues("Workbench", Severity::Warning));
	ASSERT_NE(def->parts, nullptr);
	const PartsAsset& parts = *def->parts;
	ASSERT_EQ(parts.parts.size(), 4U);
	EXPECT_EQ(parts.placements.size(), 4U);
	EXPECT_EQ(parts.drawings.size(), 12U) << "twelve drawings: three tops and nine sides";
	EXPECT_FLOAT_EQ(parts.bodyTopMeters, 1.0F) << "the toolbox's top";

	// The carcass's faces run back, right, front, left; its ends share one drawing.
	const PartDefinition& carcass = parts.parts[0];
	ASSERT_EQ(carcass.faces.size(), 4U);
	EXPECT_FLOAT_EQ(carcass.faces[2].normal.y, 1.0F) << "the front faces +Y";
	EXPECT_NEAR(carcass.faces[2].lengthMeters, 2.84F, 1e-5F);
	ASSERT_TRUE(carcass.faces[1].drawing.has_value() && carcass.faces[3].drawing.has_value());
	EXPECT_EQ(*carcass.faces[1].drawing, *carcass.faces[3].drawing);
	EXPECT_EQ(parts.drawings[*carcass.faces[2].drawing].file, "carcass-front.svg");
	// The worktop's long edges are plain.
	const PartDefinition& worktop = parts.parts[1];
	EXPECT_FALSE(worktop.faces[0].drawing.has_value());
	EXPECT_FALSE(worktop.faces[2].drawing.has_value());

	// Its top drawing is in metres over the 3 m x 1 m footprint, each shape with its effective data-lod.
	ASSERT_TRUE(worktop.topDrawing.has_value());
	const ItemDrawing& top = parts.drawings[*worktop.topDrawing];
	ASSERT_EQ(top.shapes.size(), 2U);
	float maxX = 0.0F;
	float maxY = 0.0F;
	for (const auto& vertex : top.shapes[0].shape.paths.front().vertices) {
		maxX = std::max(maxX, vertex.x);
		maxY = std::max(maxY, vertex.y);
	}
	EXPECT_NEAR(maxX, 3.0F, 1e-4F);
	EXPECT_NEAR(maxY, 1.0F, 1e-4F);
	EXPECT_FLOAT_EQ(top.shapes[0].minPixelsPerMeter, 0.0F);
	EXPECT_FLOAT_EQ(top.shapes[1].minPixelsPerMeter, 60.0F);

	// The collider is the worktop with the vise's 4 cm overhang at its front.
	ASSERT_EQ(def->partsColliders.size(), 1U);
	EXPECT_NEAR(area(def->partsColliders[0].pointsMeters), 3.0 + 0.32 * 0.04, 1e-4);
	ASSERT_EQ(parts.useAreas.size(), 1U);
	EXPECT_NEAR(area(parts.useAreas[0]), 2.4 * 0.7, 1e-4);
	EXPECT_NEAR(area(parts.clearance), 3.0 * 0.6, 1e-4);
}

TEST_F(PartsAssetTest, AStoolPlacesOneLegFourTimesAndTakesTheDefaultUseArea) {
	folder.write("Stool/seat-top.svg", svg(0.5F, 0.5F));
	writeDef(
		"Stool",
		"<parts>" + block("leg", -0.035F, -0.035F, 0.035F, 0.035F, 0.0F, 0.43F) +
			"<part name=\"seat\"><footprint>" + rect(-0.25F, -0.25F, 0.25F, 0.25F) +
			"</footprint><height bottom=\"0.43\" top=\"0.5\"/><top drawing=\"seat-top.svg\"/><sides color=\"#B38654\"/></part></parts>"
			"<placements>" + place("leg", -0.19F, -0.19F) + place("leg", 0.19F, -0.19F) + place("leg", 0.19F, 0.19F) + place("leg", -0.19F, 0.19F) +
			place("seat") + "</placements>"
	);
	const AssetDefinition* def = load("Stool");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(issues("Stool").empty()) << ::testing::PrintToString(issues("Stool"));
	EXPECT_EQ(def->parts->parts.size(), 2U);
	EXPECT_EQ(def->parts->placements.size(), 5U);
	ASSERT_EQ(def->partsColliders.size(), 1U) << "the legs lie under the seat";
	EXPECT_NEAR(area(def->partsColliders[0].pointsMeters), 0.25, 1e-4);
	ASSERT_EQ(def->parts->useAreas.size(), 1U);
	// The seat grown 0.7 m with rounded corners: 0.25 + perimeter x 0.7 + pi x 0.49, a little under for the flattening.
	EXPECT_NEAR(area(def->parts->useAreas[0]), 0.25 + 2.0 * 0.7 + 3.14159 * 0.49, 0.01);
}

TEST_F(PartsAssetTest, ARoundPartHasNFacesTakingTheSidesDrawing) {
	const float edge = 2.0F * 0.3F * std::sin(3.14159265F / 8.0F);
	folder.write("Drum/stave.svg", svg(edge, 0.6F));
	writeDef(
		"Drum",
		"<parts><part name=\"drum\"><footprint><round x=\"0\" y=\"0\" radius=\"0.3\" sides=\"8\"/></footprint><height bottom=\"0\" top=\"0.6\"/>"
		"<top color=\"#556677\"/><sides color=\"#445566\" drawing=\"stave.svg\"/></part></parts><placements>" + place("drum") + "</placements>"
	);
	const AssetDefinition* def = load("Drum");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(issues("Drum").empty()) << ::testing::PrintToString(issues("Drum"));
	const PartDefinition& drum = def->parts->parts[0];
	ASSERT_EQ(drum.footprint.size(), 8U);
	EXPECT_NEAR(std::atan2(drum.footprint[0].y, drum.footprint[0].x), 3.14159265F / 8.0F, 1e-5F) << "vertex i at (i + 0.5) 2 pi / sides";
	ASSERT_EQ(drum.faces.size(), 8U);
	for (const PartFace& face : drum.faces) {
		EXPECT_TRUE(face.drawing.has_value());
	}
	EXPECT_EQ(def->parts->drawings.size(), 1U) << "one drawing, mapped once for the one size its faces share";
}

TEST_F(PartsAssetTest, AnLShapedItemKeepsItsNotchAndAUseAreaInItHasRoom) {
	writeDef(
		"Counter",
		"<parts>" + block("long", 0.0F, 0.0F, 3.0F, 0.6F, 0.0F, 0.9F) + block("arm", 0.0F, 0.6F, 0.6F, 2.0F, 0.0F, 0.9F) +
			"</parts><placements>" + place("long") + place("arm") + "</placements><useAreas>" + rect(0.6F, 0.6F, 1.6F, 1.6F) + "</useAreas>"
	);
	const AssetDefinition* def = load("Counter");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(issues("Counter").empty()) << ::testing::PrintToString(issues("Counter"));
	ASSERT_EQ(def->partsColliders.size(), 1U);
	EXPECT_EQ(def->partsColliders[0].pointsMeters.size(), 6U) << "concave, the notch kept";
	EXPECT_NEAR(area(def->partsColliders[0].pointsMeters), 3.0 * 0.6 + 0.6 * 1.4, 1e-4);
}

TEST_F(PartsAssetTest, PartsTouchingAtACornerAreTwoRingsAndEnclosedGroundIsFilled) {
	writeDef(
		"Pair",
		"<parts>" + block("a", 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.5F) + block("b", 1.0F, 1.0F, 2.0F, 2.0F, 0.0F, 0.5F) + "</parts><placements>" +
			place("a") + place("b") + "</placements>"
	);
	writeDef(
		"Frame",
		"<parts>" + block("bar", 0.0F, 0.0F, 3.0F, 0.5F, 0.0F, 0.5F) + block("post", 0.0F, 0.0F, 0.5F, 2.0F, 0.0F, 0.5F) + "</parts><placements>" +
			place("bar") + place("bar", 0.0F, 2.5F) + place("post", 0.0F, 0.5F) + place("post", 2.5F, 0.5F) + "</placements>"
	);
	const AssetDefinition* pair = load("Pair");
	const AssetDefinition* frame = AssetRegistry::Get().getDefinition("Frame");
	ASSERT_NE(pair, nullptr);
	ASSERT_NE(frame, nullptr);
	EXPECT_TRUE(issues("Pair").empty()) << ::testing::PrintToString(issues("Pair"));
	EXPECT_EQ(pair->partsColliders.size(), 2U) << "split at the pinch, each ring passing on its own";
	ASSERT_EQ(frame->partsColliders.size(), 1U);
	EXPECT_NEAR(area(frame->partsColliders[0].pointsMeters), 9.0, 1e-4) << "the hole inside the frame is filled";
}

TEST_F(PartsAssetTest, AFlatOnlyItemHasNoColliderAndItsUseAreaGrowsItsFootprint) {
	writeDef("Spot", "<parts>" + block("mat", -0.5F, -0.5F, 0.5F, 0.5F, 0.0F, 0.0F) + "</parts><placements>" + place("mat") + "</placements>");
	const AssetDefinition* def = load("Spot");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(issues("Spot").empty()) << ::testing::PrintToString(issues("Spot"));
	EXPECT_TRUE(def->partsColliders.empty());
	EXPECT_FLOAT_EQ(def->parts->bodyTopMeters, 0.0F);
	EXPECT_NEAR(area(def->parts->useAreas[0]), 1.0 + 4.0 * 0.7 + 3.14159 * 0.49, 0.02) << "a little under, for the flattened corners";
}

TEST_F(PartsAssetTest, APartsScriptAddsPartsAndPlacements) {
	folder.write(
		"Shelf/parts.lua",
		"for i = 0, 2 do\n"
		"  asset:place{ part = 'board', y = 0, x = 0 }\n"
		"end\n"
		"asset:addPart{ name = 'board', footprint = { rect = { minX = -0.5, minY = -0.2, maxX = 0.5, maxY = 0.2 } },"
		" height = { bottom = 0, top = 1.2 }, top = { color = '#9A6A3F' }, sides = { color = '#9A6A3F' } }\n"
		"asset:setHeight('board', { bottom = 0, top = 1.5 })\n"
	);
	writeDef("Shelf", "<partsScript>parts.lua</partsScript>");
	const AssetDefinition* def = load("Shelf");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(hasIssue("Shelf", "overlap in plan and in height")) << "three boards in one place";
	EXPECT_EQ(def->parts->placements.size(), 3U);
	EXPECT_FLOAT_EQ(def->parts->bodyTopMeters, 1.5F);
}

TEST_F(PartsAssetTest, APartsDefHasNoTemplateAndAskingForOneIsSafe) {
	writeWorkbench();
	ASSERT_NE(load("Workbench"), nullptr);
	AssetRegistry& registry = AssetRegistry::Get();
	const uint32_t id = registry.templateId("Workbench", {});
	EXPECT_NE(id, 0U);
	EXPECT_EQ(registry.getTemplate(id), nullptr);
	std::vector<renderer::TessellatedMesh> meshes;
	EXPECT_FALSE(registry.buildMeshes("Workbench", 0, meshes));
	EXPECT_FALSE(registry.isTallDef("Workbench"));
	const AssetSilhouette* silhouette = registry.getSilhouette(id);
	EXPECT_TRUE(silhouette == nullptr || !silhouette->valid);
	ValidationReport smoke;
	registry.checkDetailLevels(smoke);
	EXPECT_FALSE(smoke.hasErrors());
}

TEST_F(PartsAssetTest, TheFileValidatorKnowsPartsAndTheirDrawings) {
	writeWorkbench();
	folder.write("Workbench/stray.svg", svg(1.0F, 1.0F));
	const ValidationReport report = AssetValidator::validate(folder.root, {});
	size_t orphans = 0;
	for (const ValidationIssue& issue : report.issues) {
		if (issue.message.find("orphan") != std::string::npos) {
			++orphans;
			EXPECT_NE(issue.context.find("stray.svg"), std::string::npos) << issue.context;
		}
		EXPECT_EQ(issue.message.find("Unknown assetType"), std::string::npos);
	}
	EXPECT_EQ(orphans, 1U) << "the twelve drawings are referenced; only the stray file is an orphan";
}

// ─── One fixture per rule of section 5 ──────────────────────────────────────────────────────────────────

TEST_F(PartsAssetTest, Rule1RefusesWhatAPartsDefHasNoneOf) {
	writeDef("Bad", "<worldHeight>1.0</worldHeight><parts>" + block("a", 0, 0, 1, 1, 0, 1) + "</parts><placements>" + place("a") + "</placements>");
	load("Bad");
	EXPECT_TRUE(hasIssue("Bad", "a parts def has no <worldHeight>"));
}

TEST_F(PartsAssetTest, Rule2NamesArePlacedAndUnique) {
	writeDef("Dup", "<parts>" + block("a", 0, 0, 1, 1, 0, 1) + block("a", 0, 0, 1, 1, 1, 2) + "</parts><placements>" + place("a") + "</placements>");
	writeDef("Unknown", "<parts>" + block("a", 0, 0, 1, 1, 0, 1) + "</parts><placements>" + place("a") + place("b") + "</placements>");
	writeDef("Unplaced", "<parts>" + block("a", 0, 0, 1, 1, 0, 1) + block("b", 2, 0, 3, 1, 0, 1) + "</parts><placements>" + place("a") + "</placements>");
	writeDef("Empty", "<parts>" + block("a", 0, 0, 1, 1, 0, 1) + "</parts>");
	load("Dup");
	EXPECT_TRUE(hasIssue("Dup", "two parts are named 'a'"));
	EXPECT_TRUE(hasIssue("Unknown", "names part 'b'"));
	EXPECT_TRUE(hasIssue("Unplaced", "part 'b' is placed nowhere"));
	EXPECT_TRUE(hasIssue("Empty", "places no parts"));
}

TEST_F(PartsAssetTest, Rule3FootprintsHaveSizeAndSides) {
	writeDef("FlatRect", "<parts>" + block("a", 1, 0, 1, 1, 0, 1) + "</parts><placements>" + place("a") + "</placements>");
	writeDef(
		"ManySides",
		"<parts><part name=\"a\"><footprint><round radius=\"0.3\" sides=\"40\"/></footprint><height bottom=\"0\" top=\"1\"/><top color=\"#000000\"/>"
		"<sides color=\"#000000\"/></part></parts><placements>" + place("a") + "</placements>"
	);
	// Thinner than the overlap tolerance, rule 9 could never see them: refused. A millimetre exactly is fine.
	writeDef("ThinRect", "<parts>" + block("a", 0.2F, 0.5F, 0.8F, 0.5008F, 0, 1) + "</parts><placements>" + place("a") + "</placements>");
	writeDef("MillimetreRect", "<parts>" + block("a", 0.0F, 0.0F, 0.8F, 0.0011F, 0, 1) + "</parts><placements>" + place("a") + "</placements>");
	writeDef(
		"ThinRound",
		"<parts><part name=\"a\"><footprint><round radius=\"0.0004\" sides=\"6\"/></footprint><height bottom=\"0\" top=\"1\"/><top color=\"#000000\"/>"
		"<sides color=\"#000000\"/></part></parts><placements>" + place("a") + "</placements>"
	);
	load("FlatRect");
	EXPECT_TRUE(hasIssue("FlatRect", "needs max > min on both axes"));
	EXPECT_TRUE(hasIssue("ManySides", "whole number of sides, 3 to 32"));
	EXPECT_TRUE(hasIssue("ThinRect", "thinner than the overlap tolerance"));
	EXPECT_TRUE(hasIssue("ThinRound", "thinner than the overlap tolerance"));
	EXPECT_FALSE(hasIssue("MillimetreRect", "thinner than the overlap tolerance"));
}

TEST_F(PartsAssetTest, Rule3RefusesFewSidesNoRadiusAndAFractionOfASide) {
	const auto roundPart = [&](const std::string& name, const std::string& round) {
		writeDef(
			name,
			"<parts><part name=\"drum\"><footprint>" + round + "</footprint><height bottom=\"0\" top=\"1\"/><top color=\"#000000\"/>"
			"<sides color=\"#000000\"/></part></parts><placements>" + place("drum") + "</placements>"
		);
	};
	roundPart("TwoSides", "<round radius=\"0.3\" sides=\"2\"/>");
	roundPart("NoRadius", "<round radius=\"0\" sides=\"6\"/>");
	roundPart("PartSide", "<round radius=\"0.3\" sides=\"4.6\"/>");
	folder.write("ScriptSide/parts.lua", "asset:setFootprint('drum', { round = { radius = 0.3, sides = 4.6 } })\n");
	writeDef(
		"ScriptSide",
		"<partsScript>parts.lua</partsScript><parts><part name=\"drum\"><footprint><round radius=\"0.3\" sides=\"6\"/></footprint>"
		"<height bottom=\"0\" top=\"1\"/><top color=\"#000000\"/><sides color=\"#000000\"/></part></parts><placements>" + place("drum") + "</placements>"
	);
	load("TwoSides");
	for (const char* name : {"TwoSides", "NoRadius", "PartSide", "ScriptSide"}) {
		EXPECT_TRUE(hasIssue(name, "whole number of sides, 3 to 32")) << name;
	}
}

TEST_F(PartsAssetTest, Rule4HeightsAndARaisedFlatPartRestsOnSomething) {
	writeDef("Upside", "<parts>" + block("a", 0, 0, 1, 1, 1, 0.5F) + "</parts><placements>" + place("a") + "</placements>");
	writeDef(
		"FlatSides",
		"<parts><part name=\"rug\"><footprint>" + rect(0, 0, 1, 1) +
			"</footprint><height bottom=\"0\" top=\"0\"/><top color=\"#000000\"/><sides color=\"#000000\"/></part></parts><placements>" +
			place("rug") + "</placements>"
	);
	// A cloth on a table rests on it; one hanging past the table's edge doesn't.
	const std::string table = block("table", -0.5F, -0.5F, 0.5F, 0.5F, 0.0F, 0.75F);
	writeDef(
		"Cloth",
		"<parts>" + table + block("cloth", -0.4F, -0.4F, 0.4F, 0.4F, 0.75F, 0.75F) + "</parts><placements>" + place("table") + place("cloth") + "</placements>"
	);
	writeDef(
		"Overhang",
		"<parts>" + table + block("cloth", -0.4F, -0.4F, 0.4F, 0.4F, 0.75F, 0.75F) + "</parts><placements>" + place("table") +
			place("cloth", 0.3F, 0.0F) + "</placements>"
	);
	load("Upside");
	EXPECT_TRUE(hasIssue("Upside", "0 <= bottom <= top"));
	EXPECT_TRUE(hasIssue("FlatSides", "drop its <sides>"));
	EXPECT_TRUE(issues("Cloth").empty()) << ::testing::PrintToString(issues("Cloth"));
	EXPECT_TRUE(hasIssue("Overhang", "rests on nothing"));
}

TEST_F(PartsAssetTest, Rule5FacesAreTheFootprintsOwnNamedOnce) {
	folder.write("Faces/end.svg", svg(1.0F, 1.0F));
	folder.write(
		"Faces/Faces.xml",
		partsDef(
			"Faces",
			"<parts><part name=\"a\"><footprint>" + rect(0, 0, 1, 1) +
				"</footprint><height bottom=\"0\" top=\"1\"/><top color=\"#000000\"/><sides color=\"#000000\"><face side=\"top\" drawing=\"end.svg\"/>"
				"<face side=\"left\" drawing=\"end.svg\"/><face side=\"left right\" drawing=\"end.svg\"/></sides></part>"
				"<part name=\"r\"><footprint><round radius=\"0.3\" sides=\"6\"/></footprint><height bottom=\"1\" top=\"2\"/><top color=\"#000000\"/>"
				"<sides color=\"#000000\"><face side=\"front\" drawing=\"end.svg\"/></sides></part></parts><placements>" +
				place("a") + place("r", 0.5F, 0.5F) + "</placements>"
		)
	);
	load("Faces");
	EXPECT_TRUE(hasIssue("Faces", "has no face 'top'"));
	EXPECT_TRUE(hasIssue("Faces", "names face 'left' more than once"));
	EXPECT_TRUE(hasIssue("Faces", "a round's faces have no names"));
}

TEST_F(PartsAssetTest, Rule6DrawingsExistInsideTheItemsFolder) {
	folder.write("Outside.svg", svg(1.0F, 1.0F));
	writeDef(
		"Art",
		"<parts><part name=\"a\"><footprint>" + rect(0, 0, 1, 1) + "</footprint><height bottom=\"0\" top=\"1\"/><top drawing=\"../Outside.svg\"/>"
		"<sides color=\"#000000\" drawing=\"missing.svg\"/></part></parts><placements>" + place("a") + "</placements>"
	);
	load("Art");
	EXPECT_TRUE(hasIssue("Art", "is outside the item's folder"));
	EXPECT_TRUE(hasIssue("Art", "'missing.svg' does not exist"));
}

TEST_F(PartsAssetTest, Rule7ProportionsMatchAndDataLodIsANumber) {
	folder.write("Skew/front.svg", svg(1.02F, 1.0F));
	folder.write("Skew/top.svg", svg(1.0F, 1.0F, "<rect data-lod=\"lots\" x=\"0\" y=\"0\" width=\"5\" height=\"5\" fill=\"#000\"/>"));
	writeDef(
		"Skew",
		"<parts><part name=\"a\"><footprint>" + rect(0, 0, 1, 1) + "</footprint><height bottom=\"0\" top=\"1\"/><top drawing=\"top.svg\"/>"
		"<sides color=\"#000000\"><face side=\"front\" drawing=\"front.svg\"/></sides></part></parts><placements>" + place("a") + "</placements>"
	);
	load("Skew");
	EXPECT_TRUE(hasIssue("Skew", "their proportions must match")) << "a side drawing 2% off its face";
	EXPECT_TRUE(hasIssue("Skew", "data-lod=\"lots\""));
}

TEST_F(PartsAssetTest, Rule8ArtPastTheViewBoxIsAWarning) {
	folder.write("Spill/top.svg", svg(1.0F, 1.0F, "<rect x=\"90\" y=\"0\" width=\"20\" height=\"10\" fill=\"#000\"/>"));
	writeDef("Spill", "<parts><part name=\"a\"><footprint>" + rect(0, 0, 1, 1) + "</footprint><height bottom=\"0\" top=\"1\"/><top drawing=\"top.svg\"/>"
					   "<sides color=\"#000000\"/></part></parts><placements>" + place("a") + "</placements>");
	load("Spill");
	EXPECT_TRUE(issues("Spill").empty()) << ::testing::PrintToString(issues("Spill"));
	EXPECT_TRUE(hasIssue("Spill", "draws past its viewBox", Severity::Warning));
}

TEST_F(PartsAssetTest, Rule9OverlappingPartsAreSeparatedAndTheOrderNeverCycles) {
	writeDef("Clash", "<parts>" + block("a", 0, 0, 1, 1, 0, 1) + block("b", 0.5F, 0.5F, 1.5F, 1.5F, 0.5F, 1.5F) + "</parts><placements>" +
						  place("a") + place("b") + "</placements>");
	// A raised board crossing over a box and down onto a slab: the slab before the board (it is lower and they
	// overlap), the board before the box (the box is south of it), and the box before the slab (the slab is south).
	writeDef(
		"Cycle",
		"<parts>" + block("slab", 0.0F, 2.0F, 3.0F, 2.2F, 0.0F, 0.1F) + block("box", 1.0F, 1.5F, 2.0F, 1.9F, 0.0F, 0.5F) +
			block("board", -1.026F, -0.05F, 1.026F, 0.05F, 0.5F, 0.6F) + "</parts><placements>" + place("slab") + place("box") +
			place("board", 0.8F, 1.35F, -46.97F) + "</placements>"
	);
	load("Clash");
	EXPECT_TRUE(hasIssue("Clash", "parts 'a' and 'b' overlap in plan and in height"));
	EXPECT_TRUE(hasIssue("Cycle", "parts order cycles between"));
	EXPECT_TRUE(hasIssue("Cycle", "degrees: board, box, slab"));
}

TEST_F(PartsAssetTest, Rule10TheColliderRingsPassTheNavMeshRules) {
	// Two parts 1 mm apart give one ring with two edges closer than the 3 mm the nav mesh needs.
	writeDef("Sliver", "<parts>" + block("a", 0, 0, 1, 1, 0, 1) + block("b", 0, 1.001F, 1.0F, 2.0F, 0, 1) + block("c", 0.9F, 0.5F, 1.5F, 1.5F, 0, 1) +
						   "</parts><placements>" + place("a") + place("b") + place("c") + "</placements>");
	const AssetDefinition* def = load("Sliver");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(hasIssue("Sliver", "collider ring 0"));
	EXPECT_TRUE(def->partsColliders.empty()) << "a ring the nav mesh can't take is dropped";
}

TEST_F(PartsAssetTest, Rule11AreaShapesAreValidAndEveryUseAreaHasRoom) {
	const std::string body = "<parts>" + block("box", -0.5F, -0.5F, 0.5F, 0.5F, 0, 1) + "</parts><placements>" + place("box") + "</placements>";
	writeDef("BadShapes", body + "<useAreas><area name=\"a\"><circle x=\"0\" y=\"1\" radius=\"0\"/>" + rect(1, 1, 0, 2) + "</area></useAreas>");
	writeDef("Cramped", body + "<useAreas><area name=\"slot\">" + rect(-0.25F, 0.5F, 0.25F, 1.0F) + "</area></useAreas>");
	folder.write("Outline/outline.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 50\"><rect x=\"0\" y=\"0\" width=\"100\" height=\"50\" fill=\"none\" stroke=\"#000\"/></svg>");
	writeDef("Outline", body + "<useAreas><drawing file=\"outline.svg\" minX=\"-1\" minY=\"0.5\" maxX=\"1\" maxY=\"1.5\"/></useAreas>");
	load("BadShapes");
	EXPECT_TRUE(hasIssue("BadShapes", "circle with no radius"));
	EXPECT_TRUE(hasIssue("BadShapes", "max isn't above its min"));
	EXPECT_TRUE(hasIssue("Cramped", "use area 'slot' has no room")) << "0.5 m across fits no 0.3 m disc";
	EXPECT_TRUE(hasIssue("Outline", "has no filled shape")) << "strokes are guides, not area";
}

TEST_F(PartsAssetTest, AnXmlRectAndADrawingOfItGiveTheSameArea) {
	const std::string body = "<parts>" + block("box", -0.5F, -0.5F, 0.5F, 0.5F, 0, 1) + "</parts><placements>" + place("box") + "</placements>";
	folder.write("Drawn/front.svg", svg(2.0F, 1.0F));
	writeDef("Drawn", body + "<useAreas><drawing file=\"front.svg\" minX=\"-1\" minY=\"0.5\" maxX=\"1\" maxY=\"1.5\"/></useAreas>");
	writeDef("Written", body + "<useAreas>" + rect(-1, 0.5F, 1, 1.5F) + "</useAreas>");
	const AssetDefinition* drawn = load("Drawn");
	const AssetDefinition* written = AssetRegistry::Get().getDefinition("Written");
	ASSERT_NE(drawn, nullptr);
	ASSERT_NE(written, nullptr);
	EXPECT_TRUE(issues("Drawn").empty()) << ::testing::PrintToString(issues("Drawn"));
	ASSERT_EQ(drawn->parts->useAreas[0].polygons.size(), 1U);
	ASSERT_EQ(written->parts->useAreas[0].polygons.size(), 1U);
	const auto& fromDrawing = drawn->parts->useAreas[0].polygons[0].outer;
	const auto& fromXml = written->parts->useAreas[0].polygons[0].outer;
	ASSERT_EQ(fromDrawing.size(), fromXml.size());
	for (size_t i = 0; i < fromDrawing.size(); ++i) {
		EXPECT_NEAR(fromDrawing[i].x, fromXml[i].x, 1e-5F);
		EXPECT_NEAR(fromDrawing[i].y, fromXml[i].y, 1e-5F);
	}
}

// ─── Touching, rounding, and the collider's islands ─────────────────────────────────────────────────────

TEST_F(PartsAssetTest, APartTouchingAnotherWithinRoundingIsATouchNotAnOverlap) {
	// The leg's corner is 11 micrometres inside the slab: under the millimetre the geometry rounds to.
	writeDef(
		"Touch",
		"<parts>" + block("slab", -0.5F, -0.3F, 0.5F, 0.3F, 0.0F, 0.8F) + block("leg", -0.05F, -0.05F, 0.05F, 0.05F, 0.0F, 0.8F) +
			"</parts><placements>" + place("slab") + place("leg", 0.5707F, 0.0F, 45.0F) + "</placements>"
	);
	const AssetDefinition* def = load("Touch");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(issues("Touch").empty()) << ::testing::PrintToString(issues("Touch"));
	EXPECT_EQ(def->partsColliders.size(), 2U) << "the leg's corner touches the slab's edge at a point, so the union splits there";
}

TEST_F(PartsAssetTest, PartsSharingAnEdgeAtThirtyDegreesTouchAndARealOverlapStillFails) {
	writeDef(
		"Shared",
		"<parts>" + block("near", 0.0F, 0.0F, 1.0F, 0.5F, 0.0F, 0.9F) + block("far", 0.0F, 0.5F, 1.0F, 1.0F, 0.0F, 0.9F) + "</parts><placements>" +
			place("near", 0.0F, 0.0F, 30.0F) + place("far", 0.0F, 0.0F, 30.0F) + "</placements>"
	);
	writeDef(
		"Deep",
		"<parts>" + block("near", 0.0F, 0.0F, 1.0F, 0.5F, 0.0F, 0.9F) + block("far", 0.0F, 0.45F, 1.0F, 1.0F, 0.0F, 0.9F) + "</parts><placements>" +
			place("near", 0.0F, 0.0F, 30.0F) + place("far", 0.0F, 0.0F, 30.0F) + "</placements>"
	);
	const AssetDefinition* shared = load("Shared");
	ASSERT_NE(shared, nullptr);
	EXPECT_TRUE(issues("Shared").empty()) << ::testing::PrintToString(issues("Shared"));
	EXPECT_EQ(shared->partsColliders.size(), 1U);
	EXPECT_TRUE(hasIssue("Deep", "overlap in plan and in height")) << "5 cm deep is an overlap";
}

TEST_F(PartsAssetTest, AnIslandInsideAFilledHoleAddsNoColliderRing) {
	writeDef(
		"Moat",
		"<parts>" + block("bar", 0.0F, 0.0F, 3.0F, 0.5F, 0.0F, 0.5F) + block("post", 0.0F, 0.0F, 0.5F, 2.0F, 0.0F, 0.5F) +
			block("plinth", 1.2F, 1.2F, 1.8F, 1.8F, 0.0F, 0.5F) + "</parts><placements>" + place("bar") + place("bar", 0.0F, 2.5F) +
			place("post", 0.0F, 0.5F) + place("post", 2.5F, 0.5F) + place("plinth") + "</placements>"
	);
	const AssetDefinition* def = load("Moat");
	ASSERT_NE(def, nullptr);
	ASSERT_EQ(def->partsColliders.size(), 1U) << "the plinth stands in the filled hole";
	EXPECT_NEAR(area(def->partsColliders[0].pointsMeters), 9.0, 1e-4);
}

TEST_F(PartsAssetTest, ALegTouchingAnySideOfASlabAtACornerKeepsItsRing) {
	// The leg's corner on the slab's left, right, top, and bottom side: the right one is the touch above mirrored.
	const std::vector<std::pair<std::string, glm::vec2>> sides = {
		{"Left", {-0.5707F, 0.0F}}, {"Right", {0.5707F, 0.0F}}, {"Top", {0.0F, -0.3707F}}, {"Bottom", {0.0F, 0.3707F}}};
	for (const auto& [name, at] : sides) {
		writeDef(
			name,
			"<parts>" + block("slab", -0.5F, -0.3F, 0.5F, 0.3F, 0.0F, 0.8F) + block("leg", -0.05F, -0.05F, 0.05F, 0.05F, 0.0F, 0.8F) +
				"</parts><placements>" + place("slab") + place("leg", at.x, at.y, 45.0F) + "</placements>"
		);
	}
	load("Left");
	for (const auto& [name, at] : sides) {
		const AssetDefinition* def = AssetRegistry::Get().getDefinition(name);
		ASSERT_NE(def, nullptr) << name;
		EXPECT_TRUE(issues(name).empty()) << name << ::testing::PrintToString(issues(name));
		EXPECT_EQ(def->partsColliders.size(), 2U) << name << ": the slab's ring and the leg's";
	}
}

TEST_F(PartsAssetTest, Rule9AnOverlapIsOneOverAMillimetreDeepWhateverItsLength) {
	const auto pair = [&](const std::string& name, const std::string& parts, const std::string& placements) {
		writeDef(name, "<parts>" + parts + "</parts><placements>" + placements + "</placements>");
	};
	// A box flush on a turned slab's front edge never overlaps it, at any turn.
	const std::vector<int> turns = {1, 7, 23, 37, 61, 89, 133, 271};
	for (const int turn : turns) {
		pair(
			"Flush" + std::to_string(turn),
			block("slab", -0.5F, -0.3F, 0.5F, 0.3F, 0.0F, 0.5F) + block("box", 0.15F, 0.3F, 0.25F, 0.4F, 0.0F, 0.5F),
			place("slab", 0.0F, 0.0F, static_cast<float>(turn)) + place("box", 0.0F, 0.0F, static_cast<float>(turn))
		);
	}
	// A 400 x 20 mm board reaching 9 mm into a block at its end, and a 20 mm leg 9 mm into a slab, overlap.
	pair("BoardEnd", block("board", 0.0F, 0.0F, 0.4F, 0.02F, 0.0F, 0.5F) + block("block", 0.391F, -0.1F, 0.8F, 0.1F, 0.0F, 0.5F), place("board") + place("block"));
	pair("LegIn", block("slab", 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.5F) + block("leg", -0.01F, 0.4F, 0.01F, 0.42F, 0.0F, 0.5F), place("slab") + place("leg", -0.001F, 0.0F));
	// Half a millimetre deep is a touch; two millimetres is an overlap.
	pair("HalfMillimetre", block("first", 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.5F) + block("second", 0.9995F, 0.0F, 2.0F, 1.0F, 0.0F, 0.5F), place("first") + place("second"));
	pair("TwoMillimetres", block("first", 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.5F) + block("second", 0.998F, 0.0F, 2.0F, 1.0F, 0.0F, 0.5F), place("first") + place("second"));
	load("BoardEnd");
	for (const int turn : turns) {
		const std::string name = "Flush" + std::to_string(turn);
		EXPECT_FALSE(hasIssue(name, "overlap in plan")) << name;
	}
	EXPECT_TRUE(hasIssue("BoardEnd", "overlap in plan"));
	EXPECT_TRUE(hasIssue("LegIn", "overlap in plan"));
	EXPECT_FALSE(hasIssue("HalfMillimetre", "overlap in plan"));
	EXPECT_TRUE(hasIssue("TwoMillimetres", "overlap in plan"));
}

TEST_F(PartsAssetTest, Rule9APartWhollyInsideAnotherAndARoundAgainstARect) {
	const auto pair = [&](const std::string& name, const std::string& parts, const std::string& placements) {
		writeDef(name, "<parts>" + parts + "</parts><placements>" + placements + "</placements>");
	};
	const auto drum = [](const std::string& name) {
		return "<part name=\"" + name + "\"><footprint><round radius=\"0.2\" sides=\"8\"/></footprint><height bottom=\"0\" top=\"0.5\"/>"
			   "<top color=\"#000000\"/><sides color=\"#000000\"/></part>";
	};
	pair("Inside", block("slab", 0.0F, 0.0F, 1.0F, 1.0F, 0.0F, 0.5F) + block("box", 0.4F, 0.4F, 0.5F, 0.5F, 0.0F, 0.5F), place("slab") + place("box"));
	// An 8-sided round of radius 0.2 has flat ends left and right at its inradius, 0.2 cos(22.5 degrees).
	const float inradius = 0.2F * std::cos(3.14159265F / 8.0F);
	pair("DrumIn", drum("drum") + block("slab", 0.0F, -0.5F, 1.0F, 0.5F, 0.0F, 0.5F), place("drum", inradius - 0.01F, 0.0F) + place("slab"));
	pair("DrumFlush", drum("drum") + block("slab", 0.0F, -0.5F, 1.0F, 0.5F, 0.0F, 0.5F), place("drum", -inradius, 0.0F) + place("slab"));
	load("Inside");
	EXPECT_TRUE(hasIssue("Inside", "overlap in plan and in height"));
	EXPECT_TRUE(hasIssue("DrumIn", "overlap in plan and in height"));
	EXPECT_FALSE(hasIssue("DrumFlush", "overlap in plan"));
}

// ─── The art check (rule 6) and area fill rules ─────────────────────────────────────────────────────────

TEST_F(PartsAssetTest, Rule6TheEngineArtCheckRefusesWhatLoadSvgDrops) {
	const std::string open = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">";
	const std::string square = "<rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"#808080\"/>";
	const std::vector<std::pair<std::string, std::string>> kinds = {
		{"use", open + "<rect id=\"r\" width=\"100\" height=\"100\" fill=\"#000\"/><use href=\"#r\"/></svg>"},
		{"text", open + square + "<text x=\"1\" y=\"10\">hi</text></svg>"},
		{"image", open + square + "<image href=\"x.png\" width=\"10\" height=\"10\"/></svg>"},
		{"filter", open + "<rect width=\"100\" height=\"100\" fill=\"#000\" filter=\"url(#f)\"/></svg>"},
		{"mask", open + "<rect width=\"100\" height=\"100\" fill=\"#000\" mask=\"url(#m)\"/></svg>"},
		{"clip", open + "<rect width=\"100\" height=\"100\" fill=\"#000\" clip-path=\"url(#c)\"/></svg>"},
		{"pattern", open + "<defs><pattern id=\"p\" width=\"10\" height=\"10\"/></defs><rect width=\"100\" height=\"100\" fill=\"url(#p)\"/></svg>"},
		{"dangling", open + "<rect width=\"100\" height=\"100\" fill=\"url(#nothing)\"/></svg>"},
		{"nothing", open + "<g/></svg>"},
	};
	std::string partsXml;
	std::string placements;
	float		x = 0.0F;
	for (const auto& [kind, text] : kinds) {
		folder.write("Art/" + kind + ".svg", text);
		partsXml += "<part name=\"" + kind + "\"><footprint>" + rect(0, 0, 1, 1) + "</footprint><height bottom=\"0\" top=\"1\"/><top drawing=\"" +
					kind + ".svg\"/><sides color=\"#000000\"/></part>";
		placements += place(kind, x);
		x += 2.0F;
	}
	writeDef("Art", "<parts>" + partsXml + "</parts><placements>" + placements + "</placements>");
	load("Art");
	EXPECT_TRUE(hasIssue("Art", "<use> is not drawn by loadSVG"));
	EXPECT_TRUE(hasIssue("Art", "<text> is not drawn by loadSVG"));
	EXPECT_TRUE(hasIssue("Art", "<image> is not drawn by loadSVG"));
	EXPECT_TRUE(hasIssue("Art", "has a filter"));
	EXPECT_TRUE(hasIssue("Art", "has a mask"));
	EXPECT_TRUE(hasIssue("Art", "has a clip-path"));
	EXPECT_TRUE(hasIssue("Art", "pattern.svg:1: <rect> paints with url(#p), which names no gradient"));
	EXPECT_TRUE(hasIssue("Art", "dangling.svg:1: <rect> paints with url(#nothing), which names no gradient"));
	EXPECT_TRUE(hasIssue("Art", "nothing.svg: draws nothing"));
}

TEST_F(PartsAssetTest, Rule6RefusesSharedAndAbsoluteDrawingPaths) {
	writeDef(
		"Paths",
		"<parts><part name=\"a\"><footprint>" + rect(0, 0, 1, 1) + "</footprint><height bottom=\"0\" top=\"1\"/><top drawing=\"@shared/top.svg\"/>"
		"<sides color=\"#000000\" drawing=\"/elsewhere/side.svg\"/></part></parts><placements>" + place("a") + "</placements>"
	);
	load("Paths");
	EXPECT_TRUE(hasIssue("Paths", "'@shared/top.svg' is outside the item's folder"));
	EXPECT_TRUE(hasIssue("Paths", "'/elsewhere/side.svg' is outside the item's folder"));
}

TEST_F(PartsAssetTest, AnAreaDrawingFilledEvenOddIsRefused) {
	const std::string body = "<parts>" + block("box", -0.5F, -0.5F, 0.5F, 0.5F, 0, 1) + "</parts><placements>" + place("box") + "</placements>";
	const std::string ring = "d=\"M0 0 H100 V100 H0 Z M25 25 H75 V75 H25 Z\"";
	const std::string open = "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\">";
	// On the shape, on a group round it, and in an inline style.
	const std::vector<std::pair<std::string, std::string>> drawings = {
		{"OnPath", open + "<path fill=\"#000\" fill-rule=\"evenodd\" " + ring + "/></svg>"},
		{"OnGroup", open + "<g fill-rule=\"evenodd\"><path fill=\"#000\" " + ring + "/></g></svg>"},
		{"InStyle", open + "<path style=\"fill: #000; fill-rule: evenodd\" " + ring + "/></svg>"},
	};
	for (const auto& [name, text] : drawings) {
		folder.write(name + "/ring.svg", text);
		writeDef(name, body + "<useAreas><drawing file=\"ring.svg\" minX=\"-1\" minY=\"0.5\" maxX=\"1\" maxY=\"2.5\"/></useAreas>");
	}
	load("OnPath");
	for (const auto& [name, text] : drawings) {
		EXPECT_TRUE(hasIssue(name, "fill-rule evenodd")) << name;
	}
}

// ─── Drawings mapped into metres ────────────────────────────────────────────────────────────────────────

TEST_F(PartsAssetTest, StrokesGradientsAndDetailLevelsCarryIntoMetresOnASide) {
	folder.write(
		"Crate/front.svg",
		"<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 200 100\"><defs><linearGradient id=\"shade\" gradientUnits=\"userSpaceOnUse\" "
		"x1=\"0\" y1=\"0\" x2=\"200\" y2=\"0\"><stop offset=\"0\" stop-color=\"#000000\"/><stop offset=\"1\" stop-color=\"#ffffff\"/></linearGradient></defs>"
		"<rect x=\"0\" y=\"0\" width=\"200\" height=\"100\" fill=\"url(#shade)\"/>"
		"<g data-lod=\"40\"><rect x=\"20\" y=\"20\" width=\"60\" height=\"60\" fill=\"none\" stroke=\"#000\" stroke-width=\"10\"/></g></svg>"
	);
	writeDef(
		"Crate",
		"<parts><part name=\"box\"><footprint>" + rect(-1, -0.25F, 1, 0.25F) + "</footprint><height bottom=\"0\" top=\"1\"/><top color=\"#000000\"/>"
		"<sides color=\"#000000\"><face side=\"front\" drawing=\"front.svg\"/></sides></part></parts><placements>" + place("box") + "</placements>"
	);
	const AssetDefinition* def = load("Crate");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(issues("Crate").empty()) << ::testing::PrintToString(issues("Crate"));
	const PartFace& front = def->parts->parts[0].faces[2];
	ASSERT_TRUE(front.drawing.has_value());
	const ItemDrawing& drawing = def->parts->drawings[*front.drawing];
	ASSERT_EQ(drawing.shapes.size(), 2U);
	EXPECT_FLOAT_EQ(drawing.shapes[0].minPixelsPerMeter, 0.0F);
	EXPECT_FLOAT_EQ(drawing.shapes[1].minPixelsPerMeter, 40.0F) << "the detail-level pass on a side drawing";
	EXPECT_NEAR(drawing.shapes[1].shape.strokeWidth, 0.1F, 1e-5F) << "10 units at 100 units a metre";
	// The gradient runs across the 2 m face: a quarter of the way along, a quarter grey.
	const renderer::GradientFill& gradient = drawing.shapes[0].shape.gradient;
	ASSERT_NE(gradient.type, renderer::GradientFill::Type::None);
	EXPECT_NEAR(gradient.colorAt({0.5F, 0.5F}).r, 0.25F, 0.02F);
	EXPECT_NEAR(gradient.colorAt({1.5F, 0.2F}).r, 0.75F, 0.02F);
}

TEST_F(PartsAssetTest, Rule7APairJustInsideTheToleranceLoads) {
	folder.write("Close/front.svg", svg(1.005F, 1.0F));
	writeDef(
		"Close",
		"<parts><part name=\"a\"><footprint>" + rect(0, 0, 1, 1) + "</footprint><height bottom=\"0\" top=\"1\"/><top color=\"#000000\"/>"
		"<sides color=\"#000000\"><face side=\"front\" drawing=\"front.svg\"/></sides></part></parts><placements>" + place("a") + "</placements>"
	);
	load("Close");
	EXPECT_TRUE(issues("Close").empty()) << ::testing::PrintToString(issues("Close"));
}

// ─── The parser's and the script's checks ──────────────────────────────────────────────────────────────

TEST_F(PartsAssetTest, AClearanceMarginOfZeroIsNoClearance) {
	writeDef("Zero", "<parts>" + block("box", -0.5F, -0.5F, 0.5F, 0.5F, 0, 1) + "</parts><placements>" + place("box") + "</placements><clearance marginMeters=\"0\"/>");
	const AssetDefinition* def = load("Zero");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(def->parts->clearance.polygons.empty());
}

TEST_F(PartsAssetTest, AreasAndFacesNeedWhatTheSchemaRequires) {
	const std::string body = "<parts>" + block("box", -0.5F, -0.5F, 0.5F, 0.5F, 0, 1) + "</parts><placements>" + place("box") + "</placements>";
	writeDef("NoAreas", body + "<useAreas/>");
	writeDef("Nameless", body + "<useAreas><area>" + rect(-1, 0.5F, 1, 1.5F) + "</area></useAreas>");
	writeDef(
		"Twins",
		body + "<useAreas><area name=\"front\">" + rect(-1, 0.5F, 1, 1.5F) + "</area><area name=\"front\">" + rect(-1, -1.5F, 1, -0.5F) + "</area></useAreas>"
	);
	writeDef(
		"BareFace",
		"<parts><part name=\"a\"><footprint>" + rect(0, 0, 1, 1) + "</footprint><height bottom=\"0\" top=\"1\"/><top color=\"#000000\"/>"
		"<sides color=\"#000000\"><face drawing=\"x.svg\"/><face side=\"front\"/></sides></part></parts><placements>" + place("a") + "</placements>"
	);
	folder.write("Mixed/parts.lua", "asset:addUseArea{ shapes = { { circle = { x = 0, y = -1.5, radius = 0.6 } } } }\n");
	writeDef("Mixed", body + "<partsScript>parts.lua</partsScript><useAreas><area name=\"front\">" + rect(-1, 0.5F, 1, 1.5F) + "</area></useAreas>");
	load("NoAreas");
	EXPECT_TRUE(hasIssue("NoAreas", "holds no area"));
	EXPECT_TRUE(hasIssue("Nameless", "an <area> needs a name"));
	EXPECT_TRUE(hasIssue("Twins", "two use areas are named 'front'"));
	EXPECT_TRUE(hasIssue("BareFace", "names no side"));
	EXPECT_TRUE(hasIssue("BareFace", "a <face> with no drawing"));
	EXPECT_TRUE(hasIssue("Mixed", "not both")) << "a script's areas pass the XML's checks";
}

TEST_F(PartsAssetTest, APartsScriptAddsAreasAndClearanceAndReshapesAPart) {
	folder.write(
		"Bench/parts.lua",
		"asset:setFootprint('top', { rect = { minX = -1, minY = -0.4, maxX = 1, maxY = 0.4 } })\n"
		"asset:addUseArea{ name = 'front', shapes = { { rect = { minX = -1, minY = 0.4, maxX = 1, maxY = 1.2 } } } }\n"
		"asset:addUseArea{ name = 'back', shapes = { { circle = { x = 0, y = -1.2, radius = 0.6 } } } }\n"
		"asset:addClearance{ marginMeters = 0.1 }\n"
	);
	writeDef("Bench", "<partsScript>parts.lua</partsScript><parts>" + block("top", -0.5F, -0.5F, 0.5F, 0.5F, 0, 0.8F) + "</parts><placements>" + place("top") + "</placements>");
	const AssetDefinition* def = load("Bench");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(issues("Bench").empty()) << ::testing::PrintToString(issues("Bench"));
	EXPECT_NEAR(area(def->partsColliders[0].pointsMeters), 1.6, 1e-4) << "reshaped to 2 m x 0.8 m";
	ASSERT_EQ(def->parts->useAreas.size(), 2U);
	EXPECT_EQ(def->parts->useAreas[1].name, "back");
	EXPECT_GT(area(def->parts->clearance), 1.6);
}

TEST_F(PartsAssetTest, APartsScriptsErrorsAreReported) {
	const std::string body = "<parts>" + block("box", -0.5F, -0.5F, 0.5F, 0.5F, 0, 1) + "</parts><placements>" + place("box") + "</placements>";
	folder.write("Broken/parts.lua", "asset:place{ part = 'box' \n");
	writeDef("Broken", "<partsScript>parts.lua</partsScript>" + body);
	folder.write("Typo/parts.lua", "asset:place{ prat = 'box' }\n");
	writeDef("Typo", "<partsScript>parts.lua</partsScript>" + body);
	load("Broken");
	EXPECT_FALSE(issues("Broken").empty()) << "a syntax error is an error";
	EXPECT_TRUE(hasIssue("Typo", "unknown key 'prat' in place"));
}

TEST_F(PartsAssetTest, APartsScriptsRandomNumbersAreTheSameEveryLoad) {
	folder.write(
		"Random/parts.lua",
		"for i = 1, 3 do\n"
		"  asset:place{ part = 'box', x = i * 2 + math.random(), y = math.random() }\n"
		"end\n"
	);
	writeDef("Random", "<partsScript>parts.lua</partsScript><parts>" + block("box", -0.5F, -0.5F, 0.5F, 0.5F, 0, 1) + "</parts>");
	const AssetDefinition* first = load("Random");
	ASSERT_NE(first, nullptr);
	const std::vector<PartPlacement> firstPlacements = first->parts->placements;
	const AssetDefinition*			 second = load("Random");
	ASSERT_NE(second, nullptr);
	ASSERT_EQ(second->parts->placements.size(), firstPlacements.size());
	for (size_t i = 0; i < firstPlacements.size(); ++i) {
		EXPECT_EQ(second->parts->placements[i].offsetMeters, firstPlacements[i].offsetMeters) << "placement " << i;
	}
}
