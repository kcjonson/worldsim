#include "PlacedOccluderIndex.h"
#include "SightBody.h"
#include "SightHeight.h"
#include "SightTunables.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>

#include <visibility/ForestShape.h>
#include <visibility/SightClip.h>
#include <visibility/Visibility.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <span>
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

// The kind follows the exact diameter: every tree soft and a tall body 1.40 by 1.07 m opaque at the placed threshold,
// the shortest wall soft and a 2 m wall opaque at the wall threshold.
TEST(SightBody, KindFollowsTheDiameter) {
	const vision::SightTunables start;
	const auto					kindOfRectangle = [](std::int64_t width, std::int64_t height, std::int64_t least) {
		 const geometry::Ring outline = rectangle({0, 0}, {width, height});
		 return vision::kindOf(vision::diameterSquared(outline), least);
	};
	EXPECT_EQ(kindOfRectangle(820, 820, start.placedOpaqueLeastDiameterMm), BodyKind::Soft) << "a veteran oak, 1.16 m across";
	EXPECT_EQ(kindOfRectangle(1400, 1070, start.placedOpaqueLeastDiameterMm), BodyKind::Opaque) << "a tall body 1.40 by 1.07 m, 1.76 m across";
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
	index.add(geometry::nav::kNoRing, clockwise, "Test_TallBody", 0);
	EXPECT_EQ(index.size(), 0U) << "a refused ring never enters";

	index.add(4, clockwise, "Test_TallBody", 250);
	ASSERT_EQ(index.size(), 1U);
	const vision::PlacedOccluder* rock = index.byRing(4);
	ASSERT_NE(rock, nullptr);
	EXPECT_EQ(rock->kind, BodyKind::Opaque);
	EXPECT_EQ(rock->perimeterMm, 2 * (1400 + 1070));
	EXPECT_EQ(rock->diameterMm, 1762);
	EXPECT_EQ(rock->arrivedMs, 250);
	EXPECT_EQ(rock->defName, "Test_TallBody");
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
	index.add(4, clockwise, "Test_TallBody", 750);
	ASSERT_EQ(index.size(), 1U);
	index.within({0, 0}, 1000, found);
	EXPECT_TRUE(found.empty()) << "the replaced ring left its buckets";
	index.within({15000, 500}, 600, found);
	EXPECT_EQ(found.size(), 1U);
}

// A concave ring is opaque whatever its diameter, and stays so when the threshold moves; a convex one
// as small is soft (sight-softening.md 2.2).
TEST(PlacedOccluderIndex, AConcaveRingIsOpaqueWhateverItsSize) {
	vision::PlacedOccluderIndex index;
	// An L 0.6 m on each arm, 0.85 m across: under the placed threshold.
	index.add(1, {{0, 0}, {600, 0}, {600, 300}, {300, 300}, {300, 600}, {0, 600}}, "Test_TallBody", 0);
	index.add(2, rectangle({2000, 0}, {2600, 600}), "Test_TallBody", 0);
	ASSERT_EQ(index.size(), 2U);
	EXPECT_TRUE(index.byRing(1)->concave);
	EXPECT_EQ(index.byRing(1)->kind, BodyKind::Opaque);
	EXPECT_FALSE(index.byRing(2)->concave);
	EXPECT_EQ(index.byRing(2)->kind, BodyKind::Soft);
	index.setOpaqueLeastDiameterMm(2000);
	EXPECT_EQ(index.byRing(1)->kind, BodyKind::Opaque) << "a threshold slider never makes it soft";
}

// A placed body is a sight body when its top reaches the eye (sight-softening.md 2.2): a simple def by
// its worldHeight, the threshold itself reaching it; a procedural def states no height and is tall.
TEST(SightHeight, ABodyReachesTheEyeFromItsDefsHeight) {
	const auto simple = [](float worldHeight) {
		assets::AssetDefinition def;
		def.assetType	= assets::AssetType::Simple;
		def.worldHeight = worldHeight;
		return def;
	};
	EXPECT_EQ(vision::kEyeHeightMm, 1600);
	EXPECT_FALSE(vision::reachesEyeHeight(simple(1.599F))) << "just under";
	EXPECT_TRUE(vision::reachesEyeHeight(simple(1.6F))) << "at it";
	EXPECT_TRUE(vision::reachesEyeHeight(simple(1.601F))) << "just over";
	EXPECT_EQ(vision::bodyTopMm(simple(1.5F)), 1500) << "a BigRock";
	EXPECT_FALSE(vision::reachesEyeHeight(simple(1.5F))) << "a BigRock";
	EXPECT_TRUE(vision::reachesEyeHeight(simple(2.0F))) << "a TestTallBox";

	assets::AssetDefinition unwritten;
	unwritten.assetType = assets::AssetType::Simple;
	EXPECT_EQ(vision::bodyTopMm(unwritten), 1000) << "the parser's default height";
	EXPECT_FALSE(vision::reachesEyeHeight(unwritten));

	assets::AssetDefinition procedural;
	procedural.assetType   = assets::AssetType::Procedural;
	procedural.worldHeight = 0.5F;
	EXPECT_FALSE(vision::bodyTopMm(procedural).has_value()) << "its generator draws to its own size";
	EXPECT_TRUE(vision::reachesEyeHeight(procedural)) << "a tree trunk stays a sight body";
}

// The shipped defs: a BigRock is low, a TestTallBox and an oak reach the eye.
TEST(SightHeight, ShippedBodies) {
	const std::filesystem::path repo	 = SIM_TEST_REPO_DIR;
	assets::AssetRegistry&		registry = assets::AssetRegistry::Get();
	registry.clearDefinitions();
	registry.setSharedScriptsPath(repo / "assets" / "shared" / "scripts");
	for (const char* xml : {"misc/BigRock/BigRock.xml", "misc/TestTallBox/TestTallBox.xml", "flora/OakTree/OakTree.xml"}) {
		ASSERT_TRUE(registry.loadDefinitions((repo / "assets" / "world" / xml).string())) << xml;
	}
	EXPECT_FALSE(vision::reachesEyeHeight(*registry.getDefinition("BigRock")));
	EXPECT_TRUE(vision::reachesEyeHeight(*registry.getDefinition("TestTallBox")));
	EXPECT_TRUE(vision::reachesEyeHeight(*registry.getDefinition("Flora_TreeOak")));
	registry.clearDefinitions();
}

namespace {

	// A 4 x 3 m room of 0.2 m walls, its doorway 1 m wide in the south wall from x 1.5 to 2.5 m, as one
	// outline: the outer side, in through the doorway, round the inner side, and out. The west outer face
	// is three edges, a junction's short end at each corner and the wall between.
	SightBody roomWithADoorway() {
		SightBody room = placedBody(
			{{-100, -100},
			 {4100, -100},
			 {4100, 3100},
			 {2500, 3100},
			 {2500, 2900},
			 {3900, 2900},
			 {3900, 100},
			 {100, 100},
			 {100, 2900},
			 {1500, 2900},
			 {1500, 3100},
			 {-100, 3100},
			 {-100, 2900},
			 {-100, 100}},
			BodyKind::Opaque
		);
		room.faces = vision::facesOf(room.edges, room.previousEdge);
		return room;
	}

} // namespace

// A face is the whole straight run of an outline's edges, so the west face of three edges is one face,
// and the south face either side of the doorway is two. The faces do not depend on which edge the
// outline lists first.
TEST(SightBody, AFaceIsTheWholeStraightRunOfEdges) {
	const SightBody room = roomWithADoorway();
	ASSERT_EQ(room.faces.size(), 12U) << "fourteen edges, the west face's three as one";
	const auto has = [&](const std::vector<vision::Face>& faces, Vec2i64 from, Vec2i64 to) {
		return std::ranges::any_of(faces, [&](const vision::Face& face) { return face.from == from && face.to == to; });
	};
	EXPECT_TRUE(has(room.faces, {-100, 3100}, {-100, -100})) << "the west face";
	EXPECT_TRUE(has(room.faces, {4100, 3100}, {2500, 3100})) << "the south face stops at the doorway";
	EXPECT_TRUE(has(room.faces, {1500, 3100}, {-100, 3100}));
	for (std::uint32_t shift = 1; shift < room.edges.size(); ++shift) {
		std::vector<geometry::OccluderSegment> edges;
		std::vector<std::uint32_t>			   previous;
		const auto							   count = static_cast<std::uint32_t>(room.edges.size());
		for (std::uint32_t edge = 0; edge < count; ++edge) {
			edges.push_back(room.edges[(edge + shift) % count]);
			previous.push_back((edge + count - 1) % count);
		}
		const std::vector<vision::Face> shifted = vision::facesOf(edges, previous);
		EXPECT_EQ(shifted.size(), room.faces.size()) << "starting at edge " << shift;
		for (const vision::Face& face : room.faces) {
			EXPECT_TRUE(has(shifted, face.from, face.to)) << "starting at edge " << shift;
		}
	}
}

namespace {

	// Whether the side from `from` to `to` is rim, as the first side of a triangle seen from 10 m off
	// its middle, square on, so it is no shadow side.
	bool sideIsRim(std::span<const SightBody> bodies, std::span<const std::span<const Vec2i64>> rings, Vec2i64 from, Vec2i64 to) {
		const Vec2i64		 middle{(from.x + to.x) / 2, (from.y + to.y) / 2};
		const double		 length = std::hypot(static_cast<double>(to.x - from.x), static_cast<double>(to.y - from.y));
		const Vec2i64		 eye{middle.x + std::llround(-10000.0 * static_cast<double>(to.y - from.y) / length), middle.y + std::llround(10000.0 * static_cast<double>(to.x - from.x) / length)};
		const std::vector<Vec2i64> outline = {from, to, eye};
		std::vector<bool>	 rim;
		vision::tagRimSides(outline, eye, bodies, rings, rim);
		return rim[0];
	}

} // namespace

// A side is a body face when both its ends are within the clips' drift of one face (sight-softening.md
// 7.2): the whole west face, and the face with its ends moved 20 mm off; not a side running on past the
// corner into open air, one 40 mm off, one across the doorway between the south face's two runs, nor
// one that leaves the face across open ground.
TEST(SightBody, ASideIsAFaceWithinTheClipsDriftAndNoFurther) {
	const std::vector<SightBody> bodies = {roomWithADoorway()};
	// Half a millimetre's diagonal for the snap, then three clips each of that and 8 steps of 1 mm.
	EXPECT_EQ(vision::kFaceDriftMm, 27);
	EXPECT_FALSE(sideIsRim(bodies, {}, {-100, 3100}, {-100, -100}));
	EXPECT_FALSE(sideIsRim(bodies, {}, {-120, 3000}, {-80, 0}));
	EXPECT_TRUE(sideIsRim(bodies, {}, {-100, 3100}, {-100, -1000})) << "on past the corner";
	EXPECT_TRUE(sideIsRim(bodies, {}, {-140, 3000}, {-140, 0})) << "40 mm off";
	EXPECT_TRUE(sideIsRim(bodies, {}, {3000, 3100}, {1000, 3100})) << "across the doorway";
	EXPECT_TRUE(sideIsRim(bodies, {}, {-100, 2000}, {-2000, 0})) << "off across the ground";
}

// A cutting ring that crosses the west face at 18 degrees leaves a sliver of rim beside the crossing,
// from the crossing to the ring's next corner, which lies within the drift of the face end to end: it
// is rim, whichever of the rings that cut the shape it runs along (the drawn disc, the eased rim, or
// vision's forest rim), and taken for the face only with that ring left out.
TEST(SightBody, ARimSliverBesideAShallowCrossingIsRim) {
	const std::vector<SightBody> bodies = {roomWithADoorway()};
	const double				 turn = 18.0 * std::numbers::pi / 180.0;
	// Down the west face from (-100, 1000) mm, turned 18 degrees off it, away from the room.
	const glm::dvec2 along{-std::sin(turn), -std::cos(turn)};
	const auto		 at = [&](double distance) { return Vec2i64{std::llround(-100.0 + distance * along.x), std::llround(1000.0 + distance * along.y)}; };
	// The ring: its side through the crossing, its corner 80 mm on (24.7 mm off the face), then round.
	const std::vector<Vec2i64> cutting = {at(-3000.0), at(80.0), {-6000, -6000}, {-9000, 0}};
	const std::vector<Vec2i64> other   = {{-50000, -50000}, {50000, -50000}, {50000, 50000}, {-50000, 50000}};
	const Vec2i64			   crossing = at(0.0);
	const Vec2i64			   corner	= at(80.0);
	EXPECT_FALSE(sideIsRim(bodies, {}, crossing, corner)) << "no ring: within the drift of the face";
	for (std::size_t place = 0; place < 3; ++place) {
		std::vector<std::span<const Vec2i64>> rings = {other, other, other};
		rings[place]								= cutting;
		EXPECT_TRUE(sideIsRim(bodies, rings, crossing, corner)) << "the cutting ring in place " << place;
	}
	// A true face side between two crossings is no ring's side.
	const std::vector<std::span<const Vec2i64>> rings = {cutting, other, other};
	EXPECT_FALSE(sideIsRim(bodies, rings, crossing, {-100, 3000}));
}
