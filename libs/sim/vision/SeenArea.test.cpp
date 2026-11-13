#include "SeenArea.h"

#include <nav/NavMesh.h>
#include <nav/PatchableMesh.h>
#include <visibility/SightPolygon.h>
#include <visibility/Visibility.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

using geometry::Vec2i64;
using vision::SeenArea;

namespace {

	constexpr std::int64_t kSightMm = 30000;

	using Edge = std::pair<Vec2i64, Vec2i64>;

	// The union's edge: every triangle side with no triangle across it.
	std::set<Edge> boundaryOf(const std::vector<SeenArea::Triangle>& triangles) {
		std::set<Edge> sides;
		for (const SeenArea::Triangle& triangle : triangles) {
			for (std::size_t k = 0; k < 3; ++k) {
				sides.insert({triangle.corners[k], triangle.corners[(k + 1) % 3]});
			}
		}
		std::set<Edge> out;
		for (const Edge& side : sides) {
			if (!sides.contains({side.second, side.first})) {
				out.insert(side);
			}
		}
		return out;
	}

	std::set<Vec2i64> cornersOf(const std::vector<SeenArea::Triangle>& triangles) {
		std::set<Vec2i64> out;
		for (const SeenArea::Triangle& triangle : triangles) {
			out.insert(triangle.corners.begin(), triangle.corners.end());
		}
		return out;
	}

	std::vector<SeenArea::Triangle> trianglesOf(const SeenArea& seen) {
		std::vector<SeenArea::Triangle> out;
		seen.appendTriangles(out);
		return out;
	}

	std::vector<SeenArea::Triangle> trianglesOf(const geometry::nav::PatchableMesh& mesh) {
		std::vector<SeenArea::Triangle> out;
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(mesh.triangles().size()); ++t) {
			if (mesh.triangleLive(t)) {
				const geometry::nav::NavTriangle& triangle = mesh.triangles()[static_cast<std::size_t>(t)];
				out.push_back({{mesh.vertices()[triangle.v[0]], mesh.vertices()[triangle.v[1]], mesh.vertices()[triangle.v[2]]}});
			}
		}
		return out;
	}

	void drain(SeenArea& seen) {
		for (int tick = 0; tick < 1000 && seen.shapesPending() > 0; ++tick) {
			seen.update();
		}
		ASSERT_EQ(seen.shapesPending(), 0U);
	}

	Vec2i64 metres(double x, double y) { return {std::llround(x * 1000.0), std::llround(y * 1000.0)}; }

	// A square of `halfMetres` round (x, y) metres, counter-clockwise.
	std::vector<Vec2i64> square(double x, double y, double halfMetres) {
		return {metres(x - halfMetres, y - halfMetres), metres(x + halfMetres, y - halfMetres), metres(x + halfMetres, y + halfMetres),
				metres(x - halfMetres, y + halfMetres)};
	}

} // namespace

// The landing's polygon goes in at once, as one ring, and holds what it is inscribed in.
TEST(SeenArea, LandingIsOneRingInscribedInItsCircle) {
	SeenArea	  seen;
	const Vec2i64 drop = metres(1234.5, -987.25);
	seen.commitNow(geometry::sightPolygon(drop, 150000), drop);
	EXPECT_EQ(seen.ringsRegistered(), 1U);
	EXPECT_EQ(seen.shapesPending(), 0U);
	const double circle = std::numbers::pi * 150.0 * 150.0;
	EXPECT_LT(seen.committedSquareMeters(), circle);
	EXPECT_GT(seen.committedSquareMeters(), circle * 0.99);
	for (int k = 0; k < 16; ++k) {
		const double angle = 2.0 * std::numbers::pi * k / 16.0;
		EXPECT_TRUE(seen.isSeen(drop + metres(149.0 * std::cos(angle), 149.0 * std::sin(angle)))) << "direction " << k;
		EXPECT_FALSE(seen.isSeen(drop + metres(150.01 * std::cos(angle), 150.01 * std::sin(angle)))) << "direction " << k;
	}
}

// A queued shape is seen, and covers what it holds, before the budget commits it; the budget takes
// kSeenRingsPerTick shapes a tick, first in, first out.
TEST(SeenArea, PendingShapesAnswerBeforeTheyAreCommitted) {
	SeenArea seen;
	for (int i = 0; i < 6; ++i) {
		const Vec2i64 centre = metres(100.0 * i, 0.0);
		seen.queue(geometry::sightPolygon(centre, kSightMm), centre);
	}
	EXPECT_EQ(seen.shapesPending(), 6U);
	EXPECT_EQ(seen.ringsRegistered(), 0U);
	EXPECT_TRUE(seen.isSeen(metres(500.0, 20.0)));
	EXPECT_TRUE(seen.covers(square(500.0, 0.0, 10.0)));
	EXPECT_FALSE(seen.isSeen(metres(50.0, 0.0)));

	const std::uint64_t before = seen.epoch();
	seen.update();
	EXPECT_EQ(seen.ringsRegistered(), vision::kSeenRingsPerTick);
	EXPECT_EQ(seen.shapesPending(), 6U - vision::kSeenRingsPerTick);
	EXPECT_NE(seen.epoch(), before);
	EXPECT_EQ(seen.registeredRing(0).front(), geometry::sightPolygon(metres(0.0, 0.0), kSightMm).front()) << "first in, first out";
	EXPECT_TRUE(seen.isSeen(metres(500.0, 20.0))) << "still pending, still seen";
	drain(seen);
	EXPECT_EQ(seen.ringsRegistered(), 6U);
	EXPECT_TRUE(seen.isSeen(metres(500.0, 20.0)));
}

// A shape the record already holds is dropped, not registered.
TEST(SeenArea, CoveredShapeIsDropped) {
	SeenArea	  seen;
	const Vec2i64 centre = metres(10.0, 10.0);
	seen.commitNow(geometry::sightPolygon(centre, 150000), centre);
	seen.queue(vision::legCapsule(metres(0.0, 0.0), metres(60.0, 30.0), kSightMm - vision::kSeenSlackMm), metres(0.0, 0.0));
	const std::uint64_t before = seen.epoch();
	drain(seen);
	EXPECT_EQ(seen.ringsRegistered(), 1U);
	EXPECT_EQ(seen.shapesDropped(), 1U);
	EXPECT_EQ(seen.epoch(), before);
}

// An outline across a committed ring and a pending one is covered; one reaching past both is not.
TEST(SeenArea, CoversAcrossACommittedRingAndAPendingOne) {
	SeenArea	  seen;
	const Vec2i64 west = metres(0.0, 0.0);
	const Vec2i64 east = metres(40.0, 0.0);
	seen.commitNow(geometry::sightPolygon(west, kSightMm), west);
	seen.setOpenLeg(1, east, east, kSightMm);
	const std::vector<Vec2i64> across = square(20.0, 0.0, 10.0);
	EXPECT_TRUE(seen.covers(across)) << "half in the record, half in the open leg";
	EXPECT_FALSE(seen.covers(square(20.0, 25.0, 10.0))) << "reaches past both";
	EXPECT_FALSE(seen.covers(square(-25.0, 0.0, 10.0))) << "reaches past the record alone";

	seen.closeLeg(1);
	EXPECT_TRUE(seen.covers(across)) << "queued";
	drain(seen);
	EXPECT_EQ(seen.ringsRegistered(), 2U);
	EXPECT_TRUE(seen.covers(across)) << "committed";
	EXPECT_FALSE(seen.covers(square(20.0, 25.0, 10.0)));
}

// lastSeenToward: `to` when it is seen; the far end of the last seen stretch on a segment that leaves
// the record, re-enters it (here through a pending shape), and leaves again; the same on one that
// starts outside; none when nothing on it is seen.
TEST(SeenArea, LastSeenTowardTheGoal) {
	SeenArea	  seen;
	const Vec2i64 west = metres(0.0, 0.0);
	seen.commitNow(geometry::sightPolygon(west, 10000), west);
	const Vec2i64 east = metres(40.0, 0.0);
	seen.queue(geometry::sightPolygon(east, 10000), east);

	const auto near = [](const std::optional<Vec2i64>& point, const Vec2i64& expected) {
		return point.has_value() && std::abs(point->x - expected.x) <= 2 && std::abs(point->y - expected.y) <= 2;
	};

	EXPECT_EQ(seen.lastSeenToward(west, metres(5.0, 0.0)), std::optional(metres(5.0, 0.0))) << "seen goal";
	EXPECT_EQ(seen.lastSeenToward(west, metres(45.0, 0.0)), std::optional(metres(45.0, 0.0))) << "seen in a pending shape";

	const std::optional<Vec2i64> twice = seen.lastSeenToward(west, metres(80.0, 0.0));
	EXPECT_TRUE(near(twice, metres(50.0, 0.0))) << "leaves, re-enters, leaves: " << (twice ? twice->x : -1);
	EXPECT_TRUE(twice.has_value() && seen.isSeen(*twice));

	const std::optional<Vec2i64> between = seen.lastSeenToward(west, metres(20.0, 0.0));
	EXPECT_TRUE(near(between, metres(10.0, 0.0))) << (between ? between->x : -1);

	const std::optional<Vec2i64> outside = seen.lastSeenToward(metres(-30.0, 0.0), metres(25.0, 0.0));
	EXPECT_TRUE(near(outside, metres(10.0, 0.0))) << "starts outside: " << (outside ? outside->x : -1);
	EXPECT_TRUE(outside.has_value() && seen.isSeen(*outside));

	const std::optional<Vec2i64> slanted = seen.lastSeenToward(metres(-3.0, -1.0), metres(17.0, 13.0));
	ASSERT_TRUE(slanted.has_value());
	EXPECT_TRUE(seen.isSeen(*slanted));
	EXPECT_FALSE(seen.isSeen(*slanted + Vec2i64{20, 14})) << "a few millimetres on toward the goal is not seen";

	EXPECT_FALSE(seen.lastSeenToward(metres(0.0, 50.0), metres(40.0, 50.0)).has_value()) << "nothing seen on it";

	drain(seen);
	EXPECT_TRUE(near(seen.lastSeenToward(west, metres(80.0, 0.0)), metres(50.0, 0.0))) << "the same once committed";
}

// Visibility polygons from viewpoints 0.5 m apart walking past and through a doorway, each committed
// unless covered: the record equals a fresh build of the rings it registered.
TEST(SeenArea, DoorwayPolygonsEqualAFreshBuild) {
	const std::vector<geometry::OccluderSegment> walls = {
		{metres(-20.0, 0.0), metres(-0.5, 0.0)},
		{metres(0.5, 0.0), metres(20.0, 0.0)},
		{metres(-0.5, 0.0), metres(-0.5, 0.2)},
		{metres(0.5, 0.0), metres(0.5, 0.2)},
		{metres(-20.0, 0.2), metres(-0.5, 0.2)},
		{metres(0.5, 0.2), metres(20.0, 0.2)},
	};
	std::vector<Vec2i64> viewpoints;
	for (int step = 0; step <= 24; ++step) {
		viewpoints.push_back(metres(0.0, -6.0 + 0.5 * step));
	}
	for (int step = 0; step <= 20; ++step) {
		viewpoints.push_back(metres(-6.0 + 0.5 * step, -0.4));
	}
	for (int step = 0; step <= 16; ++step) {
		viewpoints.push_back(metres(-4.0 + 0.5 * step, -4.0 + 0.5 * step));
	}

	SeenArea seen;
	int		 notSimple = 0;
	for (const Vec2i64& viewpoint : viewpoints) {
		const geometry::Ring polygon = geometry::computeVisibilityPolygon(viewpoint, kSightMm, walls);
		if (!geometry::simpleAround(polygon, viewpoint)) {
			++notSimple;
			continue;
		}
		seen.queue(polygon, viewpoint);
		seen.update();
	}
	drain(seen);
	EXPECT_EQ(notSimple, 0);
	EXPECT_GT(seen.ringsRegistered(), 10U);
	EXPECT_GT(seen.shapesDropped(), 0U);

	geometry::nav::PatchableMesh fresh;
	geometry::nav::PatchEdits	 edits;
	for (std::size_t i = 0; i < seen.ringsRegistered(); ++i) {
		edits.adds.push_back(geometry::nav::NavInputPolygon{.ring = seen.registeredRing(i)});
	}
	fresh.applyPatch(edits);
	EXPECT_EQ(cornersOf(trianglesOf(seen)), cornersOf(trianglesOf(fresh)));
	EXPECT_EQ(boundaryOf(trianglesOf(seen)), boundaryOf(trianglesOf(fresh)));
	for (const Vec2i64& viewpoint : viewpoints) {
		EXPECT_TRUE(seen.isSeen(viewpoint));
	}
	EXPECT_FALSE(seen.isSeen(metres(-25.0, 25.0))) << "north of the wall, beyond every viewpoint north of it and out of line through the door";
}
