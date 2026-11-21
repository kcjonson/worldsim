// Tests for item views (rotatable-placed-items.md, sections 3, 6, 7, and 20): the turn quantum, the face matrix and
// which faces show, shading, constant line width, narrow faces, the face clip, the parts order, depth strips and
// their layout, detail levels, the turn-0 template, and validation rule 12. Fixtures are written to a temp folder.

#include "assets/DetailLevels.h"
#include "assets/ItemView.h"
#include "assets/PartsAsset.test.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <string>
#include <vector>

using namespace assets;
using namespace assets::parts_fixtures;

namespace {

	constexpr float kTolerance = 1.0e-4F;


	const ViewConfig& viewConfig() {
		return GameViewConfig::Get().config();
	}

	uint32_t quantaOf(double degrees) {
		return turnQuantaFromDegrees(degrees);
	}

	float radiansOf(double degrees) {
		return turnRadiansFromQuanta(quantaOf(degrees));
	}

	const char* rectFaceName(uint32_t face) {
		static constexpr std::array<const char*, 4> kNames = {"back", "right", "front", "left"};
		return kNames[face];
	}

	std::set<std::string> faceNames(const std::vector<VisibleFace>& faces, uint32_t placement) {
		std::set<std::string> names;
		for (const VisibleFace& face : faces) {
			if (face.placement == placement) {
				names.insert(rectFaceName(face.face));
			}
		}
		return names;
	}

	std::optional<VisibleFace> findFace(const std::vector<VisibleFace>& faces, uint32_t placement, uint32_t face) {
		for (const VisibleFace& visible : faces) {
			if (visible.placement == placement && visible.face == face) {
				return visible;
			}
		}
		return std::nullopt;
	}

	glm::vec2 turnedPoint(glm::vec2 point, float radians) {
		return {point.x * std::cos(radians) - point.y * std::sin(radians), point.x * std::sin(radians) + point.y * std::cos(radians)};
	}

	// Whether a point lies in a convex quad, wound either way, within kTolerance.
	bool insideQuad(glm::vec2 point, const std::array<glm::vec2, 4>& quad) {
		float doubledArea = 0.0F;
		for (size_t i = 0; i < 4; ++i) {
			doubledArea += quad[i].x * quad[(i + 1) % 4].y - quad[(i + 1) % 4].x * quad[i].y;
		}
		for (size_t i = 0; i < 4; ++i) {
			const glm::vec2 along = quad[(i + 1) % 4] - quad[i];
			const glm::vec2 inward = glm::vec2{-along.y, along.x} * (doubledArea >= 0.0F ? 1.0F : -1.0F) / std::hypot(along.x, along.y);
			if ((point.x - quad[i].x) * inward.x + (point.y - quad[i].y) * inward.y < -kTolerance) {
				return false;
			}
		}
		return true;
	}

	std::array<glm::vec2, 4> faceQuad(const VisibleFace& face, float length, float height) {
		return {
			face.matrix.applyToPoint({0.0F, 0.0F}),
			face.matrix.applyToPoint({length, 0.0F}),
			face.matrix.applyToPoint({length, height}),
			face.matrix.applyToPoint({0.0F, height}),
		};
	}

	bool isRed(const Foundation::Color& color) {
		return color.r > 0.5F && color.g == 0.0F && color.b == 0.0F;
	}

	class ItemViewTest : public PartsTest {
	  protected:
		// One rect part, 1.0 x 0.6 m, standing from 0.2 to 0.8 m.
		const AssetDefinition* loadBox() {
			writeDef("Box", "<parts>" + block("box", -0.5F, -0.3F, 0.5F, 0.3F, 0.2F, 0.8F) + "</parts><placements>" + place("box") + "</placements>");
			return load("Box");
		}

		// The stool of section 4.4: four legs under a 0.5 m seat.
		const AssetDefinition* loadStool() {
			writeDef(
				"Stool",
				"<parts>" + block("leg", -0.035F, -0.035F, 0.035F, 0.035F, 0.0F, 0.43F) + block("seat", -0.25F, -0.25F, 0.25F, 0.25F, 0.43F, 0.5F) +
					"</parts><placements>" + place("leg", -0.19F, -0.19F) + place("leg", 0.19F, -0.19F) + place("leg", 0.19F, 0.19F) +
					place("leg", -0.19F, 0.19F) + place("seat") + "</placements>"
			);
			return load("Stool");
		}

		// A plain bench, `length` by `depth`, 0.8 m tall, and optionally a mat on the ground under it.
		const AssetDefinition* loadBench(float length, float depth, bool mat = false) {
			const std::string parts =
				block("bench", -length / 2, -depth / 2, length / 2, depth / 2, 0.0F, 0.8F) + (mat ? block("mat", -2.0F, -1.0F, 2.0F, 1.0F, 0.0F, 0.0F) : "");
			writeDef("Bench", "<parts>" + parts + "</parts><placements>" + place("bench") + (mat ? place("mat") : "") + "</placements>");
			return load("Bench");
		}

		// An item of plain blocks, each `name minX minY maxX maxY`, 0 to 0.9 m tall.
		const AssetDefinition* loadBlocks(const std::string& name, const std::vector<std::array<float, 4>>& rects) {
			std::string parts;
			std::string placements;
			for (size_t i = 0; i < rects.size(); ++i) {
				const std::string part = "block" + std::to_string(i);
				parts += block(part, rects[i][0], rects[i][1], rects[i][2], rects[i][3], 0.0F, 0.9F);
				placements += place(part);
			}
			writeDef(name, "<parts>" + parts + "</parts><placements>" + placements + "</placements>");
			return load(name);
		}

		// The strips' index ranges follow one another and together cover every index once.
		static void expectContiguous(const ItemView& view) {
			uint32_t next = 0;
			for (const ViewStrip& strip : view.strips) {
				EXPECT_EQ(strip.firstIndex, next);
				next += strip.indexCount;
			}
			EXPECT_EQ(next, view.mesh.indices.size());
		}

		static void expectAscending(const std::vector<ItemStrip>& strips) {
			for (size_t i = 1; i < strips.size(); ++i) {
				EXPECT_LE(strips[i - 1].anchorLocalY, strips[i].anchorLocalY) << "strip " << i;
			}
		}
	};

} // namespace

// ─── Turns ──────────────────────────────────────────────────────────────────────────────────────────────

TEST(TurnQuantum, RoundsThenWrapsAtTheWholeTurn) {
	EXPECT_EQ(turnQuantaFromDegrees(0.0), 0U);
	EXPECT_EQ(turnQuantaFromDegrees(0.004), 0U);
	EXPECT_EQ(turnQuantaFromDegrees(360.004), 0U) << "360.004 and 0.004 share a view";
	EXPECT_EQ(turnQuantaFromDegrees(359.996), 0U) << "rounded to 36000 first, then wrapped: never 36000";
	EXPECT_EQ(turnQuantaFromDegrees(359.994), 35999U);
	EXPECT_EQ(turnQuantaFromDegrees(-0.004), 0U);
	EXPECT_EQ(turnQuantaFromDegrees(-0.006), 35999U);
	EXPECT_EQ(turnQuantaFromDegrees(35.0), 3500U);
	EXPECT_EQ(turnQuantaFromDegrees(720.01), 1U);
	EXPECT_NEAR(turnRadiansFromQuanta(9000), std::numbers::pi_v<float> / 2.0F, 1.0e-6F);
	EXPECT_NEAR(turnRadiansFromQuanta(35999), 359.99F * std::numbers::pi_v<float> / 180.0F, 1.0e-5F);
}

// ─── Faces ──────────────────────────────────────────────────────────────────────────────────────────────

TEST_F(ItemViewTest, TheFaceMatrixMapsADrawingsCornersOntoTheFaceAndTheVisibleSetFollowsTheTurn) {
	const AssetDefinition* def = loadBox();
	ASSERT_NE(def, nullptr);
	ASSERT_TRUE(issues("Box").empty()) << ::testing::PrintToString(issues("Box"));
	const PartsAsset& parts = *def->parts;
	const float		  ratio = viewConfig().heightRatio;
	const std::map<int, std::set<std::string>> expected = {
		{0, {"front"}}, {35, {"front", "right"}}, {90, {"right"}}, {180, {"back"}}, {271, {"front", "left"}}, {325, {"front", "left"}},
	};
	for (const auto& [degrees, names] : expected) {
		const std::vector<VisibleFace> faces = visibleFaces(parts, radiansOf(degrees), viewConfig());
		EXPECT_EQ(faceNames(faces, 0), names) << degrees << " degrees";
		for (const VisibleFace& face : faces) {
			const float length = parts.parts[0].faces[face.face].lengthMeters;
			const float height = 0.6F;
			const auto	expectNear = [&](glm::vec2 actual, glm::vec2 wanted, const char* what) {
				 EXPECT_NEAR(actual.x, wanted.x, 1.0e-5F) << degrees << " degrees, " << rectFaceName(face.face) << ", " << what;
				 EXPECT_NEAR(actual.y, wanted.y, 1.0e-5F) << degrees << " degrees, " << rectFaceName(face.face) << ", " << what;
			};
			// Top corners at the part's top; the drawing's bottom edge is the ground endpoints raised to `bottom`.
			expectNear(face.matrix.applyToPoint({0.0F, 0.0F}), face.left + glm::vec2{0.0F, -0.8F * ratio}, "top left");
			expectNear(face.matrix.applyToPoint({length, 0.0F}), face.right + glm::vec2{0.0F, -0.8F * ratio}, "top right");
			expectNear(face.matrix.applyToPoint({length, height}), face.right + glm::vec2{0.0F, -0.2F * ratio}, "bottom right");
			expectNear(face.matrix.applyToPoint({0.0F, height}), face.left + glm::vec2{0.0F, -0.2F * ratio}, "bottom left");
			// A point inside lands where section 6.3's formula says.
			const glm::vec2 along = (face.right - face.left) / length;
			expectNear(face.matrix.applyToPoint({0.3F, 0.1F}), face.left + 0.3F * along + glm::vec2{0.0F, -0.8F * ratio + 0.1F * ratio}, "(0.3, 0.1)");
		}
	}
	// At turn 0 the front's left end is the front-left corner, named by the winding.
	const std::optional<VisibleFace> front = findFace(visibleFaces(parts, 0.0F, viewConfig()), 0, 2);
	ASSERT_TRUE(front.has_value());
	EXPECT_FLOAT_EQ(front->left.x, -0.5F);
	EXPECT_FLOAT_EQ(front->left.y, 0.3F);
	EXPECT_FLOAT_EQ(front->right.x, 0.5F);
}

TEST_F(ItemViewTest, NoFaceIsEverMappedMirrored) {
	writeDef(
		"Shapes",
		"<parts>" + block("box", -0.5F, -0.3F, 0.5F, 0.3F, 0.0F, 0.8F) +
			"<part name=\"drum\"><footprint><round radius=\"0.3\" sides=\"7\"/></footprint><height bottom=\"0\" top=\"0.5\"/>"
			"<top color=\"#000000\"/><sides color=\"#000000\"/></part></parts><placements>" +
			place("box") + place("drum", 1.5F, 0.0F, 13.0F) + "</placements>"
	);
	const AssetDefinition* def = load("Shapes");
	ASSERT_NE(def, nullptr);
	size_t checked = 0;
	for (uint32_t turn = 0; turn < kTurnQuantaPerTurn; turn += 7) {
		for (const VisibleFace& face : visibleFaces(*def->parts, turnRadiansFromQuanta(turn), viewConfig())) {
			ASSERT_GT(face.matrix.determinant(), 0.0F) << "turn " << turn;
			ASSERT_GT(face.right.x, face.left.x) << "turn " << turn;
			++checked;
		}
	}
	EXPECT_GT(checked, 20000U);
}

// ─── Shading ────────────────────────────────────────────────────────────────────────────────────────────

TEST_F(ItemViewTest, ShadingFollowsTheOneLightFromTheUpperLeft) {
	const ViewConfig& config = viewConfig();
	EXPECT_NEAR(sideShade({-1.0F, 0.0F}, config), 0.985F, 5.0e-4F) << "looking left, the brightest";
	EXPECT_NEAR(sideShade({0.0F, 1.0F}, config), 0.66F, 1.0e-6F) << "looking straight down: ambient";
	EXPECT_NEAR(sideShade({-std::numbers::sqrt2_v<float> / 2, std::numbers::sqrt2_v<float> / 2}, config), 0.66F, 1.0e-5F) << "down and left";
	EXPECT_NEAR(sideShade({std::numbers::sqrt2_v<float> / 2, std::numbers::sqrt2_v<float> / 2}, config), 0.66F, 1.0e-6F) << "down and right";

	// The four rect faces at turns 0, 45, and 325.
	const std::array<glm::vec2, 4> normals = {glm::vec2{0, -1}, glm::vec2{1, 0}, glm::vec2{0, 1}, glm::vec2{-1, 0}};
	const std::map<int, std::array<float, 4>> expected = {
		{0, {0.985F, 0.66F, 0.66F, 0.985F}},
		{45, {0.66F, 0.66F, 0.66F, 0.66F + 0.6F * 0.766044F}},
		{325, {0.66F + 0.6F * 0.984808F * 0.766044F, 0.66F, 0.66F, 0.740F}},
	};
	for (const auto& [degrees, shades] : expected) {
		for (size_t face = 0; face < 4; ++face) {
			EXPECT_NEAR(sideShade(turnedPoint(normals[face], radiansOf(degrees)), config), shades[face], 1.0e-3F) << degrees << " degrees, " << rectFaceName(static_cast<uint32_t>(face));
		}
	}

	// Wrap lets the light reach round: a face looking straight down at wrap 1 is 0.727; down and right stays 0.66.
	ViewConfig wrapped = config;
	wrapped.wrap = 1.0F;
	EXPECT_NEAR(sideShade({0.0F, 1.0F}, wrapped), 0.727F, 5.0e-4F);
	EXPECT_NEAR(sideShade({std::numbers::sqrt2_v<float> / 2, std::numbers::sqrt2_v<float> / 2}, wrapped), 0.66F, 1.0e-5F);
}

TEST_F(ItemViewTest, AFacesShadeIsContinuousAsItTurnsAndAppears) {
	const AssetDefinition* def = loadBox();
	ASSERT_NE(def, nullptr);
	std::array<float, 4> previous{};
	std::array<bool, 4>	 wasVisible{};
	float				 largestStep = 0.0F;
	for (uint32_t turn = 0; turn <= kTurnQuantaPerTurn; ++turn) {
		std::array<bool, 4> visible{};
		for (const VisibleFace& face : visibleFaces(*def->parts, turnRadiansFromQuanta(turn % kTurnQuantaPerTurn), viewConfig())) {
			visible[face.face] = true;
			if (wasVisible[face.face]) {
				largestStep = std::max(largestStep, std::abs(face.shade - previous[face.face]));
			}
			previous[face.face] = face.shade;
		}
		wasVisible = visible;
	}
	EXPECT_LT(largestStep, 2.0e-4F) << "a quantum's turn moves a shade by no more than its share of the light";
	// The left end appears at 180 degrees facing right, at ambient, and leaves at 360 facing left, at the brightest.
	const std::optional<VisibleFace> appearing = findFace(visibleFaces(*def->parts, turnRadiansFromQuanta(18001), viewConfig()), 0, 3);
	const std::optional<VisibleFace> leaving = findFace(visibleFaces(*def->parts, turnRadiansFromQuanta(35999), viewConfig()), 0, 3);
	ASSERT_TRUE(appearing.has_value());
	ASSERT_TRUE(leaving.has_value());
	EXPECT_NEAR(appearing->shade, 0.66F, 1.0e-3F);
	EXPECT_NEAR(leaving->shade, 0.985F, 1.0e-3F);
}

TEST_F(ItemViewTest, TheWorkbenchAt325ShadesItsLeftEndAndItsFront) {
	writeWorkbench();
	const AssetDefinition* def = load("Workbench");
	ASSERT_NE(def, nullptr);
	const std::vector<VisibleFace> faces = visibleFaces(*def->parts, radiansOf(325.0), viewConfig());
	const std::optional<VisibleFace> left = findFace(faces, 0, 3);
	const std::optional<VisibleFace> front = findFace(faces, 0, 2);
	ASSERT_TRUE(left.has_value());
	ASSERT_TRUE(front.has_value());
	EXPECT_NEAR(left->shade, 0.740F, 5.0e-4F);
	EXPECT_NEAR(front->shade, 0.66F, 1.0e-6F);
}

// ─── Lines, narrow faces, and the face clip ─────────────────────────────────────────────────────────────

TEST_F(ItemViewTest, AStrokeOnASkewedFaceKeepsItsWidth) {
	// A 2 m x 1 m front with one horizontal seam, 4 units (0.04 m) wide.
	folder.write("Seam/front.svg", bareSvg(2.0F, 1.0F, "<path d=\"M 20 50 L 180 50\" stroke=\"#000000\" stroke-width=\"4\" fill=\"none\"/>"));
	writeDef(
		"Seam",
		"<parts><part name=\"panel\"><footprint>" + rect(-1.0F, -0.25F, 1.0F, 0.25F) +
			"</footprint><height bottom=\"0\" top=\"1\"/><top color=\"#9A6A3F\"/><sides color=\"#9A6A3F\"><face side=\"front\" drawing=\"front.svg\"/></sides>"
			"</part></parts><placements>" + place("panel") + "</placements>"
	);
	const AssetDefinition* def = load("Seam");
	ASSERT_NE(def, nullptr);
	for (const double degrees : {0.0, 30.0, 60.0}) {
		const ItemView view = buildItemView(*def, quantaOf(degrees), 0, viewConfig());
		ASSERT_TRUE(view.error.empty()) << view.error;
		const std::optional<VisibleFace> front = findFace(visibleFaces(*def->parts, radiansOf(degrees), viewConfig()), 0, 2);
		ASSERT_TRUE(front.has_value());
		const glm::vec2 from = front->matrix.applyToPoint({0.2F, 0.5F});
		const glm::vec2 to = front->matrix.applyToPoint({1.8F, 0.5F});
		const glm::vec2 across = glm::vec2{-(to - from).y, (to - from).x} / std::hypot((to - from).x, (to - from).y);
		float			low = 1.0e9F;
		float			high = -1.0e9F;
		size_t			band = 0;
		for (size_t i = 0; i < view.mesh.vertices.size(); ++i) {
			const Foundation::Color& color = view.mesh.colors[i];
			if (color.r != 0.0F || color.g != 0.0F || color.b != 0.0F) {
				continue;
			}
			const float distance = (view.mesh.vertices[i].x - from.x) * across.x + (view.mesh.vertices[i].y - from.y) * across.y;
			low = std::min(low, distance);
			high = std::max(high, distance);
			++band;
		}
		ASSERT_GT(band, 0U) << degrees << " degrees";
		EXPECT_NEAR(high - low, 0.04F, 0.0004F) << degrees << " degrees: the band's perpendicular thickness on screen";
	}
}

TEST_F(ItemViewTest, AFaceNarrowerThanTheMinimumWidthDrawsItsPlainColour) {
	writeWorkbench();
	const AssetDefinition* workbench = load("Workbench");
	ASSERT_NE(workbench, nullptr);
	const std::optional<VisibleFace> wide = findFace(visibleFaces(*workbench->parts, radiansOf(271.0), viewConfig()), 0, 2);
	const std::optional<VisibleFace> narrow = findFace(visibleFaces(*workbench->parts, radiansOf(270.5), viewConfig()), 0, 2);
	ASSERT_TRUE(wide.has_value());
	ASSERT_TRUE(narrow.has_value());
	EXPECT_NEAR(std::abs(wide->right.x - wide->left.x), 0.050F, 5.0e-4F);
	EXPECT_TRUE(wide->drawsArt) << "the carcass front at 271 degrees, 0.050 m wide, carries its drawing";
	EXPECT_NEAR(std::abs(narrow->right.x - narrow->left.x), 0.025F, 5.0e-4F);
	EXPECT_FALSE(narrow->drawsArt) << "at 270.5 degrees, 0.025 m wide, it is plain";

	// A red front on a green box: seen nearly edge on (88 degrees, 0.035 m wide) no red is drawn, only green.
	folder.write("Narrow/front.svg", bareSvg(1.0F, 0.6F, "<rect x=\"0\" y=\"0\" width=\"100\" height=\"60\" fill=\"#FF0000\"/>"));
	writeDef(
		"Narrow",
		"<parts><part name=\"box\"><footprint>" + rect(-0.5F, -0.3F, 0.5F, 0.3F) +
			"</footprint><height bottom=\"0\" top=\"0.6\"/><top color=\"#0000FF\"/><sides color=\"#00FF00\"><face side=\"front\" drawing=\"front.svg\"/></sides>"
			"</part></parts><placements>" + place("box") + "</placements>"
	);
	const AssetDefinition* def = load("Narrow");
	ASSERT_NE(def, nullptr);
	const auto count = [&](double degrees, auto predicate) {
		const ItemView view = buildItemView(*def, quantaOf(degrees), 0, viewConfig());
		return std::count_if(view.mesh.colors.begin(), view.mesh.colors.end(), predicate);
	};
	const auto green = [](const Foundation::Color& color) { return color.g > 0.5F && color.r == 0.0F && color.b == 0.0F; };
	EXPECT_EQ(count(88.0, isRed), 0);
	EXPECT_GT(count(88.0, green), 0);
	EXPECT_GT(count(80.0, isRed), 0) << "at 80 degrees (0.17 m wide) the drawing shows";
}

TEST_F(ItemViewTest, FaceArtNeverLeavesItsParallelogram) {
	// A stroke twice as wide as the face, running past the top and bottom of the viewBox.
	folder.write("Spill/front.svg", bareSvg(0.3F, 0.5F, "<path d=\"M 15 -20 L 15 70\" stroke=\"#FF0000\" stroke-width=\"60\" fill=\"none\"/>"));
	writeDef(
		"Spill",
		"<parts><part name=\"post\"><footprint>" + rect(-0.15F, -0.15F, 0.15F, 0.15F) +
			"</footprint><height bottom=\"0\" top=\"0.5\"/><top color=\"#0000FF\"/><sides color=\"#00FF00\"><face side=\"front\" drawing=\"front.svg\"/></sides>"
			"</part></parts><placements>" + place("post") + "</placements>"
	);
	const AssetDefinition* def = load("Spill");
	ASSERT_NE(def, nullptr);
	EXPECT_TRUE(hasIssue("Spill", "draws past its viewBox", Severity::Warning));
	for (const double degrees : {0.0, 20.0, 60.0, 330.0}) {
		const ItemView	   view = buildItemView(*def, quantaOf(degrees), 0, viewConfig());
		const std::optional<VisibleFace> front = findFace(visibleFaces(*def->parts, radiansOf(degrees), viewConfig()), 0, 2);
		ASSERT_TRUE(front.has_value());
		ASSERT_TRUE(front->drawsArt);
		const std::array<glm::vec2, 4> quad = faceQuad(*front, 0.3F, 0.5F);
		size_t						   red = 0;
		for (size_t i = 0; i < view.mesh.vertices.size(); ++i) {
			if (isRed(view.mesh.colors[i])) {
				++red;
				EXPECT_TRUE(insideQuad(view.mesh.vertices[i], quad)) << degrees << " degrees, vertex " << i;
			}
		}
		EXPECT_GT(red, 0U) << degrees << " degrees";
	}
}

// ─── The parts order ────────────────────────────────────────────────────────────────────────────────────

TEST_F(ItemViewTest, TheOrderFollowsTheProjectionNotHeightsOrCentroids) {
	// A low shelf behind two front posts draws before them, though it starts higher.
	writeDef(
		"Shelf",
		"<parts>" + block("post", -0.025F, -0.025F, 0.025F, 0.025F, 0.0F, 0.9F) + block("shelf", -0.45F, -0.3F, 0.45F, 0.25F, 0.15F, 0.17F) +
			"</parts><placements>" + place("post", -0.4F, 0.3F) + place("post", 0.4F, 0.3F) + place("shelf") + "</placements>"
	);
	// A 1.2 m x 0.1 m board at 45 degrees and a small box beside it, south of the board where they share vertical
	// lines though its centre is north of the board's: board first.
	writeDef(
		"Board",
		"<parts>" + block("box", -0.1F, -0.1F, 0.1F, 0.1F, 0.0F, 0.3F) + block("board", -0.6F, -0.05F, 0.6F, 0.05F, 0.5F, 0.55F) +
			"</parts><placements>" + place("box", -0.45F, -0.15F) + place("board", 0.0F, 0.0F, 45.0F) + "</placements>"
	);
	// A slab on four legs: the slab after every leg.
	writeDef(
		"Table",
		"<parts>" + block("leg", -0.03F, -0.03F, 0.03F, 0.03F, 0.0F, 0.7F) + block("slab", -0.5F, -0.3F, 0.5F, 0.3F, 0.7F, 0.75F) +
			"</parts><placements>" + place("slab") + place("leg", -0.45F, -0.25F) + place("leg", 0.45F, -0.25F) + place("leg", 0.45F, 0.25F) +
			place("leg", -0.45F, 0.25F) + "</placements>"
	);
	writeWorkbench();
	load("Workbench");
	for (const char* name : {"Shelf", "Board", "Table", "Workbench"}) {
		EXPECT_TRUE(issues(name).empty()) << name << ::testing::PrintToString(issues(name));
	}
	AssetRegistry& registry = AssetRegistry::Get();
	const auto	   orderOf = [&](const char* name, double degrees) { return partsDrawOrder(*registry.getDefinition(name)->parts, radiansOf(degrees)); };
	EXPECT_EQ(orderOf("Shelf", 0.0), (std::vector<uint32_t>{2, 0, 1}));
	EXPECT_EQ(orderOf("Board", 0.0), (std::vector<uint32_t>{1, 0})) << "the box is south of the board where they share verticals";
	for (const double degrees : {0.0, 45.0, 200.0}) {
		const std::vector<uint32_t> table = orderOf("Table", degrees);
		EXPECT_EQ(table.back(), 0U) << degrees << " degrees: the slab draws last";
		// Workbench: carcass (0), worktop (1), vise (2), toolbox (3); the vise and toolbox after both body parts.
		const std::vector<uint32_t> bench = orderOf("Workbench", degrees);
		const auto position = [&](uint32_t placement) { return std::find(bench.begin(), bench.end(), placement) - bench.begin(); };
		EXPECT_LT(position(0), position(1)) << degrees;
		EXPECT_LT(position(1), position(2)) << degrees;
		EXPECT_LT(position(1), position(3)) << degrees;
	}
}

// ─── Depth strips ───────────────────────────────────────────────────────────────────────────────────────

TEST_F(ItemViewTest, StripCountsAndAnchorsForTheStoolAndTheBench) {
	const AssetDefinition* stool = loadStool();
	ASSERT_NE(stool, nullptr);
	EXPECT_EQ(stripLayout(*stool, quantaOf(0.0), viewConfig()).strips.size(), 1U) << "a stool square to the screen";
	EXPECT_EQ(stripLayout(*stool, quantaOf(45.0), viewConfig()).strips.size(), 8U) << "two spans of 0.354 m at a slope of 1, four bands each";

	const AssetDefinition* bench = loadBench(3.0F, 1.0F);
	ASSERT_NE(bench, nullptr);
	EXPECT_EQ(stripLayout(*bench, quantaOf(0.0), viewConfig()).strips.size(), 1U) << "a 3 m bench square to the screen";
	const StripLayout turned = stripLayout(*bench, quantaOf(45.0), viewConfig());
	ASSERT_EQ(turned.strips.size(), 31U) << "spans of 0.707, 1.414, and 0.707 m in 8, 15, and 8 bands";
	expectAscending(turned.strips);
	// Each anchor is the southern edge's largest y across its band: the anchors fall along the front edge.
	std::vector<glm::vec2> footprint;
	for (const glm::vec2& corner : {glm::vec2{-1.5F, -0.5F}, glm::vec2{1.5F, -0.5F}, glm::vec2{1.5F, 0.5F}, glm::vec2{-1.5F, 0.5F}}) {
		footprint.push_back(turnedPoint(corner, radiansOf(45.0)));
	}
	const float pad = viewConfig().outlineHalfWidthMeters();
	const float reach = 2.0F * std::numbers::sqrt2_v<float> / 2.0F;
	for (const ItemStrip& strip : turned.strips) {
		const float from = std::max(strip.minX, -reach);
		const float to = std::min(strip.maxX, reach);
		const float southern = std::max(ySpanAt(footprint, from + 1.0e-5F).second, ySpanAt(footprint, to - 1.0e-5F).second);
		EXPECT_NEAR(strip.anchorLocalY, southern, 2.0e-3F) << "strip from " << strip.minX;
		EXPECT_FALSE(strip.flat);
	}
	EXPECT_NEAR(turned.strips.front().minX + pad, -reach, 5.0e-3F) << "the westmost strip has the lowest anchor";
}

TEST_F(ItemViewTest, ACounterAtAShallowAngleMergesItsBandsInPairs) {
	const AssetDefinition* counter = loadBench(6.0F, 0.6F);
	ASSERT_NE(counter, nullptr);
	const float		  turn = radiansOf(4.5);
	const StripLayout layout = stripLayout(*counter, quantaOf(4.5), viewConfig());
	std::vector<glm::vec2> footprint;
	std::vector<float>	   xs;
	for (const glm::vec2& corner : {glm::vec2{-3.0F, -0.3F}, glm::vec2{3.0F, -0.3F}, glm::vec2{3.0F, 0.3F}, glm::vec2{-3.0F, 0.3F}}) {
		footprint.push_back(turnedPoint(corner, turn));
		xs.push_back(footprint.back().x);
	}
	std::sort(xs.begin(), xs.end());
	const float span = xs[2] - xs[1];
	const float bandWidth = span / std::ceil(span / viewConfig().depthStripWidthMeters);
	const auto	southern = [&](float x) { return ySpanAt(footprint, x).second; };
	size_t		pairs = 0;
	size_t		singles = 0;
	for (const ItemStrip& strip : layout.strips) {
		if (strip.minX < xs[1] - kTolerance || strip.maxX > xs[2] + kTolerance) {
			continue;
		}
		if (std::abs(strip.maxX - strip.minX - bandWidth) < 1.0e-4F) {
			++singles;
			continue;
		}
		ASSERT_NEAR(strip.maxX - strip.minX, 2.0F * bandWidth, 1.0e-4F) << "a pair of bands";
		const float first = std::max(southern(strip.minX), southern(strip.minX + bandWidth));
		const float second = std::max(southern(strip.minX + bandWidth), southern(strip.maxX));
		EXPECT_LE(std::abs(second - first), viewConfig().depthStripMergeMeters + 1.0e-5F) << "a merged strip's anchors spread by at most 0.02 m";
		EXPECT_NEAR(first, second - 0.0195F, 5.0e-4F) << "each band's anchor 0.0197 m past the last";
		EXPECT_NEAR(strip.anchorLocalY, std::max(first, second), 1.0e-4F);
		++pairs;
	}
	// The main span's first band joins the narrow end span's strip beside it (0.019 m apart, its southern edge the
	// same front edge), the 22 after it merge in pairs, and the last stands alone: the other end's southern edge is
	// the steep right end, which never merges.
	EXPECT_EQ(pairs, 11U);
	EXPECT_EQ(singles, 1U);
	expectAscending(layout.strips);
}

TEST_F(ItemViewTest, NotchedAndSplitBodiesCutIntoAStripAnInterval) {
	// A C open to the east: the spine, and the north and south arms beside the notch.
	const AssetDefinition* letterC = loadBlocks("LetterC", {{-1.0F, -1.0F, -0.4F, 1.0F}, {-0.4F, -1.0F, 1.0F, -0.4F}, {-0.4F, 0.4F, 1.0F, 1.0F}});
	ASSERT_NE(letterC, nullptr);
	ASSERT_EQ(letterC->partsColliders.size(), 1U);
	const StripLayout c = stripLayout(*letterC, 0, viewConfig());
	ASSERT_EQ(c.strips.size(), 3U);
	EXPECT_NEAR(c.strips[0].anchorLocalY, -0.4F, kTolerance) << "the north arm first";
	EXPECT_NEAR(c.strips[0].minX, -0.4F, kTolerance);
	EXPECT_NEAR(c.strips[1].anchorLocalY, 1.0F, kTolerance) << "the spine";
	EXPECT_LT(c.strips[1].minX, -0.99F);
	EXPECT_NEAR(c.strips[2].anchorLocalY, 1.0F, kTolerance) << "the south arm, after the spine left to right";
	EXPECT_NEAR(c.strips[2].minX, -0.4F, kTolerance) << "two strips in the bands through the notch";

	// An L whose notch opens to the south-east: the upright and the bar beside the notch.
	const AssetDefinition* letterL = loadBlocks("LetterL", {{-1.0F, -1.0F, 1.0F, -0.5F}, {-1.0F, -0.5F, -0.5F, 1.0F}});
	ASSERT_NE(letterL, nullptr);
	EXPECT_EQ(stripLayout(*letterL, 0, viewConfig()).strips.size(), 2U);

	// A U open to the north is one strip (its southern edge is one line); open to the south, three.
	const AssetDefinition* openNorth = loadBlocks("OpenNorth", {{-1.0F, -1.0F, -0.6F, 0.6F}, {0.6F, -1.0F, 1.0F, 0.6F}, {-1.0F, 0.6F, 1.0F, 1.0F}});
	ASSERT_NE(openNorth, nullptr);
	EXPECT_EQ(stripLayout(*openNorth, 0, viewConfig()).strips.size(), 1U);
	const AssetDefinition* openSouth = loadBlocks("OpenSouth", {{-1.0F, -1.0F, 1.0F, -0.6F}, {-1.0F, -0.6F, -0.6F, 1.0F}, {0.6F, -0.6F, 1.0F, 1.0F}});
	ASSERT_NE(openSouth, nullptr);
	EXPECT_EQ(stripLayout(*openSouth, 0, viewConfig()).strips.size(), 3U);

	// Two rings side by side are two strips, and two one behind the other are two strips in the same bands.
	const AssetDefinition* sideBySide = loadBlocks("SideBySide", {{-1.0F, -0.3F, -0.2F, 0.3F}, {0.2F, -0.3F, 1.0F, 0.3F}});
	ASSERT_NE(sideBySide, nullptr);
	ASSERT_EQ(sideBySide->partsColliders.size(), 2U);
	EXPECT_EQ(stripLayout(*sideBySide, 0, viewConfig()).strips.size(), 2U);
	const AssetDefinition* behind = loadBlocks("Behind", {{-0.5F, -1.0F, 0.5F, -0.2F}, {-0.5F, 0.2F, 0.5F, 1.0F}});
	ASSERT_NE(behind, nullptr);
	const StripLayout stacked = stripLayout(*behind, 0, viewConfig());
	ASSERT_EQ(stacked.strips.size(), 2U);
	EXPECT_NEAR(stacked.strips[0].anchorLocalY, -0.2F, kTolerance);
	EXPECT_NEAR(stacked.strips[1].anchorLocalY, 1.0F, kTolerance);
}

TEST_F(ItemViewTest, AMatUnderABenchIsInTheFlatStripFirst) {
	const AssetDefinition* def = loadBench(3.0F, 1.0F, true);
	ASSERT_NE(def, nullptr);
	ASSERT_TRUE(issues("Bench").empty()) << ::testing::PrintToString(issues("Bench"));
	const StripLayout layout = stripLayout(*def, quantaOf(30.0), viewConfig());
	ASSERT_GE(layout.strips.size(), 2U);
	EXPECT_TRUE(layout.strips.front().flat);
	EXPECT_EQ(layout.strips.front().anchorLocalY, std::numeric_limits<float>::lowest());
	ASSERT_EQ(layout.strips.front().pieces.size(), 1U);
	EXPECT_EQ(layout.strips.front().pieces.front().placement, 1U) << "the mat";
	for (size_t i = 1; i < layout.strips.size(); ++i) {
		EXPECT_FALSE(layout.strips[i].flat);
		for (const StripPiece& piece : layout.strips[i].pieces) {
			EXPECT_EQ(piece.placement, 0U) << "only the bench is cut into the upright strips";
		}
	}
	const ItemView view = buildItemView(*def, quantaOf(30.0), 0, viewConfig());
	ASSERT_TRUE(view.error.empty()) << view.error;
	EXPECT_GT(view.strips.front().indexCount, 0U);
	expectContiguous(view);
}

TEST_F(ItemViewTest, TheLayoutAgreesWithEveryLevelsViewWhoseStripsAreContiguousAndHoldTheirTriangles) {
	writeWorkbench();
	const AssetDefinition* workbench = load("Workbench");
	const AssetDefinition* letterC = loadBlocks("LetterC", {{-1.0F, -1.0F, -0.4F, 1.0F}, {-0.4F, -1.0F, 1.0F, -0.4F}, {-0.4F, 0.4F, 1.0F, 1.0F}});
	workbench = AssetRegistry::Get().getDefinition("Workbench");
	ASSERT_NE(workbench, nullptr);
	ASSERT_NE(letterC, nullptr);
	for (const AssetDefinition* def : {workbench, letterC}) {
		for (const double degrees : {0.0, 35.0, 135.0, 271.0}) {
			const StripLayout layout = stripLayout(*def, quantaOf(degrees), viewConfig());
			expectAscending(layout.strips);
			for (uint8_t level = 0; level < std::max<size_t>(def->detailLevels.size(), 1); ++level) {
				const ItemView view = buildItemView(*def, quantaOf(degrees), level, viewConfig());
				ASSERT_TRUE(view.error.empty()) << view.error;
				ASSERT_EQ(view.strips.size(), layout.strips.size()) << def->defName << " " << degrees;
				expectContiguous(view);
				for (size_t i = 0; i < view.strips.size(); ++i) {
					const ViewStrip& strip = view.strips[i];
					EXPECT_EQ(strip.anchorLocalY, layout.strips[i].anchorLocalY);
					EXPECT_EQ(strip.minX, layout.strips[i].minX);
					EXPECT_EQ(strip.maxX, layout.strips[i].maxX);
					EXPECT_EQ(strip.minY, layout.strips[i].minY);
					EXPECT_EQ(strip.maxY, layout.strips[i].maxY);
					for (uint32_t index = strip.firstIndex; index < strip.firstIndex + strip.indexCount; ++index) {
						const Foundation::Vec2& vertex = view.mesh.vertices[view.mesh.indices[index]];
						ASSERT_TRUE(
							vertex.x >= strip.minX - kTolerance && vertex.x <= strip.maxX + kTolerance && vertex.y >= strip.minY - kTolerance &&
							vertex.y <= strip.maxY + kTolerance
						) << def->defName << " at " << degrees << " degrees, level " << int(level) << ", strip " << i << ": (" << vertex.x << ", " << vertex.y << ")";
					}
				}
			}
		}
	}
}

// ─── Detail levels, the template, and rule 12 ───────────────────────────────────────────────────────────

TEST_F(ItemViewTest, DetailLevelsDropTaggedShapes) {
	writeWorkbench();
	const AssetDefinition* def = load("Workbench");
	ASSERT_NE(def, nullptr);
	const auto black = [](const ItemView& view) {
		return std::count_if(view.mesh.colors.begin(), view.mesh.colors.end(), [](const Foundation::Color& color) {
			return color.r == 0.0F && color.g == 0.0F && color.b == 0.0F;
		});
	};
	const ItemView reference = buildItemView(*def, 0, 1, viewConfig());
	const ItemView finest = buildItemView(*def, 0, 2, viewConfig());
	EXPECT_EQ(black(reference), 0) << "the worktop's data-lod=60 square is not drawn at 24 px/m";
	EXPECT_GT(black(finest), 0) << "it is at 60 px/m";
	EXPECT_GT(finest.mesh.vertices.size(), reference.mesh.vertices.size());
	EXPECT_FALSE(buildItemView(*def, 0, 3, viewConfig()).error.empty()) << "no level 3";
	EXPECT_TRUE(reference.silhouette.valid) << "the reference level carries the silhouette";
	EXPECT_FALSE(finest.silhouette.valid);
}

TEST_F(ItemViewTest, TheTemplateIsTheTurnZeroViewJoined) {
	writeWorkbench();
	const AssetDefinition* def = load("Workbench");
	ASSERT_NE(def, nullptr);
	AssetRegistry& registry = AssetRegistry::Get();
	const uint32_t id = registry.templateId("Workbench", {});
	ASSERT_NE(id, 0U);
	const renderer::TessellatedMesh* mesh = registry.getTemplate(id);
	ASSERT_NE(mesh, nullptr);
	const ItemView view = buildItemView(*def, 0, referenceDetailLevel(def->detailLevels), viewConfig());
	ASSERT_TRUE(view.error.empty()) << view.error;
	EXPECT_EQ(mesh->vertices, view.mesh.vertices);
	EXPECT_EQ(mesh->indices, view.mesh.indices);
	ASSERT_EQ(mesh->colors.size(), view.mesh.colors.size());
	for (size_t i = 0; i < mesh->colors.size(); ++i) {
		ASSERT_EQ(mesh->colors[i].r, view.mesh.colors[i].r);
		ASSERT_EQ(mesh->colors[i].a, view.mesh.colors[i].a);
	}
	const AssetSilhouette* silhouette = registry.getSilhouette(id);
	ASSERT_NE(silhouette, nullptr);
	EXPECT_TRUE(silhouette->valid);
	EXPECT_EQ(silhouette->rings, view.silhouette.rings);
	EXPECT_GE(registry.maxReachMeters(), itemReachMeters(*def->parts, viewConfig()) - 1.0e-6F) << "the cull reach covers every turn";
	EXPECT_NEAR(itemReachMeters(*def->parts, viewConfig()), std::hypot(1.5F, 0.5F) + 0.8F * 0.35F + 0.015F, 1.0e-4F);
	// Its levels are the view's levels.
	const AssetRegistry::BuiltTemplate finest = registry.buildTemplateLevel(id, 2);
	ASSERT_TRUE(finest.mesh.has_value()) << finest.error;
	EXPECT_EQ(finest.mesh->vertices, buildItemView(*def, 0, 2, viewConfig()).mesh.vertices);
	std::vector<renderer::TessellatedMesh> meshes;
	EXPECT_TRUE(registry.buildMeshes("Workbench", 0, meshes));
}

TEST_F(ItemViewTest, Rule12RefusesAViewOverTheVertexLimitAtAStop) {
	// A top of 14,000 small squares: 56,000 vertices at every turn.
	std::string squares;
	for (int column = 0; column < 140; ++column) {
		for (int row = 0; row < 100; ++row) {
			squares += "M" + std::to_string(column * 10) + " " + std::to_string(row * 10) + "h5v5h-5z";
		}
	}
	folder.write("Dense/top.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 1400 1000\"><path d=\"" + squares + "\" fill=\"#808080\"/></svg>");
	writeDef(
		"Dense",
		"<parts><part name=\"slab\"><footprint>" + rect(-0.7F, -0.5F, 0.7F, 0.5F) +
			"</footprint><height bottom=\"0\" top=\"0.4\"/><top drawing=\"top.svg\"/><sides color=\"#808080\"/></part></parts><placements>" +
			place("slab") + "</placements>"
	);
	writeWorkbench();
	load("Dense");
	EXPECT_TRUE(hasIssue("Dense", "vertices at its finest level"));
	EXPECT_TRUE(issues("Workbench").empty()) << ::testing::PrintToString(issues("Workbench"));
}
