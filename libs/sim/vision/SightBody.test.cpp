#include "PlacedOccluderIndex.h"
#include "SightBody.h"
#include "SightTunables.h"

#include <visibility/ForestShape.h>
#include <visibility/SightClip.h>
#include <visibility/Visibility.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include <gtest/gtest.h>

using geometry::Vec2i64;
using vision::BodyKind;
using vision::SightBody;

namespace {

	// The axis-aligned rectangle from `low` to `high`, counter-clockwise.
	geometry::Ring rectangle(Vec2i64 low, Vec2i64 high) { return {low, {high.x, low.y}, high, {low.x, high.y}}; }

	SightBody placedBody(const geometry::Ring& outline, BodyKind kind) {
		SightBody		   body{.source = SightBody::Source::Placed, .kind = kind};
		const auto		   corners = static_cast<std::uint32_t>(outline.size());
		for (std::uint32_t corner = 0; corner < corners; ++corner) {
			body.edges.push_back({outline[corner], outline[(corner + 1) % corners]});
			body.previousEdge.push_back((corner + corners - 1) % corners);
		}
		return body;
	}

} // namespace

// Out-of-range values clamp to their fixed bounds, and the soft ray width is pushed above the larger
// kind threshold rather than holding the threshold down.
TEST(SightTunables, ClampToTheirRangesWithTheRayWidthAboveBothThresholds) {
	EXPECT_EQ(vision::clampedToRanges({}), vision::SightTunables{}) << "the start values lie in range";
	vision::SightTunables wild;
	wild.forestWindowSteps	   = 40;
	wild.rimEaseMm			   = 0;
	wild.forestNearMm		   = 90000;
	wild.softShadowRebuildPercent = -5;
	wild.softShadowToleranceThousandths = 100;
	const vision::SightTunables clamped = vision::clampedToRanges(wild);
	EXPECT_EQ(clamped.forestWindowSteps, 32);
	EXPECT_EQ(clamped.rimEaseMm, 100);
	EXPECT_EQ(clamped.forestNearMm, 30000);
	EXPECT_EQ(clamped.softShadowRebuildPercent, 1);
	EXPECT_EQ(clamped.softShadowToleranceThousandths, 30);

	vision::SightTunables thresholds;
	thresholds.placedOpaqueLeastDiameterMm = 5000;
	thresholds.softShadowRayWidthMm		   = 1500;
	const vision::SightTunables raised	   = vision::clampedToRanges(thresholds);
	EXPECT_EQ(raised.placedOpaqueLeastDiameterMm, 2000);
	EXPECT_EQ(raised.softShadowRayWidthMm, 2001);
}

// The kind follows the exact diameter: every tree soft and a BigRock opaque at the placed threshold,
// the shortest wall soft and a 2 m wall opaque at the wall threshold.
TEST(SightBody, KindFollowsTheDiameter) {
	const vision::SightTunables start;
	const auto					kindOfRectangle = [](std::int64_t width, std::int64_t height, std::int64_t least) {
		 const geometry::Ring outline = rectangle({0, 0}, {width, height});
		 return vision::kindOf(vision::diameterSquared(outline), least);
	};
	EXPECT_EQ(kindOfRectangle(820, 820, start.placedOpaqueLeastDiameterMm), BodyKind::Soft) << "a veteran oak, 1.16 m across";
	EXPECT_EQ(kindOfRectangle(1400, 1070, start.placedOpaqueLeastDiameterMm), BodyKind::Opaque) << "a BigRock, 1.76 m across";
	EXPECT_EQ(kindOfRectangle(500, 150, start.wallOpaqueLeastDiameterMm), BodyKind::Soft) << "the shortest wall";
	EXPECT_EQ(kindOfRectangle(2000, 150, start.wallOpaqueLeastDiameterMm), BodyKind::Opaque);
	EXPECT_EQ(kindOfRectangle(600, 0, 600), BodyKind::Opaque) << "the threshold itself is opaque";
	EXPECT_EQ(vision::wholeSquareRoot(vision::diameterSquared(rectangle({0, 0}, {1400, 1070}))), 1762);
	EXPECT_EQ(vision::wholeSquareRoot(geometry::Int128(1000000)), 1000);
	EXPECT_EQ(vision::wholeSquareRoot(geometry::Int128(999999)), 999);
}

// A 1 m square 5 m off: face on its silhouette runs between its near corners and it is 1 m wide as
// seen; at its diagonal it is its diagonal wide; from inside its hull there is no silhouette.
TEST(SightBody, SilhouetteWidthAndDistanceFromAnEye) {
	const SightBody square = placedBody(rectangle({5000, -500}, {6000, 500}), BodyKind::Soft);
	const auto		silhouette = vision::silhouetteFrom(square, {0.0, 0.0});
	ASSERT_TRUE(silhouette.has_value());
	EXPECT_EQ(square.edges[silhouette->clockwise].a, (Vec2i64{5000, -500}));
	EXPECT_EQ(square.edges[silhouette->counterClockwise].a, (Vec2i64{5000, 500}));
	EXPECT_NEAR(*vision::widthAsSeen(square, {0.0, 0.0}), 1000.0, 1e-6);
	EXPECT_NEAR(vision::distanceTo(square, {0.0, 0.0}), 5000.0, 1e-6);
	EXPECT_NEAR(vision::cornerDistance(square, silhouette->clockwise, {0.0, 0.0}), std::hypot(5000.0, 500.0), 1e-6);

	EXPECT_NEAR(*vision::widthAsSeen(square, {0.0, -5500.0}), 1000.0 * std::sqrt(2.0), 1e-6) << "at its diagonal";
	EXPECT_FALSE(vision::silhouetteFrom(square, {5500.0, 0.0}).has_value()) << "inside it";
}

// The shadow edges of a polygon round an opaque square: one from each near corner, the lit side
// counter-clockwise of the one running out (+1) and clockwise of the one running in (-1); a soft body
// in the same list tags nothing.
TEST(SightBody, ShadowEdgesTagTheSilhouetteCorners) {
	const Vec2i64					   eye{0, 0};
	const std::vector<SightBody>	   bodies = {placedBody(rectangle({5000, -500}, {6000, 500}), BodyKind::Opaque),
												 placedBody(rectangle({-500, 5000}, {500, 6000}), BodyKind::Soft)};
	std::vector<geometry::OccluderSegment> occluders = bodies[0].edges;
	const geometry::Ring				   polygon	  = geometry::computeVisibilityPolygon(eye, 30000, occluders);
	const std::vector<vision::ShadowEdge>  tagged	  = vision::tagShadowEdges(polygon, eye, bodies);
	ASSERT_EQ(tagged.size(), 2U);
	for (const vision::ShadowEdge& edge : tagged) {
		EXPECT_EQ(edge.body, 0U);
		const Vec2i64 corner = bodies[0].edges[edge.corner].a;
		if (corner == Vec2i64{5000, 500}) {
			EXPECT_EQ(edge.litTurn, 1) << "the upper side runs out with the lit side counter-clockwise";
		} else {
			EXPECT_EQ(corner, (Vec2i64{5000, -500}));
			EXPECT_EQ(edge.litTurn, -1);
		}
	}
	EXPECT_TRUE(vision::tagShadowEdges(polygon, eye, std::vector<SightBody>{bodies[1]}).empty());
}

// The polygon path's cut: a rock's shadow edges survive a forest shape that holds their corners; a
// hard polygon with a vertex on the eye cannot be cut, and it serves as it is with every tag, so the
// observer is never left with an empty shape.
TEST(SightBody, ForestCutKeepsCornersInsideAndAnUncuttablePolygonServesAsItIs) {
	const Vec2i64				 eye{0, 0};
	const std::vector<SightBody> bodies = {placedBody(rectangle({5000, -500}, {6000, 500}), BodyKind::Opaque)};
	std::vector<geometry::OccluderSegment> occluders = bodies[0].edges;
	const geometry::Ring				   hard		 = geometry::computeVisibilityPolygon(eye, 30000, occluders);
	geometry::SightSideValues			   ranges{};
	ranges.fill(25000);
	const geometry::Ring forest = geometry::forestShape(eye, ranges);
	const vision::ForestCut cut = vision::cutByForest(hard, eye, forest, vision::tagShadowEdges(hard, eye, bodies), bodies);
	EXPECT_TRUE(cut.cut);
	EXPECT_EQ(cut.polygon, geometry::starClippedToConvex(hard, eye, forest));
	EXPECT_EQ(cut.shadowEdges.size(), 2U) << "both corners lie inside the forest shape";

	const geometry::Ring			  onEye = {eye, {20000, -10000}, {20000, 10000}, {-10000, 10000}};
	const std::vector<vision::ShadowEdge> tags	= {{.body = 0, .corner = 1, .litTurn = 1}};
	ASSERT_TRUE(geometry::starClippedToConvex(onEye, eye, forest).empty());
	const vision::ForestCut uncut = vision::cutByForest(onEye, eye, forest, tags, bodies);
	EXPECT_FALSE(uncut.cut);
	EXPECT_EQ(uncut.polygon, onEye);
	EXPECT_EQ(uncut.shadowEdges, tags);
}

// The index stores a ring counter-clockwise with its measures and a serial, never takes a refused
// ring, finds a ring across a bucket's edge from either side, re-decides kinds when the threshold
// moves, and gives a ring id handed out again a new serial with one entry.
TEST(PlacedOccluderIndex, AddsMeasuresFindsAndRemovesBySerial) {
	vision::PlacedOccluderIndex index;
	geometry::Ring				clockwise = rectangle({15500, 0}, {16900, 1070});
	std::reverse(clockwise.begin(), clockwise.end());
	index.add(geometry::nav::kNoRing, clockwise, "BigRock", 0);
	EXPECT_EQ(index.size(), 0U) << "a refused ring never enters";

	index.add(4, clockwise, "BigRock", 250);
	ASSERT_EQ(index.size(), 1U);
	const vision::PlacedOccluder* rock = index.byRing(4);
	ASSERT_NE(rock, nullptr);
	EXPECT_EQ(rock->kind, BodyKind::Opaque);
	EXPECT_EQ(rock->perimeterMm, 2 * (1400 + 1070));
	EXPECT_EQ(rock->diameterMm, 1762);
	EXPECT_EQ(rock->arrivedMs, 250);
	EXPECT_EQ(rock->defName, "BigRock");
	EXPECT_GT(geometry::cross(rock->outline[1] - rock->outline[0], rock->outline[2] - rock->outline[1]).sign(), 0) << "counter-clockwise";
	const std::uint64_t first = rock->serial;

	std::vector<const vision::PlacedOccluder*> found;
	index.within({15000, 500}, 600, found);
	EXPECT_EQ(found.size(), 1U) << "from the bucket west of 16 m";
	index.within({17500, 500}, 700, found);
	EXPECT_EQ(found.size(), 1U) << "from the bucket east of it";
	index.within({20000, 500}, 1000, found);
	EXPECT_TRUE(found.empty());

	index.setOpaqueLeastDiameterMm(2000);
	EXPECT_EQ(index.byRing(4)->kind, BodyKind::Soft);

	index.remove(4);
	EXPECT_EQ(index.size(), 0U);
	index.add(4, rectangle({0, 0}, {280, 280}), "Flora_TreePine", 500);
	ASSERT_EQ(index.size(), 1U);
	EXPECT_NE(index.byRing(4)->serial, first) << "serials never repeat";
	EXPECT_EQ(index.byRing(4)->kind, BodyKind::Soft);
	index.within({15000, 500}, 600, found);
	EXPECT_TRUE(found.empty()) << "the old ring left its buckets";

	// An id handed out again while still listed replaces its entry, buckets and all.
	index.add(4, clockwise, "BigRock", 750);
	ASSERT_EQ(index.size(), 1U);
	index.within({0, 0}, 1000, found);
	EXPECT_TRUE(found.empty()) << "the replaced ring left its buckets";
	index.within({15000, 500}, 600, found);
	EXPECT_EQ(found.size(), 1U);
}
