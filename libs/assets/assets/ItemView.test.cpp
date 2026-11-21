// Tests for item views (rotatable-placed-items.md, sections 3, 6, 7, and 20): the turn quantum, the face matrix and
// which faces show, shading, constant line width, narrow faces, the face clip, the parts order, depth strips and
// their layout, detail levels, the turn-0 template, and validation rule 12. Fixtures are written to a temp folder.

#include "assets/ItemView.h"
#include "assets/DetailLevels.h"
#include "assets/PartsAsset.test.h"
#include "assets/PointTurn.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <numbers>
#include <numeric>
#include <optional>
#include <random>
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

	std::vector<glm::vec2> faceQuad(const VisibleFace& face, float length, float height) {
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

	bool sameColor(const Foundation::Color& color, const Foundation::Color& wanted) {
		return color.r == wanted.r && color.g == wanted.g && color.b == wanted.b && color.a == wanted.a;
	}

	// The southernmost y at x of the def's collider turned: its southern edge where the item has no notch facing south.
	float southAt(const AssetDefinition& def, float turn, float x) {
		float south = -std::numeric_limits<float>::infinity();
		for (const CollisionShape& ring : def.partsColliders) {
			for (size_t i = 0; i < ring.pointsMeters.size(); ++i) {
				const glm::vec2 from = turnedPoint(ring.pointsMeters[i], turn);
				const glm::vec2 to = turnedPoint(ring.pointsMeters[(i + 1) % ring.pointsMeters.size()], turn);
				if ((from.x - x) * (to.x - x) < 0.0F) {
					south = std::max(south, from.y + (to.y - from.y) * (x - from.x) / (to.x - from.x));
				}
			}
		}
		return south;
	}

	struct ExpectedBand {
		float minX = 0.0F;
		float maxX = 0.0F;
		float anchor = 0.0F;
		float rise = 0.0F;
	};

	// The bands of section 6.7 worked out from the spans and the rise: a boundary at every collider vertex x, each
	// span in the fewest even bands no wider than the strip width across which the southern edge rises no more than
	// the strip rise. For items with no notch facing south.
	std::vector<ExpectedBand> expectedBands(const AssetDefinition& def, float turn, const ViewConfig& config) {
		std::vector<float> xs;
		for (const CollisionShape& ring : def.partsColliders) {
			for (const glm::vec2& point : ring.pointsMeters) {
				xs.push_back(turnedPoint(point, turn).x);
			}
		}
		std::sort(xs.begin(), xs.end());
		xs.erase(std::unique(xs.begin(), xs.end(), [](float left, float right) { return right - left < 1.0e-5F; }), xs.end());
		const auto south = [&](float x) { return southAt(def, turn, x); };
		std::vector<ExpectedBand> bands;
		for (size_t span = 0; span + 1 < xs.size(); ++span) {
			const float width = xs[span + 1] - xs[span];
			const float middle = (xs[span] + xs[span + 1]) * 0.5F;
			const float slope = std::abs(south(middle + width / 4) - south(middle - width / 4)) / (width / 2);
			const auto	count = static_cast<size_t>(std::max(
				 {1.0, std::ceil(width / config.depthStripWidthMeters - 1.0e-4), std::ceil(width * slope / config.depthStripRiseMeters - 1.0e-4)}
			 ));
			for (size_t i = 0; i < count; ++i) {
				const float from = xs[span] + width * static_cast<float>(i) / static_cast<float>(count);
				const float to = xs[span] + width * static_cast<float>(i + 1) / static_cast<float>(count);
				const float atFrom = south(from + 1.0e-6F);
				const float atTo = south(to - 1.0e-6F);
				bands.push_back({from, to, std::max(atFrom, atTo), std::abs(atTo - atFrom)});
			}
		}
		return bands;
	}

	// The expected bands whose middles lie in a strip's x range.
	std::vector<ExpectedBand> bandsIn(const ItemStrip& strip, const std::vector<ExpectedBand>& bands) {
		std::vector<ExpectedBand> inside;
		for (const ExpectedBand& band : bands) {
			const float middle = (band.minX + band.maxX) * 0.5F;
			if (middle > strip.minX && middle < strip.maxX) {
				inside.push_back(band);
			}
		}
		return inside;
	}

	// The perpendicular thickness on screen of the triangles of `color` lying along the line from `from` to `to`: those
	// whose vertices are all within `reach` of it and that span part of the middle three fifths of its length.
	float bandThickness(const renderer::TessellatedMesh& mesh, const Foundation::Color& color, glm::vec2 from, glm::vec2 to, float reach) {
		const float		length = std::hypot((to - from).x, (to - from).y);
		const glm::vec2 along = (to - from) / length;
		const glm::vec2 across{-along.y, along.x};
		float			low = std::numeric_limits<float>::infinity();
		float			high = -std::numeric_limits<float>::infinity();
		for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
			std::array<float, 3> position{};
			std::array<float, 3> distance{};
			bool				 taken = true;
			for (size_t corner = 0; corner < 3; ++corner) {
				const uint16_t index = mesh.indices[t + corner];
				const glm::vec2 offset = mesh.vertices[index] - from;
				position[corner] = offset.x * along.x + offset.y * along.y;
				distance[corner] = offset.x * across.x + offset.y * across.y;
				taken = taken && sameColor(mesh.colors[index], color) && std::abs(distance[corner]) <= reach;
			}
			const auto [first, last] = std::minmax_element(position.begin(), position.end());
			if (!taken || *last < length * 0.2F || *first > length * 0.8F) {
				continue;
			}
			for (const float value : distance) {
				low = std::min(low, value);
				high = std::max(high, value);
			}
		}
		return high - low;
	}

	// Every view the tests build: each vertex position and colour finite, so no test passes over a NaN, and no
	// triangle three copies of one point (what a clip makes of a triangle that only touches its plane).
	void expectWellFormed(const AssetDefinition& def, const ItemView& view, uint8_t level) {
		const auto finite = [](float value) { return std::isfinite(value); };
		for (size_t i = 0; i < view.mesh.vertices.size(); ++i) {
			const Foundation::Vec2&	 vertex = view.mesh.vertices[i];
			const Foundation::Color& color = view.mesh.colors[i];
			if (!(finite(vertex.x) && finite(vertex.y) && finite(color.r) && finite(color.g) && finite(color.b) && finite(color.a))) {
				ADD_FAILURE() << def.defName << " at turn " << view.turnQuanta << ", level " << int(level) << ": vertex " << i
							  << " is not finite";
				break;
			}
		}
		for (size_t index = 0; index + 2 < view.mesh.indices.size(); index += 3) {
			const Foundation::Vec2& first = view.mesh.vertices[view.mesh.indices[index]];
			if (first == view.mesh.vertices[view.mesh.indices[index + 1]] && first == view.mesh.vertices[view.mesh.indices[index + 2]]) {
				ADD_FAILURE() << def.defName << " at turn " << view.turnQuanta << ", level " << int(level) << ": triangle " << index / 3
							  << " is three copies of (" << first.x << ", " << first.y << ")";
				break;
			}
		}
	}

	ItemView checkedView(const AssetDefinition& def, uint32_t turnQuanta, uint8_t level, const ViewConfig& config) {
		ItemView view = buildItemView(def, turnQuanta, level, config);
		expectWellFormed(def, view, level);
		return view;
	}

	ItemView checkedMesh(const AssetDefinition& def, uint32_t turnQuanta, uint8_t level, const ViewConfig& config) {
		ItemView view = buildItemMesh(def, turnQuanta, level, config);
		expectWellFormed(def, view, level);
		return view;
	}

	// Every strip covers whole bands whose anchors spread by no more than `merge`, and takes the largest of them.
	void expectMergedWithin(const StripLayout& layout, const std::vector<ExpectedBand>& bands, float merge) {
		size_t covered = 0;
		for (const ItemStrip& strip : layout.strips) {
			const std::vector<ExpectedBand> inside = bandsIn(strip, bands);
			ASSERT_FALSE(inside.empty()) << "strip from " << strip.minX;
			float least = inside.front().anchor;
			float greatest = least;
			for (const ExpectedBand& band : inside) {
				least = std::min(least, band.anchor);
				greatest = std::max(greatest, band.anchor);
			}
			EXPECT_LE(greatest - least, merge + 1.0e-5F) << "strip from " << strip.minX << ", " << inside.size() << " bands";
			EXPECT_NEAR(strip.anchorLocalY, greatest, 1.0e-4F) << "strip from " << strip.minX;
			covered += inside.size();
		}
		EXPECT_EQ(covered, bands.size()) << "each band in one strip";
	}

	bool insideConvex(glm::vec2 point, const std::vector<glm::vec2>& ring) {
		float doubledArea = 0.0F;
		for (size_t i = 0; i < ring.size(); ++i) {
			doubledArea += ring[i].x * ring[(i + 1) % ring.size()].y - ring[(i + 1) % ring.size()].x * ring[i].y;
		}
		for (size_t i = 0; i < ring.size(); ++i) {
			const glm::vec2 along = ring[(i + 1) % ring.size()] - ring[i];
			const glm::vec2 inward = glm::vec2{-along.y, along.x} * (doubledArea >= 0.0F ? 1.0F : -1.0F) / std::hypot(along.x, along.y);
			// Written so that a NaN point is outside.
			if (!((point.x - ring[i].x) * inward.x + (point.y - ring[i].y) * inward.y >= -kTolerance)) {
				return false;
			}
		}
		return true;
	}

	std::array<glm::vec2, 3> triangleOf(const ItemView& view, size_t first) {
		return {
			view.mesh.vertices[view.mesh.indices[first]],
			view.mesh.vertices[view.mesh.indices[first + 1]],
			view.mesh.vertices[view.mesh.indices[first + 2]]
		};
	}

	float crossOf(glm::vec2 origin, glm::vec2 first, glm::vec2 second) {
		return (first.x - origin.x) * (second.y - origin.y) - (first.y - origin.y) * (second.x - origin.x);
	}

	bool containsPoint(const std::array<glm::vec2, 3>& corners, glm::vec2 point) {
		const float first = crossOf(corners[0], corners[1], point);
		const float second = crossOf(corners[1], corners[2], point);
		const float third = crossOf(corners[2], corners[0], point);
		const float slack = 1.0e-9F;
		return (first >= -slack && second >= -slack && third >= -slack) || (first <= slack && second <= slack && third <= slack);
	}

	float distanceToSegment(glm::vec2 point, glm::vec2 from, glm::vec2 to) {
		const glm::vec2 along = to - from;
		const float		lengthSquared = along.x * along.x + along.y * along.y;
		const float		at = lengthSquared > 0.0F
								 ? std::clamp(((point.x - from.x) * along.x + (point.y - from.y) * along.y) / lengthSquared, 0.0F, 1.0F)
								 : 0.0F;
		const glm::vec2 nearest = from + along * at;
		return std::hypot(point.x - nearest.x, point.y - nearest.y);
	}

	// The middle of what `polygon` and the convex `clip` (either winding) share, when they share more than a sliver.
	std::optional<glm::vec2> overlapCentre(std::vector<glm::vec2> polygon, const std::vector<glm::vec2>& clip) {
		float clipArea = 0.0F;
		for (size_t i = 0; i < clip.size(); ++i) {
			clipArea += crossOf({0.0F, 0.0F}, clip[i], clip[(i + 1) % clip.size()]);
		}
		const float sign = clipArea >= 0.0F ? 1.0F : -1.0F;
		for (size_t edge = 0; edge < clip.size() && !polygon.empty(); ++edge) {
			const glm::vec2		   from = clip[edge];
			const glm::vec2		   to = clip[(edge + 1) % clip.size()];
			std::vector<glm::vec2> kept;
			for (size_t i = 0; i < polygon.size(); ++i) {
				const glm::vec2 current = polygon[i];
				const glm::vec2 following = polygon[(i + 1) % polygon.size()];
				const float		currentSide = sign * crossOf(from, to, current);
				const float		followingSide = sign * crossOf(from, to, following);
				if (currentSide >= 0.0F) {
					kept.push_back(current);
				}
				if ((currentSide >= 0.0F) != (followingSide >= 0.0F)) {
					kept.push_back(current + (following - current) * (currentSide / (currentSide - followingSide)));
				}
			}
			polygon = std::move(kept);
		}
		float	  doubledArea = 0.0F;
		glm::vec2 centre{0.0F};
		for (size_t i = 0; i < polygon.size(); ++i) {
			doubledArea += crossOf({0.0F, 0.0F}, polygon[i], polygon[(i + 1) % polygon.size()]);
			centre += polygon[i] / static_cast<float>(polygon.size());
		}
		if (std::abs(doubledArea) < 1.0e-8F) {
			return std::nullopt;
		}
		return centre;
	}

	class ItemViewTest : public PartsTest {
	  protected:
		// One rect part, 1.0 x 0.6 m, standing from 0.2 to 0.8 m.
		const AssetDefinition* loadBox() {
			writeDef("Box", "<parts>" + block("box", -0.5F, -0.3F, 0.5F, 0.3F, 0.2F, 0.8F) + "</parts><placements>" + place("box") + "</placements>");
			return load("Box");
		}

		// The stool of section 4.4: four legs under a 0.5 m seat.
		void writeStool() {
			writeDef(
				"Stool",
				"<parts>" + block("leg", -0.035F, -0.035F, 0.035F, 0.035F, 0.0F, 0.43F) + block("seat", -0.25F, -0.25F, 0.25F, 0.25F, 0.43F, 0.5F) +
					"</parts><placements>" + place("leg", -0.19F, -0.19F) + place("leg", 0.19F, -0.19F) + place("leg", 0.19F, 0.19F) +
					place("leg", -0.19F, 0.19F) + place("seat") + "</placements>"
			);
		}

		const AssetDefinition* loadStool() {
			writeStool();
			return load("Stool");
		}

		// A plain bench, `length` by `depth`, 0.8 m tall, and optionally a mat on the ground under it.
		void writeBench(float length, float depth, bool mat = false) {
			const std::string parts =
				block("bench", -length / 2, -depth / 2, length / 2, depth / 2, 0.0F, 0.8F) + (mat ? block("mat", -2.0F, -1.0F, 2.0F, 1.0F, 0.0F, 0.0F) : "");
			writeDef("Bench", "<parts>" + parts + "</parts><placements>" + place("bench") + (mat ? place("mat") : "") + "</placements>");
		}

		const AssetDefinition* loadBench(float length, float depth, bool mat = false) {
			writeBench(length, depth, mat);
			return load("Bench");
		}

		// A slab on a narrower base, as a worktop on its carcass: the base blue, the slab's top green and its sides red,
		// so no other triangle is the colour of the slab's top.
		void writeSlab() {
			writeDef(
				"Slab",
				"<parts><part name=\"base\"><footprint>" + rect(-1.42F, -0.42F, 1.42F, 0.42F) +
					"</footprint><height bottom=\"0\" top=\"0.72\"/><top color=\"#0000FF\"/><sides color=\"#0000FF\"/></part>"
					"<part name=\"slab\"><footprint>" +
					rect(-1.5F, -0.5F, 1.5F, 0.5F) +
					"</footprint><height bottom=\"0.72\" top=\"0.8\"/><top color=\"#00FF00\"/><sides "
					"color=\"#FF0000\"/></part></parts><placements>" +
					place("base") + place("slab") + "</placements>"
			);
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

		// An 8-sided round of radius 0.4 whose top drawing fills its bounding box (1.307 times its inradius across)
		// and spills past it.
		void writeDrum() {
			const float across = 2.0F * 0.4F * std::cos(std::numbers::pi_v<float> / 8.0F);
			folder.write("Drum/top.svg", svg(across, across, "<circle cx=\"70\" cy=\"10\" r=\"20\" fill=\"#FF0000\"/>"));
			writeDef(
				"Drum",
				"<parts><part name=\"drum\"><footprint><round radius=\"0.4\" sides=\"8\"/></footprint><height bottom=\"0\" top=\"0.5\"/>"
				"<top drawing=\"top.svg\"/><sides color=\"#00FF00\"/></part></parts><placements>" + place("drum") + "</placements>"
			);
		}

		// A 3 m slab on four corner legs and one centre leg whose footprint starts on a band edge, x = 0.
		void writeTable() {
			writeDef(
				"Table",
				"<parts>" + block("slab", -1.5F, -0.4F, 1.5F, 0.4F, 0.7F, 0.75F) + block("corner", -0.05F, -0.05F, 0.05F, 0.05F, 0.0F, 0.7F) +
					block("centre", 0.0F, 0.3F, 0.1F, 0.4F, 0.0F, 0.7F) + "</parts><placements>" + place("slab") + place("corner", -1.45F, -0.35F) +
					place("corner", 1.45F, -0.35F) + place("corner", 1.45F, 0.35F) + place("corner", -1.45F, 0.35F) + place("centre") + "</placements>"
			);
		}

		// A rect whose top drawing has a red disc spilling 0.1 m past its right edge.
		void writeOverflowingTop() {
			folder.write("Overflow/top.svg", svg(0.8F, 0.4F, "<circle cx=\"80\" cy=\"20\" r=\"10\" fill=\"#FF0000\"/>"));
			writeDef(
				"Overflow",
				"<parts><part name=\"box\"><footprint>" + rect(-0.4F, -0.2F, 0.4F, 0.2F) +
					"</footprint><height bottom=\"0\" top=\"0.5\"/><top drawing=\"top.svg\"/><sides color=\"#00FF00\"/></part></parts><placements>" +
					place("box") + "</placements>"
			);
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

// An entity's ecs::Rotation is radians from +X toward +Y, clockwise on the screen, the same sense as a turn's degrees.
TEST(TurnQuantum, AnEntitysRotationIsItsTurnClockwise) {
	constexpr float kPi = std::numbers::pi_v<float>;
	EXPECT_EQ(turnQuantaFromRadians(0.0F), 0U);
	EXPECT_EQ(turnQuantaFromRadians(kPi / 2.0F), 9000U);
	EXPECT_EQ(turnQuantaFromRadians(kPi), 18000U);
	EXPECT_EQ(turnQuantaFromRadians(3.0F * kPi / 2.0F), 27000U);
	EXPECT_EQ(turnQuantaFromRadians(-kPi / 2.0F), 27000U) << "a quarter turn back is three quarters on";
	EXPECT_EQ(turnQuantaFromRadians(-35.0F * kPi / 180.0F), 32500U);
	EXPECT_EQ(turnQuantaFromRadians(2.0F * kPi), 0U);
	for (const uint32_t quanta : {0U, 3500U, 9000U, 18000U, 27100U, 32500U, 35999U}) {
		EXPECT_EQ(turnQuantaFromRadians(turnRadiansFromQuanta(quanta)), quanta) << "a stored turn reads back as itself";
	}
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
	// At 35 degrees the front's corners land at absolute points: left (-0.5, 0.3) turned is (-0.58165, -0.04104), right
	// (0.5, 0.3) turned is (0.23751, 0.53254), raised 0.8 * 0.35 = 0.28 at the top and 0.2 * 0.35 = 0.07 at the bottom.
	const std::optional<VisibleFace> turned = findFace(visibleFaces(parts, radiansOf(35.0), viewConfig()), 0, 2);
	ASSERT_TRUE(turned.has_value());
	const auto expectAt = [&](glm::vec2 drawing, float x, float y) {
		const glm::vec2 screen = turned->matrix.applyToPoint(drawing);
		EXPECT_NEAR(screen.x, x, 2.0e-5F) << "(" << drawing.x << ", " << drawing.y << ")";
		EXPECT_NEAR(screen.y, y, 2.0e-5F) << "(" << drawing.x << ", " << drawing.y << ")";
	};
	expectAt({0.0F, 0.0F}, -0.58165F, -0.32104F);
	expectAt({1.0F, 0.0F}, 0.23751F, 0.25254F);
	expectAt({1.0F, 0.6F}, 0.23751F, 0.46254F);
	expectAt({0.0F, 0.6F}, -0.58165F, -0.11104F);
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
		const ItemView view = checkedView(*def, quantaOf(degrees), 0, viewConfig());
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

	// A front half red, half blue on a green box: seen nearly edge on (88 degrees, 0.035 m wide) it is one plain
	// rectangle of the drawing's average colour, shaded, never the unrelated green of the plain sides.
	folder.write(
		"Narrow/front.svg",
		bareSvg(1.0F, 0.6F, "<rect x=\"0\" y=\"0\" width=\"50\" height=\"60\" fill=\"#FF0000\"/><rect x=\"50\" y=\"0\" width=\"50\" height=\"60\" fill=\"#0000FF\"/>")
	);
	writeDef(
		"Narrow",
		"<parts><part name=\"box\"><footprint>" + rect(-0.5F, -0.3F, 0.5F, 0.3F) +
			"</footprint><height bottom=\"0\" top=\"0.6\"/><top color=\"#FFFFFF\"/><sides color=\"#00FF00\"><face side=\"front\" drawing=\"front.svg\"/></sides>"
			"</part></parts><placements>" + place("box") + "</placements>"
	);
	const AssetDefinition* def = load("Narrow");
	ASSERT_NE(def, nullptr);
	const Foundation::Color average = def->parts->drawings[*def->parts->parts[0].faces[2].drawing].averageColor;
	EXPECT_NEAR(average.r, 0.5F, 1.0e-4F);
	EXPECT_NEAR(average.g, 0.0F, 1.0e-6F);
	EXPECT_NEAR(average.b, 0.5F, 1.0e-4F);
	const auto count = [&](double degrees, auto predicate) {
		const ItemView view = checkedView(*def, quantaOf(degrees), 0, viewConfig());
		return std::count_if(view.mesh.colors.begin(), view.mesh.colors.end(), predicate);
	};
	const float shade = findFace(visibleFaces(*def->parts, radiansOf(88.0), viewConfig()), 0, 2)->shade;
	const auto	averaged = [&](const Foundation::Color& color) {
		 return std::abs(color.r - 0.5F * shade) < 1.0e-4F && color.g == 0.0F && std::abs(color.b - 0.5F * shade) < 1.0e-4F;
	};
	const auto blue = [](const Foundation::Color& color) { return color.b > 0.5F && color.r == 0.0F && color.g == 0.0F; };
	EXPECT_EQ(count(88.0, isRed), 0);
	EXPECT_EQ(count(88.0, blue), 0);
	EXPECT_GE(count(88.0, averaged), 3) << "the front's plain rectangle";
	EXPECT_GT(count(80.0, isRed), 0) << "at 80 degrees (0.17 m wide) the drawing shows";
}

TEST_F(ItemViewTest, AGradientOnASideIsItsColourAtEachVertexShaded) {
	// A 1 m x 0.6 m front filled by a gradient from red at its left end to green at its right.
	folder.write(
		"Graded/front.svg",
		bareSvg(
			1.0F, 0.6F,
			"<defs><linearGradient id=\"across\" gradientUnits=\"userSpaceOnUse\" x1=\"0\" y1=\"0\" x2=\"100\" y2=\"0\">"
			"<stop offset=\"0\" stop-color=\"#FF0000\"/><stop offset=\"1\" stop-color=\"#00FF00\"/></linearGradient></defs>"
			"<rect x=\"0\" y=\"0\" width=\"100\" height=\"60\" fill=\"url(#across)\"/>"
		)
	);
	writeDef(
		"Graded",
		"<parts><part name=\"box\"><footprint>" + rect(-0.5F, -0.3F, 0.5F, 0.3F) +
			"</footprint><height bottom=\"0\" top=\"0.6\"/><top color=\"#FFFFFF\"/><sides color=\"#FFFFFF\"><face side=\"front\" drawing=\"front.svg\"/></sides>"
			"</part></parts><placements>" + place("box") + "</placements>"
	);
	const AssetDefinition* def = load("Graded");
	ASSERT_NE(def, nullptr);
	ASSERT_TRUE(issues("Graded").empty()) << ::testing::PrintToString(issues("Graded"));
	for (const double degrees : {0.0, 30.0, 330.0}) {
		const ItemView					 view = checkedView(*def, quantaOf(degrees), 0, viewConfig());
		const std::optional<VisibleFace> front = findFace(visibleFaces(*def->parts, radiansOf(degrees), viewConfig()), 0, 2);
		ASSERT_TRUE(front.has_value());
		size_t checked = 0;
		for (size_t i = 0; i < view.mesh.vertices.size(); ++i) {
			const Foundation::Color& color = view.mesh.colors[i];
			if (color.b != 0.0F || !insideConvex(view.mesh.vertices[i], faceQuad(*front, 1.0F, 0.6F))) {
				continue; // white sides and top, and the brown outline
			}
			// Along the face u = (x - e) / a; the gradient there, times the face's shade.
			const float along = (view.mesh.vertices[i].x - front->matrix.e) / front->matrix.a;
			EXPECT_NEAR(color.r, (1.0F - along) * front->shade, 2.0e-3F) << degrees << " degrees, u = " << along;
			EXPECT_NEAR(color.g, along * front->shade, 2.0e-3F) << degrees << " degrees, u = " << along;
			EXPECT_FLOAT_EQ(color.a, 1.0F);
			++checked;
		}
		EXPECT_GE(checked, 4U) << degrees << " degrees";
	}
}

TEST_F(ItemViewTest, OutlinesKeepTheirWidthAcrossASkewedEdge) {
	const AssetDefinition* def = loadBox();
	ASSERT_NE(def, nullptr);
	const ViewConfig& config = viewConfig();
	for (const double degrees : {20.0, 35.0, 330.0}) {
		const ItemView view = checkedView(*def, quantaOf(degrees), 0, config);
		ASSERT_TRUE(view.error.empty()) << view.error;
		const std::optional<VisibleFace> front = findFace(visibleFaces(*def->parts, radiansOf(degrees), config), 0, 2);
		ASSERT_TRUE(front.has_value());
		const glm::vec2 bottom{0.0F, -0.2F * config.heightRatio};
		const glm::vec2 top{0.0F, -0.8F * config.heightRatio};
		// The part outline runs along the front's bottom edge (the hull's south side); the top outline along its top edge.
		EXPECT_NEAR(bandThickness(view.mesh, config.outlineColor, front->left + bottom, front->right + bottom, 0.05F), config.outlineWidthMeters, config.outlineWidthMeters * 0.01F)
			<< degrees << " degrees, the part outline";
		EXPECT_NEAR(bandThickness(view.mesh, config.outlineTopColor, front->left + top, front->right + top, 0.05F), config.outlineTopWidthMeters, config.outlineTopWidthMeters * 0.01F)
			<< degrees << " degrees, the top outline";
	}
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
		const ItemView	   view = checkedView(*def, quantaOf(degrees), 0, viewConfig());
		const std::optional<VisibleFace> front = findFace(visibleFaces(*def->parts, radiansOf(degrees), viewConfig()), 0, 2);
		ASSERT_TRUE(front.has_value());
		ASSERT_TRUE(front->drawsArt);
		const std::vector<glm::vec2> quad = faceQuad(*front, 0.3F, 0.5F);
		size_t						   red = 0;
		for (size_t i = 0; i < view.mesh.vertices.size(); ++i) {
			if (isRed(view.mesh.colors[i])) {
				++red;
				EXPECT_TRUE(insideConvex(view.mesh.vertices[i], quad)) << degrees << " degrees, vertex " << i;
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
	// The same at an item turn of 45 degrees: the board placed square and the box placed where the turn brings it
	// to (-0.45, -0.15), so at 45 degrees the board is diagonal and the box (a diamond now) south of it on shared lines.
	const glm::vec2 boxAt = turnedPoint({-0.45F, -0.15F}, -std::numbers::pi_v<float> / 4.0F);
	writeDef(
		"BoardTurned",
		"<parts>" + block("box", -0.1F, -0.1F, 0.1F, 0.1F, 0.0F, 0.3F) + block("board", -0.6F, -0.05F, 0.6F, 0.05F, 0.5F, 0.55F) +
			"</parts><placements>" + place("box", boxAt.x, boxAt.y) + place("board") + "</placements>"
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
	for (const char* name : {"Shelf", "Board", "BoardTurned", "Table", "Workbench"}) {
		EXPECT_TRUE(issues(name).empty()) << name << ::testing::PrintToString(issues(name));
	}
	AssetRegistry& registry = AssetRegistry::Get();
	const auto	   orderOf = [&](const char* name, double degrees) { return partsDrawOrder(*registry.getDefinition(name)->parts, radiansOf(degrees)); };
	EXPECT_EQ(orderOf("Shelf", 0.0), (std::vector<uint32_t>{2, 0, 1}));
	EXPECT_EQ(orderOf("Board", 0.0), (std::vector<uint32_t>{1, 0})) << "the box is south of the board where they share verticals";
	EXPECT_EQ(orderOf("BoardTurned", 45.0), (std::vector<uint32_t>{1, 0})) << "the same pair, turned with the item";
	EXPECT_EQ(orderOf("BoardTurned", 225.0), (std::vector<uint32_t>{0, 1})) << "half a turn on, the box is north of the board";
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
	// Square to the screen each is one strip, its bands merged; at 45 degrees no band merges (each rises 0.088 m or
	// more), so there is a strip a band: the spec's 8 for the stool and 31 for the bench.
	const AssetDefinition* stool = loadStool();
	ASSERT_NE(stool, nullptr);
	EXPECT_EQ(stripLayout(*stool, quantaOf(0.0), viewConfig()).strips.size(), 1U);
	const std::vector<ExpectedBand> stoolBands = expectedBands(*stool, radiansOf(45.0), viewConfig());
	EXPECT_EQ(stripLayout(*stool, quantaOf(45.0), viewConfig()).strips.size(), stoolBands.size());
	EXPECT_EQ(stripLayout(*stool, quantaOf(45.0), viewConfig()).strips.size(), 8U) << "the spec's count";

	const AssetDefinition* bench = loadBench(3.0F, 1.0F);
	ASSERT_NE(bench, nullptr);
	EXPECT_EQ(stripLayout(*bench, quantaOf(0.0), viewConfig()).strips.size(), 1U);
	const StripLayout				turned = stripLayout(*bench, quantaOf(45.0), viewConfig());
	const std::vector<ExpectedBand> benchBands = expectedBands(*bench, radiansOf(45.0), viewConfig());
	ASSERT_EQ(turned.strips.size(), benchBands.size());
	EXPECT_EQ(turned.strips.size(), 31U) << "the spec's count";
	expectAscending(turned.strips);
	// Each anchor is its band's southern edge at its southern end: the anchors fall along the front edge.
	for (const ItemStrip& strip : turned.strips) {
		const std::vector<ExpectedBand> inside = bandsIn(strip, benchBands);
		ASSERT_EQ(inside.size(), 1U) << "strip from " << strip.minX;
		EXPECT_NEAR(strip.anchorLocalY, inside.front().anchor, 1.0e-4F) << "strip from " << strip.minX;
		EXPECT_FALSE(strip.flat);
	}
	EXPECT_NEAR(turned.strips.front().minX + viewConfig().outlineHalfWidthMeters(), benchBands.front().minX, 1.0e-4F)
		<< "the westmost strip has the lowest anchor";
}

TEST_F(ItemViewTest, ACounterAtAShallowAngleMergesItsBandsInPairs) {
	const AssetDefinition* counter = loadBench(6.0F, 0.6F);
	ASSERT_NE(counter, nullptr);
	const StripLayout				layout = stripLayout(*counter, quantaOf(4.5), viewConfig());
	const std::vector<ExpectedBand> bands = expectedBands(*counter, radiansOf(4.5), viewConfig());
	expectMergedWithin(layout, bands, viewConfig().depthStripMergeMeters);
	// Each band's anchor is about 0.0195 m past the last along the front, so pairs merge and threes don't.
	size_t pairs = 0;
	for (const ItemStrip& strip : layout.strips) {
		const std::vector<ExpectedBand> inside = bandsIn(strip, bands);
		EXPECT_LE(inside.size(), 2U) << "strip from " << strip.minX;
		pairs += inside.size() == 2 ? 1 : 0;
	}
	EXPECT_GE(pairs, 11U) << "the main span's 24 bands merge in pairs";
	EXPECT_LT(layout.strips.size(), bands.size());
	expectAscending(layout.strips);
}

TEST_F(ItemViewTest, AShallowVsMergedStripsSpreadByNoMoreThanTheMergeDistance) {
	// Two rects at minus and plus 2.5 degrees, the second stacked where they overlap: the southern edge is a shallow V
	// past the merge distance (0.028 m; the run's least and greatest anchors keep it to 0.017 m).
	writeDef(
		"ShallowV",
		"<parts>" + block("left", -3.0F, -0.3F, 0.3F, 0.3F, 0.0F, 0.8F) + block("right", -0.3F, -0.3F, 3.0F, 0.3F, 0.8F, 0.9F) + "</parts><placements>" +
			place("left", 0.0F, 0.0F, -2.5F) + place("right", 0.0F, 0.0F, 2.5F) + "</placements>"
	);
	const AssetDefinition* def = load("ShallowV");
	ASSERT_NE(def, nullptr);
	ASSERT_TRUE(issues("ShallowV").empty()) << ::testing::PrintToString(issues("ShallowV"));
	const StripLayout				layout = stripLayout(*def, 0, viewConfig());
	const std::vector<ExpectedBand> bands = expectedBands(*def, 0.0F, viewConfig());
	expectMergedWithin(layout, bands, viewConfig().depthStripMergeMeters);
	EXPECT_LT(layout.strips.size(), bands.size()) << "bands still merge along each arm";
}

TEST_F(ItemViewTest, NotchedAndSplitBodiesCutIntoAStripAnInterval) {
	// A C open to the east: the spine, and the north and south arms beside the notch.
	const AssetDefinition* letterC = loadBlocks("LetterC", {{-1.0F, -1.0F, -0.4F, 1.0F}, {-0.4F, -1.0F, 1.0F, -0.4F}, {-0.4F, 0.4F, 1.0F, 1.0F}});
	ASSERT_NE(letterC, nullptr);
	ASSERT_EQ(letterC->partsColliders.size(), 1U);
	const StripLayout letterLayout = stripLayout(*letterC, 0, viewConfig());
	ASSERT_EQ(letterLayout.strips.size(), 3U);
	EXPECT_NEAR(letterLayout.strips[0].anchorLocalY, -0.4F, kTolerance) << "the north arm first";
	EXPECT_NEAR(letterLayout.strips[0].minX, -0.4F, kTolerance) << "the arm's outline over the spine is in the spine's strip";
	EXPECT_NEAR(letterLayout.strips[1].anchorLocalY, 1.0F, kTolerance) << "the spine";
	EXPECT_LT(letterLayout.strips[1].minX, -0.99F);
	EXPECT_NEAR(letterLayout.strips[2].anchorLocalY, 1.0F, kTolerance) << "the south arm, after the spine left to right";
	EXPECT_NEAR(letterLayout.strips[2].minX, -0.4F, kTolerance) << "two strips in the bands through the notch";

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
	const ItemView view = checkedView(*def, quantaOf(30.0), 0, viewConfig());
	ASSERT_TRUE(view.error.empty()) << view.error;
	EXPECT_GT(view.strips.front().indexCount, 0U);
	expectContiguous(view);
}

TEST_F(ItemViewTest, TheLayoutAgreesWithEveryLevelsViewWhoseStripsAreContiguousAndHoldTheirTriangles) {
	writeWorkbench();
	writeDrum();
	writeTable();
	writeOverflowingTop();
	writeDef(
		"LetterL",
		"<parts>" + block("bar", 0.0F, 0.0F, 2.0F, 0.6F, 0.0F, 0.9F) + block("upright", 0.0F, 0.6F, 0.6F, 2.0F, 0.0F, 0.9F) + "</parts><placements>" +
			place("bar") + place("upright") + "</placements>"
	);
	const AssetDefinition* letterC = loadBlocks("LetterC", {{-1.0F, -1.0F, -0.4F, 1.0F}, {-0.4F, -1.0F, 1.0F, -0.4F}, {-0.4F, 0.4F, 1.0F, 1.0F}});
	ASSERT_NE(letterC, nullptr);
	std::vector<const AssetDefinition*> defs;
	for (const char* name : {"Workbench", "LetterC", "LetterL", "Drum", "Table", "Overflow"}) {
		defs.push_back(AssetRegistry::Get().getDefinition(name));
		ASSERT_NE(defs.back(), nullptr) << name;
	}
	for (const AssetDefinition* def : defs) {
		const float reach = itemReachMeters(*def->parts, viewConfig());
		for (const double degrees : {0.0, 35.0, 135.0, 271.0}) {
			const StripLayout layout = stripLayout(*def, quantaOf(degrees), viewConfig());
			expectAscending(layout.strips);
			for (uint8_t level = 0; level < std::max<size_t>(def->detailLevels.size(), 1); ++level) {
				const ItemView view = checkedView(*def, quantaOf(degrees), level, viewConfig());
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
				for (const Foundation::Vec2& vertex : view.mesh.vertices) {
					ASSERT_LE(std::hypot(vertex.x, vertex.y), reach + kTolerance) << def->defName << " at " << degrees << " degrees: past the cull reach";
				}
			}
		}
	}
}

TEST_F(ItemViewTest, ATopsArtStaysOnItsFootprint) {
	writeDrum();
	writeOverflowingTop();
	load("Drum");
	for (const char* name : {"Drum", "Overflow"}) {
		const AssetDefinition* def = AssetRegistry::Get().getDefinition(name);
		ASSERT_NE(def, nullptr) << name;
		EXPECT_TRUE(issues(name).empty()) << name << ::testing::PrintToString(issues(name));
		const float top = def->parts->parts[0].topMeters * viewConfig().heightRatio;
		for (const double degrees : {0.0, 22.5, 70.0, 200.0}) {
			const ItemView		   view = checkedView(*def, quantaOf(degrees), 0, viewConfig());
			std::vector<glm::vec2> lifted;
			for (const glm::vec2& point : turnedFootprint(*def->parts, 0, radiansOf(degrees))) {
				lifted.push_back(point + glm::vec2{0.0F, -top});
			}
			size_t red = 0;
			size_t grey = 0;
			for (size_t i = 0; i < view.mesh.vertices.size(); ++i) {
				const Foundation::Color& color = view.mesh.colors[i];
				const bool				 topArt = sameColor(color, {1.0F, 0.0F, 0.0F, 1.0F}) || (color.r == color.g && color.g == color.b && color.r > 0.4F && color.r < 0.6F);
				if (topArt) {
					red += color.g == 0.0F ? 1 : 0;
					grey += color.g != 0.0F ? 1 : 0;
					EXPECT_TRUE(insideConvex(view.mesh.vertices[i], lifted)) << name << " at " << degrees << " degrees, vertex " << i;
				}
			}
			EXPECT_GT(red, 0U) << name << " at " << degrees << " degrees";
			EXPECT_GT(grey, 0U) << name << " at " << degrees << " degrees";
		}
	}
}

TEST_F(ItemViewTest, AnOutlineIsNeverCutWhereAFootprintEndsOnABandEdge) {
	const ViewConfig& config = viewConfig();
	// An L whose upright ends at x = 0.6, a band edge: the upright's outline facing the notch keeps its full width.
	writeDef(
		"LetterL",
		"<parts>" + block("bar", 0.0F, 0.0F, 2.0F, 0.6F, 0.0F, 0.9F) + block("upright", 0.0F, 0.6F, 0.6F, 2.0F, 0.0F, 0.9F) + "</parts><placements>" +
			place("bar") + place("upright") + "</placements>"
	);
	// A 3 m slab whose centre leg starts at x = 0, a band edge: the leg's outline on that side keeps its full width.
	writeTable();
	load("Table");
	const ItemView letter = checkedView(*AssetRegistry::Get().getDefinition("LetterL"), 0, 0, config);
	ASSERT_TRUE(letter.error.empty()) << letter.error;
	EXPECT_NEAR(bandThickness(letter.mesh, config.outlineColor, {0.6F, 1.0F}, {0.6F, 1.8F}, 0.05F), config.outlineWidthMeters, config.outlineWidthMeters * 0.01F);
	const ItemView table = checkedView(*AssetRegistry::Get().getDefinition("Table"), 0, 0, config);
	ASSERT_TRUE(table.error.empty()) << table.error;
	EXPECT_NEAR(bandThickness(table.mesh, config.outlineColor, {0.0F, 0.12F}, {0.0F, 0.33F}, 0.05F), config.outlineWidthMeters, config.outlineWidthMeters * 0.01F);
}

TEST_F(ItemViewTest, StripsEitherSideOfABandEdgeShareBitIdenticalCutVertices) {
	const AssetDefinition* bench = loadBench(3.0F, 1.0F);
	ASSERT_NE(bench, nullptr);
	for (const double degrees : {30.0, 45.0, 117.0}) {
		const ItemView view = checkedView(*bench, quantaOf(degrees), 0, viewConfig());
		ASSERT_TRUE(view.error.empty()) << view.error;
		// Every vertex by its exact x, with the strip it is drawn in.
		std::map<float, std::vector<std::pair<size_t, uint32_t>>> byX;
		for (size_t strip = 0; strip < view.strips.size(); ++strip) {
			for (uint32_t index = view.strips[strip].firstIndex; index < view.strips[strip].firstIndex + view.strips[strip].indexCount; ++index) {
				byX[view.mesh.vertices[view.mesh.indices[index]].x].emplace_back(strip, view.mesh.indices[index]);
			}
		}
		// Band edges inside a span; at a collider vertex many source edges meet and near points are distinct geometry.
		std::vector<float> corners;
		for (const glm::vec2& point : bench->partsColliders.front().pointsMeters) {
			corners.push_back(turnedPoint(point, radiansOf(degrees)).x);
		}
		size_t shared = 0;
		for (const auto& [x, entries] : byX) {
			if (std::any_of(corners.begin(), corners.end(), [&](float corner) { return std::abs(corner - x) < 1.0e-3F; })) {
				continue;
			}
			for (size_t i = 0; i < entries.size(); ++i) {
				for (size_t j = i + 1; j < entries.size(); ++j) {
					if (entries[i].first == entries[j].first) {
						continue;
					}
					const uint32_t first = entries[i].second;
					const uint32_t second = entries[j].second;
					// Two vertices of one surface a hair apart on a band edge would open a seam; they must be one point.
					const float apart = std::abs(view.mesh.vertices[first].y - view.mesh.vertices[second].y);
					if (apart < 1.0e-4F && sameColor(view.mesh.colors[first], view.mesh.colors[second])) {
						ASSERT_EQ(view.mesh.vertices[first].y, view.mesh.vertices[second].y) << degrees << " degrees at x " << x;
						++shared;
					}
				}
			}
		}
		EXPECT_GT(shared, 0U) << degrees << " degrees";
	}
}

TEST_F(ItemViewTest, NoTwoStripsOverlapOnScreenAtAnyTurn) {
	// Items whose collider is convex rings, so every band holds at most one interval and is drawn by one strip: the
	// strips' triangles lie in x ranges that never overlap, and the strip order alone can't put one part over another
	// the wrong way. A slab on a narrower base and a slab on legs (one starting on a band edge) are what the outline's
	// reach past a footprint once broke. Two bodies side by side are sampled while the gap between them is open: there
	// each body's outline stays with its own strip, so where the gap is narrower than two of the outline's half widths
	// the two strips overlap by design, by the gap or by what the two outlines share of it, whichever is less.
	writeTable();
	writeSlab();
	writeStool();
	writeBench(3.0F, 1.0F);
	writeDef(
		"SideBySide",
		"<parts>" + block("west", -1.0F, -0.3F, -0.2F, 0.3F, 0.0F, 0.9F) + block("east", 0.2F, -0.3F, 1.0F, 0.3F, 0.0F, 0.9F) +
			"</parts><placements>" + place("west") + place("east") + "</placements>"
	);
	load("Table");
	const float			pad = viewConfig().outlineHalfWidthMeters();
	std::vector<double> everyDegree(360);
	std::iota(everyDegree.begin(), everyDegree.end(), 0.0);
	// The gap closes at about 33.7 degrees; it is under two half widths from 31.5 and under one from 32.5.
	const std::vector<std::pair<const char*, std::vector<double>>> cases = {
		{"Table", everyDegree},
		{"Slab", everyDegree},
		{"Stool", everyDegree},
		{"Bench", everyDegree},
		{"SideBySide", {0, 10, 20, 30, 31, 31.5, 32, 32.5, 32.75, 33, 33.25, 33.5, 33.6, 160, 170, 180, 190, 200, 212.5, 213, 340, 350}}
	};
	size_t views = 0;
	for (const auto& [name, turns] : cases) {
		const AssetDefinition* def = AssetRegistry::Get().getDefinition(name);
		ASSERT_NE(def, nullptr) << name;
		for (const double degrees : turns) {
			const ItemView view = checkedMesh(*def, quantaOf(degrees), 0, viewConfig());
			ASSERT_TRUE(view.error.empty()) << view.error;
			++views;
			float allowed = 1.0e-6F;
			if (std::string(name) == "SideBySide") {
				const float							   turn = radiansOf(degrees);
				std::array<std::pair<float, float>, 2> extents;
				for (size_t placement = 0; placement < 2; ++placement) {
					extents[placement] = {std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
					for (const glm::vec2& point : turnedFootprint(*def->parts, placement, turn)) {
						extents[placement].first = std::min(extents[placement].first, point.x);
						extents[placement].second = std::max(extents[placement].second, point.x);
					}
				}
				const float gap = std::max(extents[1].first - extents[0].second, extents[0].first - extents[1].second);
				allowed += std::max(0.0F, std::min(gap, 2.0F * pad - gap));
			}
			std::vector<std::pair<float, float>> ranges;
			for (const ViewStrip& strip : view.strips) {
				std::pair<float, float> range{std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()};
				for (uint32_t index = strip.firstIndex; index < strip.firstIndex + strip.indexCount; ++index) {
					range.first = std::min(range.first, view.mesh.vertices[view.mesh.indices[index]].x);
					range.second = std::max(range.second, view.mesh.vertices[view.mesh.indices[index]].x);
				}
				ranges.push_back(range);
			}
			for (size_t first = 0; first < ranges.size(); ++first) {
				for (size_t second = first + 1; second < ranges.size(); ++second) {
					const float overlap =
						std::min(ranges[first].second, ranges[second].second) - std::max(ranges[first].first, ranges[second].first);
					EXPECT_LE(overlap, allowed) << name << " at " << degrees << " degrees: strips " << first << " [" << ranges[first].first
												<< ", " << ranges[first].second << "] and " << second << " [" << ranges[second].first
												<< ", " << ranges[second].second << "]";
				}
			}
		}
	}
	EXPECT_EQ(views, 4U * 360U + 22U);
}

TEST_F(ItemViewTest, NothingOfALowerPartShowsThroughThePartAboveIt) {
	// A slab on a narrower base, as a worktop on its carcass. The slab is above the base in plan, so drawing the strips
	// in order is the parts order there: nothing of the base (its sides, its top, its outline) lands over the slab's
	// top after the slab's top has drawn, in whatever strips the layout cuts. Every 3 degrees: the fault this guards
	// against showed at about 60 turns in a row.
	writeSlab();
	const AssetDefinition* def = load("Slab");
	ASSERT_NE(def, nullptr);
	const ViewConfig&		config = viewConfig();
	const Foundation::Color slabTop(0.0F, 1.0F, 0.0F, 1.0F);
	// The slab's top less the band its own outlines reach into.
	const float inset = std::max(config.outlineWidthMeters, config.outlineTopWidthMeters);
	size_t		views = 0;
	for (int degrees = 0; degrees < 360; degrees += 3) {
		const float	   turn = radiansOf(degrees);
		const ItemView view = checkedMesh(*def, quantaOf(degrees), 0, config);
		ASSERT_TRUE(view.error.empty()) << view.error;
		++views;
		std::vector<glm::vec2> inner;
		for (const glm::vec2 corner :
			 {glm::vec2{-1.5F + inset, -0.5F + inset},
			  glm::vec2{1.5F - inset, -0.5F + inset},
			  glm::vec2{1.5F - inset, 0.5F - inset},
			  glm::vec2{-1.5F + inset, 0.5F - inset}}) {
			inner.push_back(turnedPoint(corner, turn) + glm::vec2{0.0F, -0.8F * config.heightRatio});
		}
		for (size_t index = 0; index + 2 < view.mesh.indices.size(); index += 3) {
			if (sameColor(view.mesh.colors[view.mesh.indices[index]], slabTop)) {
				continue;
			}
			// The part of this triangle over the slab's inner top, and a point inside that part.
			const std::array<glm::vec2, 3> corners = triangleOf(view, index);
			const std::optional<glm::vec2> centre = overlapCentre(std::vector<glm::vec2>(corners.begin(), corners.end()), inner);
			if (!centre.has_value()) {
				continue;
			}
			// The slab's top must draw over it there: a slab top triangle holding that point comes later.
			bool covered = false;
			for (size_t later = index + 3; later + 2 < view.mesh.indices.size() && !covered; later += 3) {
				covered = sameColor(view.mesh.colors[view.mesh.indices[later]], slabTop) && containsPoint(triangleOf(view, later), *centre);
			}
			if (!covered) {
				ADD_FAILURE() << degrees << " degrees: triangle " << index / 3 << " (" << corners[0].x << ", " << corners[0].y << ") ("
							  << corners[1].x << ", " << corners[1].y << ") (" << corners[2].x << ", " << corners[2].y
							  << ") shows over the slab's top at (" << centre->x << ", " << centre->y << ")";
				break;
			}
		}
	}
	EXPECT_EQ(views, 120U);
}

TEST_F(ItemViewTest, WhereTwoPartsOverlapOnScreenThePartInFrontDrawsLater) {
	// Bodies with notches and gaps, where an outline past a footprint's end lands in another interval's strip. Each
	// part has its own pure hue (its sides shaded, its top not), and the parts share the outline colour. Wherever two triangles of
	// different parts overlap on screen, the one drawn later must not belong to a part the parts order puts behind
	// the other's (section 6.6). An outline triangle belongs to whichever parts' outlines pass under it; a pair is
	// wrong only when every way of reading it is. Every 3 degrees.
	const std::vector<std::pair<std::string, std::vector<std::array<float, 6>>>> items = {
		{"LetterL", {{-1.0F, -1.0F, 1.0F, -0.5F, 0.0F, 0.9F}, {-1.0F, -0.5F, -0.5F, 1.0F, 0.0F, 0.9F}}},
		{"LetterC",
		 {{-1.0F, -1.0F, -0.4F, 1.0F, 0.0F, 0.9F}, {-0.4F, -1.0F, 1.0F, -0.4F, 0.0F, 0.9F}, {-0.4F, 0.4F, 1.0F, 1.0F, 0.0F, 0.9F}}},
		{"OpenSouth",
		 {{-1.0F, -1.0F, 1.0F, -0.6F, 0.0F, 0.9F}, {-1.0F, -0.6F, -0.6F, 1.0F, 0.0F, 0.9F}, {0.6F, -0.6F, 1.0F, 1.0F, 0.0F, 0.9F}}},
		{"TwoBodies", {{-1.0F, -0.3F, -0.2F, 0.3F, 0.0F, 0.9F}, {0.2F, -0.3F, 1.0F, 0.3F, 0.0F, 0.9F}}},
	};
	const std::array<const char*, 3> hues = {"#FF0000", "#00FF00", "#0000FF"};
	for (const auto& [name, rects] : items) {
		std::string parts;
		std::string placements;
		for (size_t i = 0; i < rects.size(); ++i) {
			const std::string part = "block" + std::to_string(i);
			parts += "<part name=\"" + part + "\"><footprint>" + rect(rects[i][0], rects[i][1], rects[i][2], rects[i][3]) +
					 "</footprint><height bottom=\"" + number(rects[i][4]) + "\" top=\"" + number(rects[i][5]) + "\"/><top color=\"" +
					 hues[i] + "\"/><sides color=\"" + hues[i] + "\"/></part>";
			placements += place(part);
		}
		writeDef(name, "<parts>" + parts + "</parts><placements>" + placements + "</placements>");
	}
	load("LetterL");
	const ViewConfig& config = viewConfig();
	// The part a colour is, by its one channel; none for the outline colour.
	const auto partOf = [](const Foundation::Color& color) -> std::optional<size_t> {
		const std::array<float, 3> channels = {color.r, color.g, color.b};
		std::optional<size_t>	   found;
		for (size_t i = 0; i < 3; ++i) {
			if (channels[i] > 0.0F) {
				if (found.has_value()) {
					return std::nullopt;
				}
				found = i;
			}
		}
		return found;
	};
	size_t views = 0;
	size_t pairs = 0;
	for (const auto& item : items) {
		const std::string&	   name = item.first;
		const AssetDefinition* def = AssetRegistry::Get().getDefinition(name);
		ASSERT_NE(def, nullptr) << name;
		const PartsAsset& parts = *def->parts;
		for (int degrees = 0; degrees < 360; degrees += 3) {
			const float	   turn = radiansOf(degrees);
			const ItemView view = checkedMesh(*def, quantaOf(degrees), 0, config);
			ASSERT_TRUE(view.error.empty()) << view.error;
			++views;
			std::vector<std::vector<glm::vec2>> footprints;
			std::vector<std::vector<glm::vec2>> outlines; // each placement's two outline rings, end to end
			std::vector<size_t>					outlineSplit;
			for (size_t placement = 0; placement < parts.placements.size(); ++placement) {
				const PartDefinition&  part = parts.parts[parts.placements[placement].part];
				std::vector<glm::vec2> footprint = turnedFootprint(parts, placement, turn);
				std::vector<glm::vec2> projected;
				std::vector<glm::vec2> top;
				for (const glm::vec2& point : footprint) {
					projected.push_back(point + glm::vec2{0.0F, -part.bottomMeters * config.heightRatio});
					projected.push_back(point + glm::vec2{0.0F, -part.topMeters * config.heightRatio});
					top.push_back(point + glm::vec2{0.0F, -part.topMeters * config.heightRatio});
				}
				std::vector<glm::vec2> rings = convexHull(projected);
				outlineSplit.push_back(rings.size());
				rings.insert(rings.end(), top.begin(), top.end());
				outlines.push_back(std::move(rings));
				footprints.push_back(std::move(footprint));
			}
			const std::vector<std::vector<size_t>> drawnAfter = partsDrawnAfter(parts, footprints);
			const auto							   behind = [&](size_t part, size_t other) {
				return std::find(drawnAfter[part].begin(), drawnAfter[part].end(), other) != drawnAfter[part].end();
			};
			// The parts whose outline passes under a point.
			const auto outlinesAt = [&](glm::vec2 point) {
				std::vector<size_t> found;
				const float			reach = config.outlineHalfWidthMeters() + 2.0e-3F;
				for (size_t placement = 0; placement < outlines.size(); ++placement) {
					const std::vector<glm::vec2>& rings = outlines[placement];
					const size_t				  split = outlineSplit[placement];
					bool						  near = false;
					for (size_t i = 0; i < rings.size() && !near; ++i) {
						const size_t	ringStart = i < split ? 0 : split;
						const size_t	ringSize = i < split ? split : rings.size() - split;
						const glm::vec2 from = rings[i];
						const glm::vec2 to = rings[ringStart + (i - ringStart + 1) % ringSize];
						near = distanceToSegment(point, from, to) <= reach;
					}
					if (near) {
						found.push_back(placement);
					}
				}
				return found;
			};
			struct Box {
				float  minX, maxX, minY, maxY;
				size_t index;
			};
			std::vector<Box> boxes;
			for (size_t index = 0; index + 2 < view.mesh.indices.size(); index += 3) {
				const std::array<glm::vec2, 3> corners = triangleOf(view, index);
				boxes.push_back(
					{std::min({corners[0].x, corners[1].x, corners[2].x}),
					 std::max({corners[0].x, corners[1].x, corners[2].x}),
					 std::min({corners[0].y, corners[1].y, corners[2].y}),
					 std::max({corners[0].y, corners[1].y, corners[2].y}),
					 index}
				);
			}
			std::sort(boxes.begin(), boxes.end(), [](const Box& left, const Box& right) { return left.minX < right.minX; });
			for (size_t a = 0; a < boxes.size(); ++a) {
				for (size_t b = a + 1; b < boxes.size() && boxes[b].minX < boxes[a].maxX; ++b) {
					if (boxes[b].minY >= boxes[a].maxY || boxes[a].minY >= boxes[b].maxY) {
						continue;
					}
					const size_t				   earlier = std::min(boxes[a].index, boxes[b].index);
					const size_t				   later = std::max(boxes[a].index, boxes[b].index);
					const std::array<glm::vec2, 3> earlierCorners = triangleOf(view, earlier);
					const std::array<glm::vec2, 3> laterCorners = triangleOf(view, later);
					const std::optional<glm::vec2> centre = overlapCentre(
						std::vector<glm::vec2>(laterCorners.begin(), laterCorners.end()),
						std::vector<glm::vec2>(earlierCorners.begin(), earlierCorners.end())
					);
					if (!centre.has_value()) {
						continue;
					}
					++pairs;
					const auto partsOf = [&](size_t index) {
						const std::optional<size_t> part = partOf(view.mesh.colors[view.mesh.indices[index]]);
						return part.has_value() ? std::vector<size_t>{*part} : outlinesAt(*centre);
					};
					const std::vector<size_t> earlierParts = partsOf(earlier);
					const std::vector<size_t> laterParts = partsOf(later);
					bool					  wrong = !earlierParts.empty() && !laterParts.empty();
					for (const size_t earlierPart : earlierParts) {
						for (const size_t laterPart : laterParts) {
							wrong = wrong && laterPart != earlierPart && behind(laterPart, earlierPart);
						}
					}
					if (wrong) {
						ADD_FAILURE() << name << " at " << degrees << " degrees: triangle " << later / 3 << " of part "
									  << laterParts.front() << " draws over triangle " << earlier / 3 << " of part " << earlierParts.front()
									  << ", which is in front of it, at (" << centre->x << ", " << centre->y << ")";
						return;
					}
				}
			}
		}
	}
	EXPECT_EQ(views, 4U * 120U);
	EXPECT_GT(pairs, 0U);
}

TEST_F(ItemViewTest, ADrawingsAverageColourIsWhatShows) {
	// A grey background with a red panel painted over its right half: half grey and half red, not the background
	// counted whole under the panel.
	folder.write("Panel/front.svg", svg(1.0F, 0.6F, "<rect x=\"50\" y=\"0\" width=\"50\" height=\"60\" fill=\"#FF0000\"/>"));
	writeDef(
		"Panel",
		"<parts><part name=\"box\"><footprint>" + rect(-0.5F, -0.3F, 0.5F, 0.3F) +
			"</footprint><height bottom=\"0\" top=\"0.6\"/><top color=\"#FFFFFF\"/><sides color=\"#00FF00\"><face side=\"front\" drawing=\"front.svg\"/></sides>"
			"</part></parts><placements>" + place("box") + "</placements>"
	);
	const AssetDefinition* def = load("Panel");
	ASSERT_NE(def, nullptr);
	const Foundation::Color average = def->parts->drawings[*def->parts->parts[0].faces[2].drawing].averageColor;
	const float				grey = static_cast<float>(0x80) / 255.0F;
	EXPECT_NEAR(average.r, (grey + 1.0F) / 2.0F, 1.0e-4F);
	EXPECT_NEAR(average.g, grey / 2.0F, 1.0e-4F);
	EXPECT_NEAR(average.b, grey / 2.0F, 1.0e-4F);
	EXPECT_FLOAT_EQ(average.a, 1.0F);
}

TEST_F(ItemViewTest, ATranslucentShapeIsAveragedOncePerPointItCovers) {
	// A half-transparent red square over grey: the lattice points on the diagonal its two triangles share are painted
	// once, as every other point is, so the average is the even mix.
	folder.write(
		"Glass/front.svg", svg(1.0F, 1.0F, "<rect x=\"0\" y=\"0\" width=\"100\" height=\"100\" fill=\"#FF0000\" fill-opacity=\"0.5\"/>")
	);
	writeDef(
		"Glass",
		"<parts><part name=\"box\"><footprint>" + rect(-0.5F, -0.5F, 0.5F, 0.5F) +
			"</footprint><height bottom=\"0\" top=\"1\"/><top color=\"#FFFFFF\"/><sides color=\"#00FF00\"><face side=\"front\" "
			"drawing=\"front.svg\"/></sides>"
			"</part></parts><placements>" +
			place("box") + "</placements>"
	);
	const AssetDefinition* def = load("Glass");
	ASSERT_NE(def, nullptr);
	const ItemDrawing& drawing = def->parts->drawings[*def->parts->parts[0].faces[2].drawing];
	ASSERT_EQ(drawing.shapes.size(), 2U);
	// The opacity as the SVG reader stores it, a byte: 127/255, not 0.5.
	const float alpha = drawing.shapes[1].shape.fillColor.a;
	const float grey = static_cast<float>(0x80) / 255.0F;
	EXPECT_NEAR(drawing.averageColor.r, grey * (1.0F - alpha) + alpha, 1.0e-4F);
	EXPECT_NEAR(drawing.averageColor.g, grey * (1.0F - alpha), 1.0e-4F);
	EXPECT_NEAR(drawing.averageColor.b, grey * (1.0F - alpha), 1.0e-4F);
}

TEST_F(ItemViewTest, RandomArtSpillingEveryWayIsClippedToItsFaceAndItsTop) {
	// 300 random triangles each on a front and a top, most spilling past the viewBox: every vertex they make is
	// finite and on its face or its top, at turns where the clip has two exactly vertical planes and where it has none.
	std::mt19937						  random(1146);
	std::uniform_real_distribution<float> acrossFront(-50.0F, 150.0F);
	std::uniform_real_distribution<float> downFront(-40.0F, 100.0F);
	std::uniform_real_distribution<float> acrossTop(-40.0F, 120.0F);
	std::uniform_real_distribution<float> downTop(-30.0F, 90.0F);
	std::uniform_int_distribution<int>	  shade(64, 255);
	const auto							  triangles = [&](auto& across, auto& down, bool red) {
		 std::string out;
		 for (int i = 0; i < 300; ++i) {
			 char color[8];
			 std::snprintf(color, sizeof(color), red ? "#%02X0000" : "#0000%02X", shade(random));
			 out += "<path d=\"M " + number(across(random)) + " " + number(down(random)) + " L " + number(across(random)) + " " + number(down(random)) +
					" L " + number(across(random)) + " " + number(down(random)) + " Z\" fill=\"" + color + "\"/>";
		 }
		 return out;
	};
	folder.write("Scatter/front.svg", bareSvg(1.0F, 0.6F, triangles(acrossFront, downFront, true)));
	folder.write("Scatter/top.svg", bareSvg(1.0F, 0.6F, triangles(acrossTop, downTop, false)));
	writeDef(
		"Scatter",
		"<parts><part name=\"box\"><footprint>" + rect(-0.5F, -0.3F, 0.5F, 0.3F) +
			"</footprint><height bottom=\"0\" top=\"0.6\"/><top drawing=\"top.svg\"/><sides color=\"#00FF00\"><face side=\"front\" drawing=\"front.svg\"/></sides>"
			"</part></parts><placements>" + place("box") + "</placements>"
	);
	const AssetDefinition* def = load("Scatter");
	ASSERT_NE(def, nullptr);
	const float lift = 0.6F * viewConfig().heightRatio;
	for (const double degrees : {0.0, 17.0, 45.0, 300.0, 333.0}) {
		const ItemView					 view = checkedView(*def, quantaOf(degrees), 0, viewConfig());
		const std::optional<VisibleFace> front = findFace(visibleFaces(*def->parts, radiansOf(degrees), viewConfig()), 0, 2);
		ASSERT_TRUE(front.has_value());
		const std::vector<glm::vec2> face = faceQuad(*front, 1.0F, 0.6F);
		std::vector<glm::vec2>		 top;
		for (const glm::vec2& point : turnedFootprint(*def->parts, 0, radiansOf(degrees))) {
			top.push_back(point + glm::vec2{0.0F, -lift});
		}
		size_t frontArt = 0;
		size_t topArt = 0;
		for (size_t i = 0; i < view.mesh.vertices.size(); ++i) {
			const Foundation::Color& color = view.mesh.colors[i];
			if (color.r > 0.0F && color.g == 0.0F && color.b == 0.0F) {
				++frontArt;
				ASSERT_TRUE(insideConvex(view.mesh.vertices[i], face)) << degrees << " degrees, front vertex " << i;
			} else if (color.b > 0.0F && color.r == 0.0F && color.g == 0.0F) {
				++topArt;
				ASSERT_TRUE(insideConvex(view.mesh.vertices[i], top)) << degrees << " degrees, top vertex " << i;
			}
		}
		EXPECT_GT(frontArt, 300U) << degrees << " degrees";
		EXPECT_GT(topArt, 300U) << degrees << " degrees";
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
	const ItemView reference = checkedView(*def, 0, 1, viewConfig());
	const ItemView finest = checkedView(*def, 0, 2, viewConfig());
	EXPECT_EQ(black(reference), 0) << "the worktop's data-lod=60 square is not drawn at 24 px/m";
	EXPECT_GT(black(finest), 0) << "it is at 60 px/m";
	EXPECT_GT(finest.mesh.vertices.size(), reference.mesh.vertices.size());
	EXPECT_FALSE(checkedView(*def, 0, 3, viewConfig()).error.empty()) << "no level 3";
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
	const ItemView view = checkedView(*def, 0, referenceDetailLevel(def->detailLevels), viewConfig());
	ASSERT_TRUE(view.error.empty()) << view.error;
	EXPECT_EQ(mesh->vertices, view.mesh.vertices);
	EXPECT_EQ(mesh->indices, view.mesh.indices);
	ASSERT_EQ(mesh->colors.size(), view.mesh.colors.size());
	for (size_t i = 0; i < mesh->colors.size(); ++i) {
		ASSERT_TRUE(sameColor(mesh->colors[i], view.mesh.colors[i])) << "vertex " << i;
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
	EXPECT_EQ(finest.mesh->vertices, checkedView(*def, 0, 2, viewConfig()).mesh.vertices);
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
	// A box whose right end carries 13,000 squares (52,000 vertices): hidden at 0 degrees, over the limit once it shows.
	std::string endSquares;
	for (int column = 0; column < 130; ++column) {
		for (int row = 0; row < 100; ++row) {
			endSquares += "M" + std::to_string(column * 46 / 10) + "." + std::to_string(column * 46 % 10) + " " + std::to_string(row * 10) + "h2v2h-2z";
		}
	}
	folder.write("Sideways/end.svg", "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 600 1000\"><path d=\"" + endSquares + "\" fill=\"#808080\"/></svg>");
	writeDef(
		"Sideways",
		"<parts><part name=\"box\"><footprint>" + rect(-0.5F, -0.3F, 0.5F, 0.3F) +
			"</footprint><height bottom=\"0\" top=\"1\"/><top color=\"#808080\"/><sides color=\"#808080\"><face side=\"right\" drawing=\"end.svg\"/></sides>"
			"</part></parts><placements>" + place("box") + "</placements>"
	);
	writeWorkbench();
	load("Dense");
	EXPECT_TRUE(hasIssue("Dense", "its view at 0 degrees"));
	const AssetDefinition* sideways = AssetRegistry::Get().getDefinition("Sideways");
	ASSERT_NE(sideways, nullptr);
	EXPECT_LT(checkedView(*sideways, 0, 0, viewConfig()).vertexCount, kMaxStopViewVertices) << "under the limit at 0 degrees";
	EXPECT_TRUE(hasIssue("Sideways", "its view at 15 degrees")) << "the first stop that shows the right end";
	EXPECT_TRUE(issues("Workbench").empty()) << ::testing::PrintToString(issues("Workbench"));
}

// ─── Kept layouts and the cull reach (sections 8 and 6.9) ────────────────────────────────────────────────

TEST_F(ItemViewTest, TheRegistryKeepsOneLayoutATemplateAndTurnUntilAClear) {
	ASSERT_NE(loadBench(3.0F, 1.0F), nullptr);
	AssetRegistry& registry = AssetRegistry::Get();
	const uint32_t bench = registry.templateId("Bench", {});
	ASSERT_NE(bench, 0U);
	EXPECT_EQ(registry.templateDefinition(bench), registry.getDefinition("Bench"));
	EXPECT_EQ(registry.templateDefinition(0), nullptr);

	const ItemLayout* square = registry.stripLayout(bench, quantaOf(0.0));
	const ItemLayout* turned = registry.stripLayout(bench, quantaOf(45.0));
	ASSERT_NE(square, nullptr);
	ASSERT_NE(turned, nullptr);
	EXPECT_EQ(registry.stripLayout(bench, quantaOf(45.0)), turned) << "kept, not made again";
	EXPECT_EQ(square->layout.strips.size(), 1U);
	EXPECT_EQ(turned->layout.strips.size(), 31U);
	EXPECT_EQ(turned->templateId, bench);
	EXPECT_EQ(turned->turnQuanta, quantaOf(45.0));
	EXPECT_EQ(turned->id, square->id + 1);
	const StripLayout fresh = stripLayout(*registry.getDefinition("Bench"), quantaOf(45.0), viewConfig());
	ASSERT_EQ(fresh.strips.size(), turned->layout.strips.size());
	for (size_t i = 0; i < fresh.strips.size(); ++i) {
		EXPECT_EQ(fresh.strips[i].anchorLocalY, turned->layout.strips[i].anchorLocalY) << "strip " << i;
	}

	EXPECT_EQ(registry.stripLayout(0, 0), nullptr);
	const uint64_t generation = registry.generation();
	ASSERT_NE(load("Bench"), nullptr);
	EXPECT_NE(registry.generation(), generation) << "a clear moves the generation";
	const uint32_t again = registry.templateId("Bench", {});
	EXPECT_EQ(registry.stripLayout(again, quantaOf(45.0))->id, 0U) << "layouts were dropped with the templates and start over";
}

// The gather culls an item by its position, the view padded on each axis by the registry's reach: every strip's box,
// at every turn, lies within that reach of the pivot on each axis, so a turned item at the edge of the view is
// gathered, where the turn-0 template's own reach would cull it.
TEST_F(ItemViewTest, TheCullReachHoldsEveryStripAtEveryTurn) {
	ASSERT_NE(loadBench(3.0F, 1.0F), nullptr);
	AssetRegistry& registry = AssetRegistry::Get();
	const uint32_t id = registry.templateId("Bench", {});
	ASSERT_NE(id, 0U);
	const float						 reach = registry.maxReachMeters();
	const renderer::TessellatedMesh* turnZero = registry.getTemplate(id);
	ASSERT_NE(turnZero, nullptr);
	float templateReach = 0.0F;
	for (const auto& vertex : turnZero->vertices) {
		templateReach = std::max(templateReach, std::hypot(vertex.x, vertex.y));
	}
	const AssetDefinition& bench = *registry.getDefinition("Bench");
	float				   farthest = 0.0F;
	for (int degrees = 0; degrees < 360; degrees += 5) {
		for (const ItemStrip& strip : stripLayout(bench, quantaOf(degrees), viewConfig()).strips) {
			const float extent = std::max({std::abs(strip.minX), std::abs(strip.maxX), std::abs(strip.minY), std::abs(strip.maxY)});
			EXPECT_LE(extent, reach + 1.0e-4F) << degrees << " degrees";
			farthest = std::max(farthest, extent);
		}
	}
	EXPECT_GT(farthest, templateReach) << "height lifts a turned top past the turn-0 template's reach";
}

// A tall second part: a 3 m bench with a 2.2 m chimney standing against its east end. At 90 degrees the chimney is at
// the bench's south end and at 270 at its north end, where its top, lifted 0.77 m, draws farthest from the pivot.
TEST_F(ItemViewTest, TheCullReachHoldsATallSecondPartAtEveryTurn) {
	writeDef(
		"Forge", "<parts>" + block("bench", -1.5F, -0.5F, 1.5F, 0.5F, 0.0F, 0.8F) + block("chimney", 1.5F, -0.15F, 1.8F, 0.15F, 0.0F, 2.2F) +
					 "</parts><placements>" + place("bench") + place("chimney") + "</placements>"
	);
	ASSERT_NE(load("Forge"), nullptr);
	ASSERT_TRUE(issues("Forge").empty()) << ::testing::PrintToString(issues("Forge"));
	AssetRegistry& registry = AssetRegistry::Get();
	ASSERT_NE(registry.templateId("Forge", {}), 0U);
	const float			   reach = registry.maxReachMeters();
	const AssetDefinition& forge = *registry.getDefinition("Forge");
	EXPECT_NEAR(reach, std::hypot(1.8F, 0.15F) + (2.2F * 0.35F) + 0.015F, 1.0e-4F) << "the chimney's top sets the reach";
	for (int degrees = 0; degrees < 360; degrees += 5) {
		for (const ItemStrip& strip : stripLayout(forge, quantaOf(degrees), viewConfig()).strips) {
			EXPECT_LE(std::max({std::abs(strip.minX), std::abs(strip.maxX), std::abs(strip.minY), std::abs(strip.maxY)}), reach + 1.0e-4F)
				<< degrees << " degrees";
		}
	}
	float north = 0.0F;
	for (const ItemStrip& strip : stripLayout(forge, quantaOf(270.0), viewConfig()).strips) {
		north = std::min(north, strip.minY);
	}
	EXPECT_LT(north, -1.8F) << "at 270 degrees the chimney's top stands north past the bench's whole length";
}
