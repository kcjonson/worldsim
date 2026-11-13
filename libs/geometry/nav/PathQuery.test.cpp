#include "PathQuery.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"
#include "LimitPolygon.test.h"
#include "NavMesh.h"
#include "PatchableMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;
using namespace geometry::nav;

namespace {

	NavInputPolygon border(std::vector<Vec2i64> ring) {
		return {std::move(ring), false, 1};
	}

	NavInputPolygon blocked(std::vector<Vec2i64> ring, std::int64_t id) {
		return {std::move(ring), true, id};
	}

	// A blocked ring that is a pathable door's footprint: provenance = segment id,
	// openingId = the door. Truth-walkable; belief-gated by knowing both.
	NavInputPolygon doorSpan(std::vector<Vec2i64> ring, std::int64_t segId, std::int64_t openingId) {
		return {std::move(ring), true, segId, openingId};
	}

	// True if segment [a,b] properly crosses or touches any edge of `ring`.
	bool segmentHitsRing(const Vec2i64& a, const Vec2i64& b, const std::vector<Vec2i64>& ring) {
		const std::size_t n = ring.size();
		for (std::size_t i = 0; i < n; ++i) {
			const Vec2i64&		 r0 = ring[i];
			const Vec2i64&		 r1 = ring[(i + 1) % n];
			SegmentIntersection	 si = intersectSegments(a, b, r0, r1);
			if (si.relation == SegmentRelation::ProperCrossing || si.relation == SegmentRelation::CollinearOverlap) {
				return true;
			}
		}
		return false;
	}

	bool pathHitsRing(const PathResult& path, const std::vector<Vec2i64>& ring) {
		for (std::size_t i = 0; i + 1 < path.points.size(); ++i) {
			if (segmentHitsRing(path.points[i], path.points[i + 1], ring)) {
				return true;
			}
		}
		return false;
	}

	// Minimum float distance from any path vertex to any edge of `ring`.
	double minVertexDistanceToRing(const PathResult& path, const std::vector<Vec2i64>& ring) {
		double			  best = 1e30;
		const std::size_t n	   = ring.size();
		for (const Vec2i64& p : path.points) {
			for (std::size_t i = 0; i < n; ++i) {
				const double d = distanceToSegment(p, ring[i], ring[(i + 1) % n]);
				if (d < best) {
					best = d;
				}
			}
		}
		return best;
	}

} // namespace

TEST(PathQuery, LocateInsideReturnsContainingTriangle) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 p{500, 500};
	std::int32_t  ti = locateTriangle(m, p);
	ASSERT_GE(ti, 0);

	// The returned triangle actually contains p (no edge sees it as Clockwise).
	const NavTriangle& t = m.triangles[ti];
	EXPECT_NE(orientation(m.vertices[t.v[0]], m.vertices[t.v[1]], p), Orientation::Clockwise);
	EXPECT_NE(orientation(m.vertices[t.v[1]], m.vertices[t.v[2]], p), Orientation::Clockwise);
	EXPECT_NE(orientation(m.vertices[t.v[2]], m.vertices[t.v[0]], p), Orientation::Clockwise);
}

TEST(PathQuery, LocateOutsideReturnsMinusOne) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));
	NavMesh m = buildNavMesh(in);

	EXPECT_EQ(locateTriangle(m, Vec2i64{-50, 500}), -1);
	EXPECT_EQ(locateTriangle(m, Vec2i64{500, 5000}), -1);
}

TEST(PathQuery, LocateDeterministic) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	in.polygons.push_back(blocked({{800, 800}, {1200, 800}, {1200, 1200}, {800, 1200}}, 7));
	NavMesh m = buildNavMesh(in);

	const Vec2i64 p{300, 300};
	EXPECT_EQ(locateTriangle(m, p), locateTriangle(m, p));
}

TEST(PathQuery, StraightShotOpenSpace) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 start{200, 200};
	const Vec2i64 goal{1800, 1800};
	PathResult	  path = pathThrough(m, start, goal, 0);

	ASSERT_TRUE(path.reachable);
	ASSERT_GE(path.points.size(), 2u);
	EXPECT_EQ(path.points.front(), start);
	EXPECT_EQ(path.points.back(), goal);
	// Open space: the taut path is the straight segment, no intermediate bends.
	EXPECT_EQ(path.points.size(), 2u);
}

TEST(PathQuery, AroundObstacle) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	std::vector<Vec2i64> obstacle = {{800, 800}, {1200, 800}, {1200, 1200}, {800, 1200}};
	in.polygons.push_back(blocked(obstacle, 7));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	// Start and goal on opposite corners so the straight line passes through the
	// obstacle; the path must bend around it.
	const Vec2i64 start{200, 200};
	const Vec2i64 goal{1800, 1800};
	PathResult	  path = pathThrough(m, start, goal, 0);

	ASSERT_TRUE(path.reachable);
	EXPECT_GT(path.points.size(), 2u) << "path must bend around the obstacle";
	EXPECT_FALSE(pathHitsRing(path, obstacle)) << "no path segment may cross the obstacle";
	EXPECT_EQ(path.points.front(), start);
	EXPECT_EQ(path.points.back(), goal);
}

TEST(PathQuery, ThroughDoor) {
	// Closed room with a doorway gap in the bottom wall [1400,1600] at y=1100.
	// Start outside (below the wall band), goal inside the room.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	std::vector<Vec2i64> bottomLeft	 = {{1000, 1000}, {1400, 1000}, {1400, 1100}, {1000, 1100}};
	std::vector<Vec2i64> bottomRight = {{1600, 1000}, {2000, 1000}, {2000, 1100}, {1600, 1100}};
	std::vector<Vec2i64> top		 = {{1000, 1900}, {2000, 1900}, {2000, 2000}, {1000, 2000}};
	std::vector<Vec2i64> left		 = {{1000, 1100}, {1100, 1100}, {1100, 1900}, {1000, 1900}};
	std::vector<Vec2i64> right		 = {{1900, 1100}, {2000, 1100}, {2000, 1900}, {1900, 1900}};
	in.polygons.push_back(blocked(bottomLeft, 20));
	in.polygons.push_back(blocked(bottomRight, 24));
	in.polygons.push_back(blocked(top, 21));
	in.polygons.push_back(blocked(left, 22));
	in.polygons.push_back(blocked(right, 23));

	DoorPortal door;
	door.openingId	  = 99;
	door.a			  = {1400, 1100};
	door.b			  = {1600, 1100};
	door.clearWidthMm = 200;
	in.doors.push_back(door);

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 start{1500, 500};	 // below the wall band, in the open exterior
	const Vec2i64 goal{1500, 1500};	 // centre of the room
	PathResult	  path = pathThrough(m, start, goal, 0);

	ASSERT_TRUE(path.reachable) << "doorway must connect exterior to room interior";
	EXPECT_EQ(path.points.front(), start);
	EXPECT_EQ(path.points.back(), goal);

	// The path must not cross any wall block ring (it threads the door gap).
	EXPECT_FALSE(pathHitsRing(path, bottomLeft));
	EXPECT_FALSE(pathHitsRing(path, bottomRight));
	EXPECT_FALSE(pathHitsRing(path, top));
	EXPECT_FALSE(pathHitsRing(path, left));
	EXPECT_FALSE(pathHitsRing(path, right));

	// The route must thread the doorway: some path segment crosses the door gap
	// line between the two opening endpoints (1400,1100)-(1600,1100). A straight
	// shot has no vertex inside the gap, so test segment traversal, not vertices.
	const Vec2i64 doorA{1400, 1100};
	const Vec2i64 doorB{1600, 1100};
	bool		  throughGap = false;
	for (std::size_t i = 0; i + 1 < path.points.size(); ++i) {
		const SegmentRelation rel =
			intersectSegments(path.points[i], path.points[i + 1], doorA, doorB).relation;
		if (rel == SegmentRelation::ProperCrossing || rel == SegmentRelation::EndpointTouch) {
			throughGap = true;
		}
	}
	EXPECT_TRUE(throughGap) << "expected a path segment to cross the doorway gap";
}

TEST(PathQuery, UnreachableClosedRoom) {
	// Closed room, no door: exterior and interior are disconnected.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	in.polygons.push_back(blocked({{1000, 1000}, {2000, 1000}, {2000, 1100}, {1000, 1100}}, 20));
	in.polygons.push_back(blocked({{1000, 1900}, {2000, 1900}, {2000, 2000}, {1000, 2000}}, 21));
	in.polygons.push_back(blocked({{1000, 1100}, {1100, 1100}, {1100, 1900}, {1000, 1900}}, 22));
	in.polygons.push_back(blocked({{1900, 1100}, {2000, 1100}, {2000, 1900}, {1900, 1900}}, 23));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 start{500, 500};	 // exterior
	const Vec2i64 goal{1500, 1500};	 // sealed interior
	PathResult	  path = pathThrough(m, start, goal, 0);

	EXPECT_FALSE(path.reachable);
	EXPECT_TRUE(path.points.empty());
}

TEST(PathQuery, OffMesh) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	NavMesh m = buildNavMesh(in);

	// Start outside the border.
	EXPECT_FALSE(pathThrough(m, Vec2i64{-100, 500}, Vec2i64{500, 500}, 0).reachable);
	// Goal outside the border.
	EXPECT_FALSE(pathThrough(m, Vec2i64{500, 500}, Vec2i64{9000, 9000}, 0).reachable);
}

TEST(PathQuery, RadiusClearance) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}}));
	std::vector<Vec2i64> obstacle = {{1600, 1600}, {2400, 1600}, {2400, 2400}, {1600, 2400}};
	in.polygons.push_back(blocked(obstacle, 7));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 start{300, 300};
	const Vec2i64 goal{3700, 3700};

	const std::int64_t radius = 200;
	PathResult		   wide	  = pathThrough(m, start, goal, radius);
	PathResult		   zero	  = pathThrough(m, start, goal, 0);
	ASSERT_TRUE(wide.reachable);
	ASSERT_TRUE(zero.reachable);

	// Every vertex of the radius path keeps ~radius clearance from the obstacle
	// (small rounding slack for the mm-rounded corners).
	const double clearance = minVertexDistanceToRing(wide, obstacle);
	EXPECT_GE(clearance, static_cast<double>(radius) - 2.0)
		<< "radius path must keep clearance from the obstacle";

	// A wider radius bows the corner further from the obstacle than radius 0.
	const double clearanceZero = minVertexDistanceToRing(zero, obstacle);
	EXPECT_GT(clearance, clearanceZero) << "wider radius must bow further out";
}

TEST(PathQuery, FunnelTautLShape) {
	// L-shaped corridor around a single reflex corner. A big blocker fills the
	// top-right quadrant; the only route from bottom-right to top-left bends once
	// at the blocker's reflex corner (its bottom-left corner).
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	std::vector<Vec2i64> blocker = {{1000, 1000}, {3000, 1000}, {3000, 3000}, {1000, 3000}};
	in.polygons.push_back(blocked(blocker, 7));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 start{2500, 500};	 // bottom-right open strip
	const Vec2i64 goal{500, 2500};	 // top-left open strip
	PathResult	  path = pathThrough(m, start, goal, 0);

	ASSERT_TRUE(path.reachable);
	EXPECT_EQ(path.points.size(), 3u) << "L-shape path has one interior bend";
	EXPECT_EQ(path.points.front(), start);
	EXPECT_EQ(path.points.back(), goal);
	// The single interior bend hugs the reflex corner (1000,1000).
	EXPECT_EQ(path.points[1], (Vec2i64{1000, 1000}));
}

TEST(PathQuery, Deterministic) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	in.polygons.push_back(blocked({{800, 800}, {1200, 800}, {1200, 1200}, {800, 1200}}, 7));
	NavMesh m = buildNavMesh(in);

	const Vec2i64 start{200, 200};
	const Vec2i64 goal{1800, 1800};
	PathResult	  a = pathThrough(m, start, goal, 0);
	PathResult	  b = pathThrough(m, start, goal, 0);

	ASSERT_EQ(a.reachable, b.reachable);
	ASSERT_EQ(a.points.size(), b.points.size());
	for (std::size_t i = 0; i < a.points.size(); ++i) {
		EXPECT_EQ(a.points[i], b.points[i]);
	}
}

// ---------------------------------------------------------------------------
// Belief filtering: the full footprint is triangulated and tagged, so the same
// truth mesh routes differently per agent (pathfinding-architecture section 5).
// ---------------------------------------------------------------------------

namespace {

	// Wall segment ids for the four walls of a 2000x2000 box room (walls at the
	// border edges, 200 mm thick, inward). Each wall is its own belief-gated segment.
	constexpr std::int64_t kSegS = 100; // south
	constexpr std::int64_t kSegN = 101; // north
	constexpr std::int64_t kSegW = 102; // west
	constexpr std::int64_t kSegE = 103; // east
	constexpr std::int64_t kDoorOp = 7; // a door in the south wall

	const Vec2i64 kOutside{1000, -500}; // below the south wall, outside the box
	const Vec2i64 kInside{1000, 1000};	// box centre

	// A 2000x2000 box room, walls 200 mm thick inset from the border (the interior is
	// [200,1800]^2). The border is padded so `kOutside` is on-mesh outside the box.
	// Each wall ring is tagged with its own segment id; nothing is physically cut, so
	// the box is SEALED under truth. With `withDoor`, the south wall's middle span
	// [800,1200] is a pathable door span instead of solid.
	NavMeshInput buildBoxRoom(bool withDoor) {
		NavMeshInput in;
		in.polygons.push_back(border({{-2000, -2000}, {4000, -2000}, {4000, 4000}, {-2000, 4000}}));
		// North wall (top), west, east: solid full spans.
		in.polygons.push_back(blocked({{0, 1800}, {2000, 1800}, {2000, 2000}, {0, 2000}}, kSegN));
		in.polygons.push_back(blocked({{0, 0}, {200, 0}, {200, 2000}, {0, 2000}}, kSegW));
		in.polygons.push_back(blocked({{1800, 0}, {2000, 0}, {2000, 2000}, {1800, 2000}}, kSegE));
		// South wall (bottom), y in [0,200]: solid, or split into flanks + door span.
		if (withDoor) {
			in.polygons.push_back(blocked({{0, 0}, {800, 0}, {800, 200}, {0, 200}}, kSegS));					// left flank
			in.polygons.push_back(doorSpan({{800, 0}, {1200, 0}, {1200, 200}, {800, 200}}, kSegS, kDoorOp));	// door span
			in.polygons.push_back(blocked({{1200, 0}, {2000, 0}, {2000, 200}, {1200, 200}}, kSegS));			// right flank
		} else {
			in.polygons.push_back(blocked({{0, 0}, {2000, 0}, {2000, 200}, {0, 200}}, kSegS)); // solid south
		}
		return in;
	}

	// The wall centre line crossing point (used to assert a path threads the band).
	const Vec2i64 kWallA{0, 100};
	const Vec2i64 kWallB{2000, 100};

	bool crossesWallLine(const PathResult& path) {
		for (std::size_t i = 0; i + 1 < path.points.size(); ++i) {
			const SegmentRelation rel = intersectSegments(path.points[i], path.points[i + 1], kWallA, kWallB).relation;
			if (rel == SegmentRelation::ProperCrossing || rel == SegmentRelation::EndpointTouch) {
				return true;
			}
		}
		return false;
	}

} // namespace

TEST(PathQuery, TruthSealedRoomUnreachable) {
	// A sealed box (no door) is unreachable under truth: the four solid walls block.
	// (Confirms truth still routes as v1: a closed room has no path in.)
	NavMesh m = buildNavMesh(buildBoxRoom(/*withDoor=*/false));
	EXPECT_FALSE(pathThrough(m, kOutside, kInside, 0).reachable);
}

TEST(PathQuery, BeliefEmptyWalksStraightThroughUnseenWall) {
	// Same sealed box, but the agent has seen NOTHING: every wall is absent, so the
	// interior is reachable and the route crosses the south wall line where the wall
	// physically sits. This is the optimistic freespace assumption: plan through the
	// unseen wall.
	NavMesh m = buildNavMesh(buildBoxRoom(/*withDoor=*/false));

	std::unordered_set<std::uint64_t> noSegs;
	std::unordered_set<std::uint64_t> noOps;
	BeliefFilter belief{&noSegs, &noOps};

	PathResult path = pathThrough(m, kOutside, kInside, 0, belief);
	ASSERT_TRUE(path.reachable) << "an unseen wall is absent: the sealed room is reachable in belief";
	EXPECT_EQ(path.points.front(), kOutside);
	EXPECT_EQ(path.points.back(), kInside);
	EXPECT_TRUE(crossesWallLine(path)) << "the path passes through where the (unseen) south wall sits";
}

TEST(PathQuery, BeliefKnowsWallsIsBlocked) {
	// Same sealed box; the agent now knows all four wall segments. No door exists, so
	// every wall blocks: no believed route in, exactly as truth.
	NavMesh m = buildNavMesh(buildBoxRoom(/*withDoor=*/false));

	std::unordered_set<std::uint64_t> segs{
		static_cast<std::uint64_t>(kSegS), static_cast<std::uint64_t>(kSegN),
		static_cast<std::uint64_t>(kSegW), static_cast<std::uint64_t>(kSegE)};
	std::unordered_set<std::uint64_t> noOps;
	BeliefFilter belief{&segs, &noOps};

	PathResult path = pathThrough(m, kOutside, kInside, 0, belief);
	EXPECT_FALSE(path.reachable);
	EXPECT_TRUE(path.points.empty());
}

TEST(PathQuery, BeliefKnowsWallButNotItsDoorIsBlocked) {
	// Box WITH a door in the south wall, but the agent knows the south wall and has
	// not discovered the door: the known wall blocks and the undiscovered door does
	// not help. (Knows all four walls, knows no openings.)
	NavMesh m = buildNavMesh(buildBoxRoom(/*withDoor=*/true));

	std::unordered_set<std::uint64_t> segs{
		static_cast<std::uint64_t>(kSegS), static_cast<std::uint64_t>(kSegN),
		static_cast<std::uint64_t>(kSegW), static_cast<std::uint64_t>(kSegE)};
	std::unordered_set<std::uint64_t> noOps;
	BeliefFilter belief{&segs, &noOps};

	EXPECT_FALSE(pathThrough(m, kOutside, kInside, 0, belief).reachable);
}

TEST(PathQuery, BeliefKnowsWallAndDoorRoutesThroughDoor) {
	// Box WITH a door; the agent knows the walls AND the south door. It routes in
	// through the door span (x in [800,1200]), like a truth query would.
	NavMesh m = buildNavMesh(buildBoxRoom(/*withDoor=*/true));

	std::unordered_set<std::uint64_t> segs{
		static_cast<std::uint64_t>(kSegS), static_cast<std::uint64_t>(kSegN),
		static_cast<std::uint64_t>(kSegW), static_cast<std::uint64_t>(kSegE)};
	std::unordered_set<std::uint64_t> ops{static_cast<std::uint64_t>(kDoorOp)};
	BeliefFilter belief{&segs, &ops};

	PathResult path = pathThrough(m, kOutside, kInside, 0, belief);
	ASSERT_TRUE(path.reachable);
	ASSERT_TRUE(crossesWallLine(path));
	for (std::size_t i = 0; i + 1 < path.points.size(); ++i) {
		SegmentIntersection si = intersectSegments(path.points[i], path.points[i + 1], kWallA, kWallB);
		if (si.relation == SegmentRelation::ProperCrossing) {
			EXPECT_GE(si.point.x, 800);
			EXPECT_LE(si.point.x, 1200) << "known door: must thread the door span, not the solid flank";
		}
	}

	// And a truth query routes the same way (door passes, walls block).
	EXPECT_TRUE(pathThrough(m, kOutside, kInside, 0).reachable);
}

// ---------------------------------------------------------------------------
// Corridor-width filtering: a too-narrow gap yields no path (not a clipping
// path). Gaps are bounded by common-knowledge terrain; doors by their clear
// width. Thresholds are exact (floored mm diameter vs 2*radius).
// ---------------------------------------------------------------------------

namespace {

	NavInputPolygon water(std::vector<Vec2i64> ring, std::int64_t id) {
		return {std::move(ring), true, id};
	}

	// A full-width water band split by a single 400 mm gap (x in [1800,2200]); the
	// only way from below to above is through that gap.
	NavMeshInput buildGappedBand() {
		NavMeshInput in;
		in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 2000}, {0, 2000}}));
		in.polygons.push_back(water({{0, 900}, {1800, 900}, {1800, 1100}, {0, 1100}}, -10));
		in.polygons.push_back(water({{2200, 900}, {4000, 900}, {4000, 1100}, {2200, 1100}}, -11));
		return in;
	}

} // namespace

TEST(PathQuery, NarrowGapAdmitsAgentThatFits) {
	// Gap is 400 mm. An agent of radius 150 (diameter 300 <= 400) fits and routes
	// through; the same query at radius 0 also routes (sanity).
	NavMesh m = buildNavMesh(buildGappedBand());
	const Vec2i64 below{2000, 300};
	const Vec2i64 above{2000, 1700};

	PathResult fits = pathThrough(m, below, above, 150);
	ASSERT_TRUE(fits.reachable) << "an agent narrower than the gap must pass";
	EXPECT_EQ(fits.points.front(), below);
	EXPECT_EQ(fits.points.back(), above);
	// The route threads the gap: some segment crosses the gap mouth line x in
	// [1800,2200] at the band's centre y=1000.
	bool throughGap = false;
	for (std::size_t i = 0; i + 1 < fits.points.size(); ++i) {
		SegmentIntersection si =
			intersectSegments(fits.points[i], fits.points[i + 1], Vec2i64{1800, 1000}, Vec2i64{2200, 1000});
		if (si.relation == SegmentRelation::ProperCrossing || si.relation == SegmentRelation::EndpointTouch) {
			throughGap = true;
		}
	}
	EXPECT_TRUE(throughGap) << "the path must thread the 400 mm gap";
}

TEST(PathQuery, NarrowGapRejectsAgentTooWide) {
	// Same 400 mm gap. An agent of radius 201 (diameter 402 > 400) does not fit:
	// no path, and crucially NOT a clipping path through the gap.
	NavMesh m = buildNavMesh(buildGappedBand());
	const Vec2i64 below{2000, 300};
	const Vec2i64 above{2000, 1700};

	PathResult tooWide = pathThrough(m, below, above, 201);
	EXPECT_FALSE(tooWide.reachable) << "an agent wider than the gap must be rejected";
	EXPECT_TRUE(tooWide.points.empty());
}

TEST(PathQuery, GapWidthThresholdIsExact) {
	// Gap is exactly 400 mm. Diameter == width passes (radius 200), diameter one mm
	// over fails (radius 201): the integer threshold is exact, no float slack.
	NavMesh m = buildNavMesh(buildGappedBand());
	const Vec2i64 below{2000, 300};
	const Vec2i64 above{2000, 1700};

	EXPECT_TRUE(pathThrough(m, below, above, 200).reachable) << "diameter 400 == gap 400 must pass";
	EXPECT_FALSE(pathThrough(m, below, above, 201).reachable) << "diameter 402 > gap 400 must fail";
}

TEST(PathQuery, SliverStripPassesWhatFitsItsWidth) {
	// A long thin strip with staggered corners along both sides triangulates into high-
	// aspect slivers between its mesh edges, which bound widths like obstacles: the
	// strip's 40 mm, never a sliver's altitude.
	std::vector<Vec2i64> ring;
	for (std::int64_t x = 0; x <= 6000; x += 500) {
		ring.push_back({x, 0});
	}
	ring.push_back({6000, 40});
	for (std::int64_t x = 5750; x > 0; x -= 500) {
		ring.push_back({x, 40});
	}
	ring.push_back({0, 40});
	NavMeshInput in;
	in.polygons.push_back(border(std::move(ring)));
	NavMesh m = buildNavMesh(in);
	ASSERT_GT(m.triangles.size(), 20u);

	const Vec2i64 a{200, 20};
	const Vec2i64 b{5800, 20};
	PathResult	  p = pathThrough(m, a, b, 15); // diameter 30, inside the 40 mm strip
	ASSERT_TRUE(p.reachable) << "slivers must not block an agent that fits the strip";
	EXPECT_EQ(p.points.front(), a);
	EXPECT_EQ(p.points.back(), b);
	EXPECT_FALSE(pathThrough(m, a, b, 21).reachable) << "diameter 42 is wider than the strip";
}

TEST(PathQuery, DoorRejectsAgentWiderThanClearWidth) {
	// Closed room with a 200 mm-clear-width door in the bottom wall. An agent of
	// radius 150 (diameter 300 > 200) cannot fit the door; radius 80 (diameter 160 <
	// 200) can. Clear width drives the gate.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	std::vector<Vec2i64> bottomLeft	 = {{1000, 1000}, {1400, 1000}, {1400, 1100}, {1000, 1100}};
	std::vector<Vec2i64> bottomRight = {{1600, 1000}, {2000, 1000}, {2000, 1100}, {1600, 1100}};
	std::vector<Vec2i64> top		 = {{1000, 1900}, {2000, 1900}, {2000, 2000}, {1000, 2000}};
	std::vector<Vec2i64> left		 = {{1000, 1100}, {1100, 1100}, {1100, 1900}, {1000, 1900}};
	std::vector<Vec2i64> right		 = {{1900, 1100}, {2000, 1100}, {2000, 1900}, {1900, 1900}};
	in.polygons.push_back(blocked(bottomLeft, 20));
	in.polygons.push_back(blocked(bottomRight, 24));
	in.polygons.push_back(blocked(top, 21));
	in.polygons.push_back(blocked(left, 22));
	in.polygons.push_back(blocked(right, 23));

	DoorPortal door;
	door.openingId	  = 99;
	door.a			  = {1400, 1100};
	door.b			  = {1600, 1100};
	door.clearWidthMm = 200; // doorway is 200 mm clear
	in.doors.push_back(door);

	NavMesh m = buildNavMesh(in);
	const Vec2i64 start{1500, 500};	 // exterior
	const Vec2i64 goal{1500, 1500};	 // room interior

	EXPECT_TRUE(pathThrough(m, start, goal, 80).reachable) << "agent narrower than the door passes";
	PathResult tooWide = pathThrough(m, start, goal, 150);
	EXPECT_FALSE(tooWide.reachable) << "agent wider than the 200 mm door is rejected";
	EXPECT_TRUE(tooWide.points.empty());
}

TEST(PathQuery, ObtuseVertexPinchIsRespected) {
	// Two triangular obstacles (CK) with tips facing each other across the channel,
	// tips at (1700,1000) and (2300,1000). The binding constriction is the 600 mm
	// distance between those two obstacle VERTICES (Demyen Case 1, an obtuse squeeze:
	// the perpendicular foot from one tip onto the other obstacle's edges lies outside
	// them, so a vertex is the closest feature). It is the only way from below to above.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 2000}, {0, 2000}}));
	// Left wedge spanning top-to-bottom with its tip poking right to (1700,1000).
	in.polygons.push_back(water({{0, -10}, {1700, 1000}, {0, 2010}, {-10, 2010}, {-10, -10}}, -10));
	// Right wedge with its tip poking left to (2300,1000).
	in.polygons.push_back(water({{4000, -10}, {4010, -10}, {4010, 2010}, {4000, 2010}, {2300, 1000}}, -11));
	NavMesh m = buildNavMesh(in);

	const Vec2i64 below{2000, 200};
	const Vec2i64 above{2000, 1800};

	// An agent with diameter == the 600 mm vertex gap fits; one mm wider does not.
	EXPECT_TRUE(pathThrough(m, below, above, 300).reachable) << "diameter 600 == vertex gap must pass";
	PathResult tooWide = pathThrough(m, below, above, 301);
	EXPECT_FALSE(tooWide.reachable) << "diameter 602 > vertex gap must be rejected";
	EXPECT_TRUE(tooWide.points.empty());
}

TEST(PathQuery, TerrainSentinelBlocksEvenWithEmptyBelief) {
	// A terrain obstacle (negative provenance sentinel, like water/tree) always
	// blocks: belief does not apply to common-knowledge terrain. A full-width water
	// band partitions the border; no route across under empty belief OR truth.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	in.polygons.push_back(blocked({{0, 900}, {2000, 900}, {2000, 1100}, {0, 1100}}, -1)); // water sentinel
	NavMesh m = buildNavMesh(in);

	const Vec2i64 below{1000, 300};
	const Vec2i64 above{1000, 1700};
	std::unordered_set<std::uint64_t> noSegs;
	std::unordered_set<std::uint64_t> noOps;
	BeliefFilter belief{&noSegs, &noOps};

	EXPECT_FALSE(pathThrough(m, below, above, 0, belief).reachable);
	EXPECT_FALSE(pathThrough(m, below, above, 0).reachable);
}

namespace {

	// A visibility limit of 10 m round the origin, and a placed obstacle rising from below it
	// to a point about 1 m under its top corner: the only way from west to east passes
	// between that point and the mesh edge.
	struct EdgeGap {
		std::vector<Vec2i64> limit = testdata::limitPolygon({0, 0}, 10000);
		NavInputPolygon		 ground{limit, false, 1};
		NavInputPolygon		 spike = blocked({{-500, -12000}, {500, -12000}, {0, limit[4].y - 1020}}, -2);
		// The ground a second limit adds round the first's top, so its edge there dissolves.
		NavInputPolygon above{testdata::limitPolygon({0, 8000}, 10000), false, 1};
		Vec2i64			west{-5000, 0};
		Vec2i64			east{5000, 0};
	};

	bool freshPasses(const std::vector<NavInputPolygon>& polygons, const EdgeGap& g, std::int64_t radius) {
		NavMeshInput in;
		in.polygons = polygons;
		return pathThrough(buildNavMesh(in), g.west, g.east, radius).reachable;
	}

} // namespace

// Outside the mesh is impassable, so its edge narrows corridors like an obstacle: the 1 m
// gap between a placed obstacle and the mesh edge passes an 800 mm disc and shuts on a
// 1.2 m one.
TEST(PathQuery, MeshEdgeNarrowsCorridors) {
	const EdgeGap g;
	EXPECT_TRUE(freshPasses({g.ground, g.spike}, g, 400));
	EXPECT_FALSE(freshPasses({g.ground, g.spike}, g, 600));
	EXPECT_TRUE(freshPasses({g.ground}, g, 600)) << "without the obstacle the way is open";
}

// Ground added beside the edge dissolves it and the gap widens; removed again, the edge
// returns and the gap narrows. The patched mesh's refreshed widths answer as a fresh build
// of the same outlines does.
TEST(PathQuery, DissolvedEdgeWidensAgain) {
	const EdgeGap g;
	PatchableMesh mesh;
	PatchEdits	  first;
	first.adds = {g.ground, g.spike};
	mesh.applyPatch(first);
	mesh.refreshWidths();
	ASSERT_TRUE(mesh.validate().empty()) << mesh.validate();
	EXPECT_FALSE(pathThrough(mesh.view(), g.west, g.east, 600).reachable);

	PatchEdits widen;
	widen.adds.push_back(g.above);
	const RingId added = mesh.applyPatch(widen).added.front();
	mesh.refreshWidths();
	ASSERT_TRUE(mesh.validate().empty()) << mesh.validate();
	EXPECT_TRUE(pathThrough(mesh.view(), g.west, g.east, 600).reachable) << "the edge beside the gap is dissolved";
	EXPECT_TRUE(freshPasses({g.ground, g.spike, g.above}, g, 600));

	PatchEdits narrow;
	narrow.removes.push_back(added);
	mesh.applyPatch(narrow);
	mesh.refreshWidths();
	ASSERT_TRUE(mesh.validate().empty()) << mesh.validate();
	EXPECT_FALSE(pathThrough(mesh.view(), g.west, g.east, 600).reachable) << "the edge is back";
	EXPECT_TRUE(pathThrough(mesh.view(), g.west, g.east, 400).reachable);
}

namespace {

	// The floor triangles at vertex `at`, and how many of them have neither of their two
	// edges at it on an obstacle (the mesh edge or a blocked face).
	struct Fan {
		int triangles = 0;
		int clear	  = 0;
	};

	Fan floorFanAt(const NavMesh& m, const Vec2i64& at) {
		Fan fan;
		for (const NavTriangle& t : m.triangles) {
			if (!isFloorFace(t)) {
				continue;
			}
			for (int i = 0; i < 3; ++i) {
				if (m.vertices[t.v[static_cast<std::size_t>(i)]] != at) {
					continue;
				}
				++fan.triangles;
				bool open = true;
				for (const int e : {i, (i + 2) % 3}) {
					const std::int32_t nb = t.neighbor[static_cast<std::size_t>(e)];
					open = open && nb >= 0 && isFloorFace(m.triangles[static_cast<std::size_t>(nb)]);
				}
				fan.clear += open ? 1 : 0;
			}
		}
		return fan;
	}

	std::int64_t floorDistance(const Vec2i64& a, const Vec2i64& b) {
		const Vec2i64 d = b - a;
		auto		  k = static_cast<std::int64_t>(std::sqrt(static_cast<double>(d.x * d.x + d.y * d.y)));
		while (k * k > d.x * d.x + d.y * d.y) {
			--k;
		}
		while ((k + 1) * (k + 1) <= d.x * d.x + d.y * d.y) {
			++k;
		}
		return k;
	}

	// A disc fits the waist between corners p and q exactly when its diameter is at most
	// |pq|: one 10 mm under passes and one 10 mm over is refused.
	void expectWaist(const NavMesh& m, const Vec2i64& p, const Vec2i64& q, const Vec2i64& west, const Vec2i64& east, const std::string& what) {
		const std::int64_t waist = floorDistance(p, q);
		for (const Vec2i64& corner : {p, q}) {
			const Fan fan = floorFanAt(m, corner);
			EXPECT_GE(fan.triangles, 3) << what << ": the fan at (" << corner.x << ", " << corner.y << ")";
			EXPECT_GE(fan.clear, 1) << what << ": a fan triangle at (" << corner.x << ", " << corner.y << ") with no obstacle edge there";
		}
		EXPECT_TRUE(pathThrough(m, west, east, (waist - 10) / 2).reachable) << what << ": waist " << waist;
		EXPECT_FALSE(pathThrough(m, west, east, (waist + 10) / 2).reachable) << what << ": waist " << waist;
	}

} // namespace

// Two overlapping limits meet at two reflex corners, the only way between them: the waist
// between those corners admits a disc that fits it and refuses one that does not.
TEST(PathQuery, WaistBetweenTwoLimitsIsItsCornerGap) {
	const std::int64_t circumradius = testdata::limitCircumradius(10000);
	for (const std::int64_t overlap : {150, 199, 260, 400}) {
		for (const std::int64_t rise : {0, 120, 350}) {
			const std::vector<Vec2i64> left	 = testdata::limitPolygon({0, 0}, 10000);
			const std::vector<Vec2i64> right = testdata::limitPolygon({2 * circumradius - overlap, rise}, 10000);
			NavMeshInput			   in;
			in.polygons.push_back(border(left));
			in.polygons.push_back(border(right));
			const NavMesh m = buildNavMesh(in);

			std::vector<Vec2i64> corners;
			for (const Vec2i64& v : m.vertices) {
				if (std::find(left.begin(), left.end(), v) == left.end() && std::find(right.begin(), right.end(), v) == right.end()) {
					corners.push_back(v);
				}
			}
			const std::string what = "overlap " + std::to_string(overlap) + " rise " + std::to_string(rise);
			ASSERT_EQ(corners.size(), 2u) << what;
			expectWaist(m, corners[0], corners[1], {-5000, 3000}, {2 * circumradius - overlap + 5000, rise - 3000}, what);
		}
	}
}

// The same waist between two placed obstacles whose corners face each other across open
// ground, no edge of one parallel to an edge of the other.
TEST(PathQuery, WaistBetweenFacingCornersIsTheirGap) {
	for (const std::int64_t gap : {600, 1000, 1500}) {
		for (const std::int64_t shift : {0, 250}) {
			const Vec2i64 top{0, gap / 2};
			const Vec2i64 bottom{shift, -(gap - gap / 2)};
			NavMeshInput  in;
			in.polygons.push_back(border({{-8000, -8000}, {8000, -8000}, {8000, 8000}, {-8000, 8000}}));
			in.polygons.push_back(blocked({top, {1300, 8010}, {-1100, 8010}}, -2));
			in.polygons.push_back(blocked({bottom, {shift - 1200, -8010}, {shift + 1000, -8010}}, -3));
			const NavMesh m = buildNavMesh(in);
			expectWaist(m, top, bottom, {-5000, 0}, {5000, 0}, "gap " + std::to_string(gap) + " shift " + std::to_string(shift));
		}
	}
}

// ---------------------------------------------------------------------------
// Grid-accelerated locateTriangle: correctness vs brute-force linear scan.
// ---------------------------------------------------------------------------

namespace {

	// Brute-force linear scan over all triangles; returns the lowest-index match.
	std::int32_t locateLinear(const NavMesh& mesh, const Vec2i64& p) {
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(mesh.triangles.size()); ++ti) {
			const NavTriangle& t  = mesh.triangles[ti];
			const Vec2i64&	   v0 = mesh.vertices[t.v[0]];
			const Vec2i64&	   v1 = mesh.vertices[t.v[1]];
			const Vec2i64&	   v2 = mesh.vertices[t.v[2]];
			if (orientation(v0, v1, p) != Orientation::Clockwise &&
				orientation(v1, v2, p) != Orientation::Clockwise &&
				orientation(v2, v0, p) != Orientation::Clockwise) {
				return ti;
			}
		}
		return -1;
	}

	// Simple LCG (Knuth multiplicative): deterministic pseudo-random int64 sequence.
	// Seeded with a constant so tests are reproducible.
	struct LCG {
		std::uint64_t state;
		explicit LCG(std::uint64_t seed) : state(seed) {}
		std::int64_t next(std::int64_t lo, std::int64_t hi) {
			// LCG parameters from Knuth MMIX.
			state = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
			// Map the high 32 bits to [lo, hi].
			const std::uint64_t range = static_cast<std::uint64_t>(hi - lo) + 1;
			return lo + static_cast<std::int64_t>((state >> 32) % range);
		}
	};

} // namespace

TEST(LocateGrid, EmptyMeshReturnsMinusOne) {
	// An empty NavMesh (no polygons -> no triangles) must return -1 for any point.
	NavMesh empty;
	EXPECT_EQ(locateTriangle(empty, Vec2i64{0, 0}), -1);
	EXPECT_EQ(locateTriangle(empty, Vec2i64{500, 500}), -1);
	EXPECT_EQ(locateTriangle(empty, Vec2i64{-1000, 999}), -1);
}

TEST(LocateGrid, GridMatchesLinearScanFuzzed) {
	// Over a mesh with a central obstacle, compare grid locate vs brute-force linear
	// scan for 500 deterministically-generated points spanning and surrounding the
	// mesh AABB. The grid must agree for every point, including off-mesh (-1).
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	in.polygons.push_back(blocked({{1000, 1000}, {2000, 1000}, {2000, 2000}, {1000, 2000}}, 5));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	LCG rng(0xDEADBEEF42ULL);
	// Points drawn from a range wider than the mesh AABB to include off-mesh points.
	for (int i = 0; i < 500; ++i) {
		const Vec2i64 p{rng.next(-500, 3500), rng.next(-500, 3500)};
		EXPECT_EQ(locateTriangle(m, p), locateLinear(m, p))
			<< "mismatch at p=(" << p.x << "," << p.y << ") i=" << i;
	}
}

TEST(LocateGrid, SharedEdgeTieBreakMatchesLinearScan) {
	// A point exactly on a shared edge between two triangles must resolve to the
	// same (lowest-index) triangle as the linear scan.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	NavMesh m = buildNavMesh(in);
	ASSERT_GE(m.triangles.size(), 2u);

	// Find a point on a shared interior edge by using a triangle's centroid of its
	// shared edge.  Walk all triangles, find one with a neighbor, use the midpoint
	// of their shared edge (which lies exactly on both triangles).
	bool foundShared = false;
	for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
		const NavTriangle& t = m.triangles[ti];
		for (int e = 0; e < 3; ++e) {
			if (t.neighbor[e] < 0) continue;
			// Midpoint of shared edge (v[e], v[(e+1)%3]).
			const Vec2i64& a = m.vertices[t.v[e]];
			const Vec2i64& b = m.vertices[t.v[(e + 1) % 3]];
			Vec2i64 mid{(a.x + b.x) / 2, (a.y + b.y) / 2};
			const std::int32_t grid   = locateTriangle(m, mid);
			const std::int32_t linear = locateLinear(m, mid);
			EXPECT_EQ(grid, linear)
				<< "shared-edge midpoint (" << mid.x << "," << mid.y
				<< "): grid=" << grid << " linear=" << linear;
			foundShared = true;
			// Test one shared edge; the property must hold for all, but one is enough
			// for this targeted test (the fuzz test covers the rest).
			break;
		}
		if (foundShared) break;
	}
	EXPECT_TRUE(foundShared) << "expected at least one shared interior edge";
}

TEST(LocateGrid, SharedVertexTieBreakMatchesLinearScan) {
	// A point exactly at a mesh vertex (shared by multiple triangles) must also
	// agree with the linear scan's lowest-index result.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.vertices.empty());

	// Test each vertex.
	for (std::size_t vi = 0; vi < m.vertices.size(); ++vi) {
		const Vec2i64& v = m.vertices[vi];
		const std::int32_t grid   = locateTriangle(m, v);
		const std::int32_t linear = locateLinear(m, v);
		EXPECT_EQ(grid, linear)
			<< "vertex[" << vi << "]=(" << v.x << "," << v.y
			<< "): grid=" << grid << " linear=" << linear;
	}
}

TEST(LocateGrid, OffMeshPointsReturnMinusOne) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));
	NavMesh m = buildNavMesh(in);

	EXPECT_EQ(locateTriangle(m, Vec2i64{-1, 500}), -1);
	EXPECT_EQ(locateTriangle(m, Vec2i64{500, -1}), -1);
	EXPECT_EQ(locateTriangle(m, Vec2i64{1001, 500}), -1);
	EXPECT_EQ(locateTriangle(m, Vec2i64{500, 1001}), -1);
}

// --- Placed obstacles ------------------------------------------------------------

namespace {

	constexpr std::int64_t kTreeTag = -2;

	NavInputPolygon treeRect(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, true, kTreeTag};
	}

	double polylineLength(const PathResult& p) {
		double length = 0.0;
		for (std::size_t i = 1; i < p.points.size(); ++i) {
			length += std::hypot(static_cast<double>(p.points[i].x - p.points[i - 1].x), static_cast<double>(p.points[i].y - p.points[i - 1].y));
		}
		return length;
	}

	NavInputPolygon field() {
		return {{{0, 0}, {20000, 0}, {20000, 12000}, {0, 12000}}, false, 1};
	}

	// A 20 x 12 m field split at y = 6 m by a hedge of tree squares from x = 2 m to x =
	// 18 m, open at both ends, with `gap` mm between trunks.
	NavMesh hedgeMesh(std::int64_t gap = 0) {
		NavMeshInput in;
		in.polygons.push_back(field());
		for (std::int64_t x = 2000; x < 18000; x += 1000) {
			in.polygons.push_back(treeRect(x, 5800, x + 1000 - gap, 6200));
		}
		return buildNavMesh(in);
	}

} // namespace

// Placed obstacles block: a goal walled in by a ring of trees is no path, and trunks 200 mm
// apart stay shut to a 600 mm disc, which goes round an end, while a point walks through
// the gap. A walk's cost is its length.
TEST(PathQuery, PlacedObstaclesBlock) {
	NavMeshInput in;
	in.polygons.push_back({{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}}, false, 1});
	in.polygons.push_back(treeRect(3000, 3000, 7000, 3500));
	in.polygons.push_back(treeRect(3000, 6500, 7000, 7000));
	in.polygons.push_back(treeRect(3000, 3000, 3500, 7000));
	in.polygons.push_back(treeRect(6500, 3000, 7000, 7000));
	EXPECT_FALSE(pathThrough(buildNavMesh(in), {1000, 5000}, {5000, 5000}, 0).reachable);

	const NavMesh m = hedgeMesh(200);
	const Vec2i64 start{10100, 4000};
	const Vec2i64 goal{10100, 8000};
	const PathResult point = pathThrough(m, start, goal, 0);
	ASSERT_TRUE(point.reachable);
	EXPECT_LT(polylineLength(point), 6000.0) << "a point walks through the 200 mm gap";
	const PathResult round = pathThrough(m, start, goal, 300);
	ASSERT_TRUE(round.reachable);
	EXPECT_GT(polylineLength(round), 12000.0) << "the disc goes round an end";
	EXPECT_NEAR(round.cost, polylineLength(round), 2.0);
}

// Start and goal in one triangle: the walk is the straight line and costs its length.
TEST(PathQuery, OneTriangleWalkCostsItsLength) {
	NavMeshInput in;
	in.polygons.push_back({{{0, 0}, {10000, 0}, {0, 10000}}, false, 1});
	const PathResult plain = pathThrough(buildNavMesh(in), {1000, 1000}, {1600, 1800}, 0);
	ASSERT_TRUE(plain.reachable);
	EXPECT_EQ(plain.points.size(), 2u);
	EXPECT_NEAR(plain.cost, 1000.0, 1e-6);
}

// A start or goal on a placed obstacle's edge stands on the floor beside it.
TEST(PathQuery, AnEndOnAnObstacleEdgeStandsOnTheFloor) {
	NavMeshInput in;
	in.polygons.push_back({{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}}, false, 1});
	in.polygons.push_back(treeRect(4000, 4000, 6000, 6000));
	const NavMesh m = buildNavMesh(in);
	// On the trunk's east edge, where the tree's faces' corners sort before the floor's.
	for (const auto& [a, b] : std::vector<std::pair<Vec2i64, Vec2i64>>{{{6000, 5000}, {9000, 5000}}, {{9000, 5000}, {6000, 5000}}, {{6000, 6000}, {9000, 6000}}}) {
		const PathResult r = pathThrough(m, a, b, 0);
		ASSERT_TRUE(r.reachable) << "from (" << a.x << ", " << a.y << ")";
		EXPECT_NEAR(r.cost, 3000.0, 1e-6);
	}
}

// Corner classes are kept per mesh state. A mesh assigned another's content in place,
// buffers and all, answers as that content does: a door span where a tree stood is no
// corner to keep clear of.
TEST(PathQuery, AMeshRebuiltInPlaceForgetsItsCorners) {
	auto build = [](NavInputPolygon square) {
		NavMeshInput in;
		in.polygons.push_back({{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}}, false, 1});
		in.polygons.push_back(std::move(square));
		return buildNavMesh(in);
	};
	NavMesh		  m		= build(treeRect(4000, 4000, 6000, 6000));
	const NavMesh door	= build({{{4000, 4000}, {6000, 4000}, {6000, 6000}, {4000, 6000}}, true, 7, 3});
	// 100 mm inside the square's south edge: straight through the door span, but past the
	// tree only round its corners.
	const Vec2i64	 start{1000, 4100};
	const Vec2i64	 goal{9000, 4100};
	const PathResult round = pathThrough(m, start, goal, 300);
	ASSERT_TRUE(round.reachable);
	ASSERT_GT(round.points.size(), 2u);
	const void* vertices = m.vertices.data();
	m					 = door;
	ASSERT_EQ(m.vertices.data(), vertices) << "the copy reuses the buffers, as a rebuild at one address can";
	const PathResult again = pathThrough(m, start, goal, 300);
	EXPECT_EQ(again.points, pathThrough(door, start, goal, 300).points);
	EXPECT_EQ(again.points, (std::vector<Vec2i64>{start, goal}));
}

// --- Shortest walks against an exact referee ------------------------------------

namespace {

	struct Rect {
		std::int64_t x0, y0, x1, y1;
	};

	// Walls of axis-aligned rects in a 6 x 4 m box: a long slab with both ends open, a
	// C round a pocket (whose way in is behind the start of anything inside it), and
	// posts, so shortest walks wrap ends, double back, and thread gaps.
	const std::vector<Rect>& refereeRects() {
		static const std::vector<Rect> rects = {
			{800, 1400, 4400, 1600},  // slab
			{3600, 2200, 5200, 2400}, // C: top
			{5000, 2200, 5200, 3600}, // C: back (overlapping its arms, so no seam is open)
			{3600, 3400, 5200, 3600}, // C: bottom
			{1200, 2400, 1600, 2800}, // posts
			{2200, 2800, 2600, 3200},
			{1800, 400, 2200, 800},
		};
		return rects;
	}

	NavMesh buildRefereeMesh(const std::vector<Rect>& rects) {
		NavMeshInput in;
		in.polygons.push_back({{{0, 0}, {6000, 0}, {6000, 4000}, {0, 4000}}, false, 1});
		std::int64_t id = -40;
		for (const Rect& r : rects) {
			in.polygons.push_back({{{r.x0, r.y0}, {r.x1, r.y0}, {r.x1, r.y1}, {r.x0, r.y1}}, true, id--});
		}
		return buildNavMesh(in);
	}

	// Does segment p-q pass through the open interior of rect r?
	bool crossesInterior(const Vec2i64& p, const Vec2i64& q, const Rect& r) {
		double		 lo = 0.0;
		double		 hi = 1.0;
		const double dx = static_cast<double>(q.x - p.x);
		const double dy = static_cast<double>(q.y - p.y);
		auto		 clip = [&](double start, double delta, double min, double max) {
			if (delta == 0.0) {
				if (start <= min || start >= max) {
					hi = -1.0;
				}
				return;
			}
			double a = (min - start) / delta;
			double b = (max - start) / delta;
			if (a > b) {
				std::swap(a, b);
			}
			lo = std::max(lo, a);
			hi = std::min(hi, b);
		};
		clip(static_cast<double>(p.x), dx, static_cast<double>(r.x0), static_cast<double>(r.x1));
		clip(static_cast<double>(p.y), dy, static_cast<double>(r.y0), static_cast<double>(r.y1));
		return hi - lo > 1e-9;
	}

	// The exact shortest walk from a to b round the rects (radius 0): Dijkstra over the
	// visibility graph of a, b and every rect corner. Independent of the mesh and of
	// pathThrough.
	double refereeLength(const std::vector<Rect>& rects, const Vec2i64& a, const Vec2i64& b) {
		std::vector<Vec2i64> nodes{a, b};
		for (const Rect& r : rects) {
			nodes.insert(nodes.end(), {{r.x0, r.y0}, {r.x1, r.y0}, {r.x1, r.y1}, {r.x0, r.y1}});
		}
		auto visible = [&](const Vec2i64& p, const Vec2i64& q) {
			for (const Rect& r : rects) {
				if (crossesInterior(p, q, r)) {
					return false;
				}
			}
			return true;
		};
		std::vector<double> dist(nodes.size(), std::numeric_limits<double>::infinity());
		std::vector<char>	done(nodes.size(), 0);
		dist[0] = 0.0;
		for (std::size_t round = 0; round < nodes.size(); ++round) {
			std::size_t u = nodes.size();
			for (std::size_t i = 0; i < nodes.size(); ++i) {
				if (done[i] == 0 && (u == nodes.size() || dist[i] < dist[u])) {
					u = i;
				}
			}
			if (u == nodes.size() || std::isinf(dist[u])) {
				break;
			}
			done[u] = 1;
			for (std::size_t v = 0; v < nodes.size(); ++v) {
				if (done[v] == 0 && visible(nodes[u], nodes[v])) {
					const double dx = static_cast<double>(nodes[v].x - nodes[u].x);
					const double dy = static_cast<double>(nodes[v].y - nodes[u].y);
					dist[v]			= std::min(dist[v], dist[u] + std::sqrt(dx * dx + dy * dy));
				}
			}
		}
		return dist[1];
	}

	double walkLength(const PathResult& p) {
		double length = 0.0;
		for (std::size_t i = 1; i < p.points.size(); ++i) {
			const double dx = static_cast<double>(p.points[i].x - p.points[i - 1].x);
			const double dy = static_cast<double>(p.points[i].y - p.points[i - 1].y);
			length += std::sqrt(dx * dx + dy * dy);
		}
		return length;
	}

	std::vector<Vec2i64> refereePoints(std::size_t n) {
		std::mt19937		 rng(19);
		std::vector<Vec2i64> out;
		while (out.size() < n) {
			const Vec2i64 p{1 + static_cast<std::int64_t>(rng() % 5998), 1 + static_cast<std::int64_t>(rng() % 3998)};
			bool		  inside = false;
			for (const Rect& r : refereeRects()) {
				inside = inside || (p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1);
			}
			if (!inside) {
				out.push_back(p);
			}
		}
		return out;
	}

} // namespace

// The search returns the shortest walk at radius 0, checked against an exact referee that
// shares no code with it: wrapping a wall end, doubling back into the C's pocket (the
// goal behind the start, past both bounds of any funnel heading out), threading posts.
// The walk's corners are rounded to the mm, so a few mm either way.
TEST(PathQuery, ShortestWalkMatchesVisibilityGraph) {
	const NavMesh				m	   = buildRefereeMesh(refereeRects());
	const std::vector<Vec2i64> points = refereePoints(160);
	int						   checked = 0;
	for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
		const PathResult walk = pathThrough(m, points[i], points[i + 1], 0);
		ASSERT_TRUE(walk.reachable) << "pair " << i;
		const double exact = refereeLength(refereeRects(), points[i], points[i + 1]);
		EXPECT_NEAR(walkLength(walk), exact, 4.0) << "from (" << points[i].x << ", " << points[i].y << ") to (" << points[i + 1].x << ", "
												 << points[i + 1].y << ")";
		++checked;
	}
	// Into the C's pocket from just outside its back: out round an end and back.
	const Vec2i64	 start{5400, 3000};
	const Vec2i64	 goal{4300, 3000};
	const PathResult uTurn = pathThrough(m, start, goal, 0);
	ASSERT_TRUE(uTurn.reachable);
	EXPECT_NEAR(walkLength(uTurn), refereeLength(refereeRects(), start, goal), 4.0);
	EXPECT_EQ(checked, 80);
}

namespace {

	std::int64_t between(std::mt19937& rng, std::int64_t lo, std::int64_t hi) {
		return lo + static_cast<std::int64_t>(rng() % static_cast<std::uint32_t>(hi - lo + 1));
	}

	// A wall 200 mm thick across the box, its ends somewhere inside, and 3 to 8 square
	// posts, from `rng`.
	std::vector<Rect> wallAndPosts(std::mt19937& rng) {
		auto			  between = [&](std::int64_t lo, std::int64_t hi) { return ::between(rng, lo, hi); };
		std::vector<Rect> rects;
		const std::int64_t wy = between(1500, 2500);
		rects.push_back({between(300, 2500), wy, between(3500, 5700), wy + 200});
		const int posts = 3 + static_cast<int>(rng() % 6);
		for (int k = 0; k < posts; ++k) {
			const std::int64_t x = between(100, 5500);
			const std::int64_t y = between(100, 3500);
			const std::int64_t s = between(100, 500);
			rects.push_back({x, y, x + s, y + s});
		}
		return rects;
	}

} // namespace

// Walks round a wall and posts against the referee, over many layouts: the shortest walk
// often wraps the wall's end to reach its far side. Two ways the search kept a longer
// one: a goal label pulled exactly kept out a newcomer by its funnel's reach, a bound,
// though the newcomer's walk was shorter (seed 238); and a cone restarted at a corner
// past the next portal, which over-scored corridors that wrap (seeds 7, 8, 14, 17, 20).
// Layouts whose rects touch are skipped: the referee walks between touching rects, the
// mesh does not.
TEST(PathQuery, ShortestWalksWrapWallEnds) {
	int	   checked = 0;
	int	   longer  = 0;
	double worst   = 0.0;
	for (std::uint32_t seed = 0; seed < 300; ++seed) {
		std::mt19937			rng(seed);
		const std::vector<Rect> rects = wallAndPosts(rng);
		bool					touch = false;
		for (std::size_t i = 0; i < rects.size(); ++i) {
			for (std::size_t j = 0; j < i; ++j) {
				const Rect& p = rects[i];
				const Rect& q = rects[j];
				touch		  = touch || !(p.x1 < q.x0 || q.x1 < p.x0 || p.y1 < q.y0 || q.y1 < p.y0);
			}
		}
		if (touch) {
			continue;
		}
		const NavMesh m = buildRefereeMesh(rects);
		for (int pair = 0; pair < 20; ++pair) {
			const Vec2i64 a{between(rng, 1, 5999), between(rng, 1, 3999)};
			const Vec2i64 b{between(rng, 1, 5999), between(rng, 1, 3999)};
			bool		  inside = false;
			for (const Rect& r : rects) {
				for (const Vec2i64& p : {a, b}) {
					inside = inside || (p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1);
				}
			}
			if (inside) {
				continue;
			}
			const PathResult walk = pathThrough(m, a, b, 0);
			const double	 exact = refereeLength(rects, a, b);
			ASSERT_TRUE(walk.reachable);
			EXPECT_NEAR(walkLength(walk), exact, 4.0) << "seed " << seed << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << ")";
			++checked;
			longer += walkLength(walk) > exact + 4.0 ? 1 : 0;
			worst = std::max(worst, (walkLength(walk) - exact) / exact);
		}
	}
	EXPECT_GT(checked, 2000);
	::testing::Test::RecordProperty("pairs", checked);
	::testing::Test::RecordProperty("longer", longer);
	::testing::Test::RecordProperty("worstExcessPermille", std::to_string(1000.0 * worst));
}

// The same layouts for a disc of the colonists' radius, against the referee with every rect
// grown by the radius and the box's edge, a wall like any rect, moved in by it: its walks
// keep the radius from each, rounding a corner on the grown square's corner where the disc
// could cut the arc, so it is no shorter than the disc's shortest walk. A walk longer than
// it by more than kDiscSlackMm took a longer corridor than it needed.
TEST(PathQuery, ShortestDiscWalksWrapWallEnds) {
	constexpr std::int64_t kRadius		= 300;
	constexpr double	   kDiscSlackMm = 4.0;
	int					   checked		= 0;
	int					   longer		= 0;
	int					   refused		= 0; // the referee finds a way the walk does not
	double				   worst		= 0.0;
	double				   worstMm		= 0.0;
	for (std::uint32_t seed = 0; seed < 600; ++seed) {
		std::mt19937			rng(seed);
		const std::vector<Rect> rects = wallAndPosts(rng);
		bool					touch = false;
		for (std::size_t i = 0; i < rects.size(); ++i) {
			for (std::size_t j = 0; j < i; ++j) {
				const Rect& p = rects[i];
				const Rect& q = rects[j];
				touch		  = touch || !(p.x1 < q.x0 || q.x1 < p.x0 || p.y1 < q.y0 || q.y1 < p.y0);
			}
		}
		if (touch) {
			continue;
		}
		std::vector<Rect> grown{{-1000, -1000, kRadius, 5000}, {6000 - kRadius, -1000, 7000, 5000}, {-1000, -1000, 7000, kRadius}, {-1000, 4000 - kRadius, 7000, 5000}};
		for (const Rect& r : rects) {
			grown.push_back({r.x0 - kRadius, r.y0 - kRadius, r.x1 + kRadius, r.y1 + kRadius});
		}
		const NavMesh m = buildRefereeMesh(rects);
		for (int pair = 0; pair < 20; ++pair) {
			const Vec2i64 a{between(rng, 1, 5999), between(rng, 1, 3999)};
			const Vec2i64 b{between(rng, 1, 5999), between(rng, 1, 3999)};
			bool		  inside = false;
			for (const Rect& r : grown) {
				for (const Vec2i64& p : {a, b}) {
					inside = inside || (p.x >= r.x0 && p.x <= r.x1 && p.y >= r.y0 && p.y <= r.y1);
				}
			}
			if (inside) {
				continue;
			}
			const PathResult walk  = pathThrough(m, a, b, kRadius);
			const double	 bound = refereeLength(grown, a, b);
			if (std::isinf(bound)) {
				continue;
			}
			if (!walk.reachable) {
				++refused;
				continue;
			}
			++checked;
			const double excess = walkLength(walk) - bound;
			EXPECT_LE(excess, kDiscSlackMm) << "seed " << seed << " from (" << a.x << ", " << a.y << ") to (" << b.x << ", " << b.y << ")";
			longer += excess > kDiscSlackMm ? 1 : 0;
			worst	= std::max(worst, excess / bound);
			worstMm = std::max(worstMm, excess);
		}
	}
	EXPECT_GT(checked, 1000);
	EXPECT_EQ(refused, 0);
	::testing::Test::RecordProperty("pairs", checked);
	::testing::Test::RecordProperty("longer", longer);
	::testing::Test::RecordProperty("worstExcessPermille", std::to_string(1000.0 * worst));
	::testing::Test::RecordProperty("worstExcessMm", std::to_string(worstMm));
}
// --- Limits on a query ----------------------------------------------------------

// maxCost: a path costing at most the cap (to within a millimetre) is the path found
// without one; a cap further below its cost returns no path and says the limit stopped it; a goal walled off returns NoPath
// when everything that could reach it cost under the cap, LimitReached when the cap cut
// corridors off first.
TEST(PathQuery, MaxCostFindsPathsWithinItAndNoOthers) {
	const NavMesh	 m = hedgeMesh(200);
	const Vec2i64	 start{10100, 4000};
	const Vec2i64	 goal{10100, 8000};
	const PathResult free = pathThrough(m, start, goal, 300);
	ASSERT_EQ(free.stop, PathStop::Found);
	for (const double cap : {free.cost - 0.5, free.cost, free.cost * 2.0}) {
		const PathResult capped = pathThrough(m, start, goal, 300, {}, {.maxCost = cap});
		EXPECT_EQ(capped.stop, PathStop::Found);
		EXPECT_EQ(capped.points, free.points);
		EXPECT_DOUBLE_EQ(capped.cost, free.cost);
	}
	const PathResult under = pathThrough(m, start, goal, 300, {}, {.maxCost = free.cost - 1.5});
	EXPECT_EQ(under.stop, PathStop::LimitReached);
	EXPECT_FALSE(under.reachable);
	EXPECT_TRUE(under.points.empty());

	NavMeshInput in;
	in.polygons.push_back({{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}}, false, 1});
	in.polygons.push_back({{{3000, 3000}, {7000, 3000}, {7000, 7000}, {3000, 7000}}, true, -5});
	in.polygons.push_back({{{3000, 3000}, {7000, 3000}, {7000, 3200}, {3000, 3200}}, true, -6});
	const NavMesh  walled = buildNavMesh(in);
	const Vec2i64 inside{5000, 5000};
	// The rock fills the square: the goal is on it, an untraversable face, so NoPath at once.
	EXPECT_EQ(pathThrough(walled, {1000, 1000}, inside, 0, {}, {.maxCost = 1.0}).stop, PathStop::NoPath);
}

// A goal sealed in a room the belief knows: the open set runs dry (NoPath) with no cap or
// a cap above everything reachable, and a low cap stops it first (LimitReached).
TEST(PathQuery, SealedGoalStopsOnTheCapOrRunsDry) {
	const NavMesh					  m = buildNavMesh(buildBoxRoom(/*withDoor=*/false));
	std::unordered_set<std::uint64_t> segs{static_cast<std::uint64_t>(kSegS), static_cast<std::uint64_t>(kSegN), static_cast<std::uint64_t>(kSegW),
										   static_cast<std::uint64_t>(kSegE)};
	std::unordered_set<std::uint64_t> noOps;
	const BeliefFilter				  belief{&segs, &noOps};
	const PathResult				  dry = pathThrough(m, kOutside, kInside, 0, belief);
	EXPECT_EQ(dry.stop, PathStop::NoPath);
	EXPECT_EQ(pathThrough(m, kOutside, kInside, 0, belief, {.maxCost = 1.0e9}).stop, PathStop::NoPath);
	const PathResult capped = pathThrough(m, kOutside, kInside, 0, belief, {.maxCost = 10.0});
	EXPECT_EQ(capped.stop, PathStop::LimitReached);
	EXPECT_LE(capped.expansions, dry.expansions);
}

// maxExpansions: the same query stops at the same place every time, after exactly that
// many expansions; a limit above what the search needs changes nothing.
TEST(PathQuery, MaxExpansionsStopsDeterministically) {
	const NavMesh	 m	  = buildRefereeMesh(refereeRects());
	const Vec2i64	 a{200, 200};
	const Vec2i64	 b{5800, 3800};
	const PathResult full = pathThrough(m, a, b, 0);
	ASSERT_EQ(full.stop, PathStop::Found);
	ASSERT_GT(full.expansions, 4u);
	EXPECT_EQ(full.expansions, static_cast<std::uint32_t>(full.nodesExpanded));
	const PathResult cut  = pathThrough(m, a, b, 0, {}, {.maxExpansions = 4});
	const PathResult again = pathThrough(m, a, b, 0, {}, {.maxExpansions = 4});
	EXPECT_EQ(cut.stop, PathStop::LimitReached);
	EXPECT_EQ(cut.expansions, 4u);
	EXPECT_EQ(again.expansions, cut.expansions);
	EXPECT_EQ(again.peakOpenSet, cut.peakOpenSet);
	const PathResult roomy = pathThrough(m, a, b, 0, {}, {.maxExpansions = full.expansions + 1});
	EXPECT_EQ(roomy.stop, PathStop::Found);
	EXPECT_EQ(roomy.points, full.points);
	EXPECT_EQ(roomy.nodesExpanded, full.nodesExpanded);
	// The goal accepted on the pop after the count: Found, not stopped. Exactly at the
	// count it is found as without one.
	const PathResult exact = pathThrough(m, a, b, 0, {}, {.maxExpansions = full.expansions});
	EXPECT_EQ(exact.stop, PathStop::Found);
	EXPECT_EQ(exact.points, full.points);
	const PathResult atGoal = pathThrough(m, a, b, 0, {}, {.maxExpansions = full.expansions - 1});
	EXPECT_EQ(atGoal.stop, PathStop::Found);
	EXPECT_EQ(atGoal.points, full.points);
	EXPECT_EQ(atGoal.expansions, full.expansions);
}

// --- Searches run a slice at a time -----------------------------------------------

namespace {

	struct SearchRun {
		SearchProgress			  progress = SearchProgress::Running;
		PathResult				  result;
		std::int32_t			  goal		 = -1;
		std::int64_t			  expansions = 0;
		std::vector<std::int32_t> touched;
	};

	// The search run to its end in slices of `slice` expansions; each call may do one
	// more, the goal it accepts.
	SearchRun runInSlices(const NavView& view, const SearchArgs& args, std::uint32_t slice) {
		SearchState	   search(args);
		SearchProgress progress = SearchProgress::Running;
		do {
			const std::int64_t before = search.expansions();
			progress				  = search.step(view, slice);
			EXPECT_LE(search.expansions() - before, static_cast<std::int64_t>(slice) + 1);
		} while (progress == SearchProgress::Running);
		const std::span<const std::int32_t> touched = search.touchedTriangles();
		return {progress, search.result(), search.goalIndex(), search.expansions(), {touched.begin(), touched.end()}};
	}

	void expectSameRun(const SearchRun& a, const SearchRun& b, const std::string& what) {
		EXPECT_EQ(a.progress, b.progress) << what;
		EXPECT_EQ(a.result.stop, b.result.stop) << what;
		EXPECT_EQ(a.result.points, b.result.points) << what;
		EXPECT_EQ(a.result.cost, b.result.cost) << what;
		EXPECT_EQ(a.result.nodesExpanded, b.result.nodesExpanded) << what;
		EXPECT_EQ(a.result.peakOpenSet, b.result.peakOpenSet) << what;
		EXPECT_EQ(a.goal, b.goal) << what;
		EXPECT_EQ(a.expansions, b.expansions) << what;
		EXPECT_EQ(a.touched, b.touched) << what;
	}

	SearchArgs argsFor(const Vec2i64& start, std::vector<Vec2i64> goals, std::int64_t radius, const BeliefFilter& belief = {},
					   std::uint32_t goalSidePhase = kGoalSidePhase, double maxCost = std::numeric_limits<double>::infinity()) {
		return {start, std::move(goals), radius, copyBelief(belief), maxCost, goalSidePhase};
	}

	struct SearchCase {
		const NavMesh* mesh;
		SearchArgs	   args;
		std::string	   name;
	};

	// Searches over the fixtures above: found and exhausted, from the start only, from the
	// goal first, switching between them, nearest-of, round a hedge, belief-gated, cost-capped.
	std::vector<SearchCase> searchCases(const NavMesh& referee, const NavMesh& hedge, const NavMesh& sealedRoom, const NavMesh& doorRoom) {
		static const std::unordered_set<std::uint64_t> walls{kSegS, kSegN, kSegW, kSegE};
		static const std::unordered_set<std::uint64_t> door{kDoorOp};
		static const std::unordered_set<std::uint64_t> noDoors;
		std::vector<SearchCase>							out;
		const std::vector<Vec2i64>						points = refereePoints(24);
		for (std::size_t i = 0; i + 1 < points.size(); i += 2) {
			for (const std::uint32_t phase : {0U, 5U, kGoalSidePhase}) {
				for (const std::int64_t radius : {0, 300}) {
					out.push_back({&referee, argsFor(points[i], {points[i + 1]}, radius, {}, phase),
								   "referee pair " + std::to_string(i) + " phase " + std::to_string(phase) + " radius " + std::to_string(radius)});
				}
			}
			out.push_back({&referee, argsFor(points[i], {points[i + 1], {2600, 200}, {5400, 3000}, {300, 3700}}, 300), "nearest-of " + std::to_string(i)});
		}
		for (const std::uint32_t phase : {0U, 5U, kGoalSidePhase}) {
			out.push_back({&hedge, argsFor({10100, 4000}, {{10100, 8000}}, 300, {}, phase), "hedge phase " + std::to_string(phase)});
			out.push_back({&sealedRoom, argsFor(kOutside, {kInside}, 0, BeliefFilter{&walls, &noDoors}, phase), "sealed room phase " + std::to_string(phase)});
			out.push_back({&doorRoom, argsFor(kOutside, {kInside}, 150, BeliefFilter{&walls, &door}, phase), "door room phase " + std::to_string(phase)});
			out.push_back({&hedge, argsFor({10100, 4000}, {{10100, 8000}}, 300, {}, phase, 100.0), "capped hedge phase " + std::to_string(phase)});
		}
		return out;
	}

} // namespace

// A search run in slices of any size expands the same labels in the same order and
// answers the same, node for node, as one run to the end; with no goal-side phase that
// answer is pathThrough's.
TEST(PathQuerySearch,SlicesOfAnySizeGiveTheOneShotAnswer) {
	const NavMesh referee	 = buildRefereeMesh(refereeRects());
	const NavMesh hedge		 = hedgeMesh(200);
	const NavMesh sealedRoom = buildNavMesh(buildBoxRoom(false));
	const NavMesh doorRoom	 = buildNavMesh(buildBoxRoom(true));
	int			  switched	 = 0;
	for (const SearchCase& c : searchCases(referee, hedge, sealedRoom, doorRoom)) {
		const NavView	view = navView(*c.mesh);
		const SearchRun whole = runInSlices(view, c.args, kRunToEnd);
		for (const std::uint32_t slice : {1U, 7U, 1000U}) {
			expectSameRun(runInSlices(view, c.args, slice), whole, c.name + " in slices of " + std::to_string(slice));
		}
		if (c.args.goalSidePhase == 0 && c.args.goals.size() == 1) {
			const PathResult once = pathThrough(view, c.args.start, c.args.goals[0], c.args.agentRadiusMm, {}, {.maxCost = c.args.maxCost});
			if (c.args.belief.truth) {
				EXPECT_EQ(once.points, whole.result.points) << c.name;
				EXPECT_EQ(once.nodesExpanded, whole.result.nodesExpanded) << c.name;
			}
		}
		switched += c.args.goalSidePhase == 5 && whole.expansions > 5 ? 1 : 0;
		// Restarted, it answers again as it did.
		SearchState search(c.args);
		search.step(view, 3);
		search.restart();
		search.step(view, kRunToEnd);
		EXPECT_EQ(search.result().points, whole.result.points) << c.name;
		EXPECT_EQ(search.expansions(), whole.expansions) << c.name;
	}
	EXPECT_GT(switched, 5) << "some searches must outrun the goal-side phase and switch to the start";
}

// A goal sealed in a room the belief knows: from the goal the search runs dry at once;
// from the start alone it floods everything outside first.
TEST(PathQuerySearch,GoalSidePhaseFindsASealedGoalCheaply) {
	const NavMesh							m = buildNavMesh(buildBoxRoom(false));
	const std::unordered_set<std::uint64_t> walls{kSegS, kSegN, kSegW, kSegE};
	const std::unordered_set<std::uint64_t> noDoors;
	const NavView							view = navView(m);
	const SearchRun phase = runInSlices(view, argsFor(kOutside, {kInside}, 0, BeliefFilter{&walls, &noDoors}), kRunToEnd);
	const SearchRun ahead = runInSlices(view, argsFor(kOutside, {kInside}, 0, BeliefFilter{&walls, &noDoors}, 0), kRunToEnd);
	EXPECT_EQ(phase.progress, SearchProgress::Exhausted);
	EXPECT_EQ(phase.result.stop, PathStop::NoPath);
	EXPECT_EQ(ahead.progress, SearchProgress::Exhausted);
	EXPECT_LT(phase.expansions * 4, ahead.expansions);
	// Found from the goal side, the walk still runs from the start.
	const std::unordered_set<std::uint64_t> door{kDoorOp};
	const NavMesh							withDoor = buildNavMesh(buildBoxRoom(true));
	const SearchRun							in = runInSlices(navView(withDoor), argsFor(kOutside, {kInside}, 0, BeliefFilter{&walls, &door}), kRunToEnd);
	ASSERT_EQ(in.progress, SearchProgress::Found);
	EXPECT_EQ(in.result.points.front(), kOutside);
	EXPECT_EQ(in.result.points.back(), kInside);
	EXPECT_EQ(in.goal, 0);
}

// Nearest-of: the answer is the goal with the shortest walk, against the exact referee,
// not the one nearest in a straight line. Goals no walk reaches drop out.
TEST(PathQuerySearch,NearestOfReturnsTheGoalWithTheShortestWalk) {
	const NavMesh m	   = buildRefereeMesh(refereeRects());
	const NavView view = navView(m);
	// Under the slab: the goal just over it is nearer in a line, the one further down is
	// nearer on foot.
	const SearchRun slab = runInSlices(view, argsFor({2600, 1200}, {{2600, 1800}, {2600, 200}}, 0), kRunToEnd);
	ASSERT_EQ(slab.progress, SearchProgress::Found);
	EXPECT_EQ(slab.goal, 1);
	EXPECT_EQ(slab.result.points.back(), (Vec2i64{2600, 200}));

	const std::vector<Vec2i64> points = refereePoints(120);
	int						   checked = 0;
	for (std::size_t i = 0; i + 5 < points.size(); i += 6) {
		const std::vector<Vec2i64> goals(points.begin() + static_cast<std::ptrdiff_t>(i + 1), points.begin() + static_cast<std::ptrdiff_t>(i + 6));
		std::vector<double>		   exact;
		for (const Vec2i64& g : goals) {
			exact.push_back(refereeLength(refereeRects(), points[i], g));
		}
		const double	shortest = *std::min_element(exact.begin(), exact.end());
		const SearchRun run		 = runInSlices(view, argsFor(points[i], goals, 0), 7);
		ASSERT_EQ(run.progress, SearchProgress::Found) << "start " << i;
		ASSERT_GE(run.goal, 0);
		EXPECT_EQ(run.result.points.back(), goals[static_cast<std::size_t>(run.goal)]);
		EXPECT_NEAR(walkLength(run.result), shortest, 4.0) << "start " << i;
		EXPECT_LE(exact[static_cast<std::size_t>(run.goal)], shortest + 8.0) << "start " << i;
		++checked;
	}
	EXPECT_EQ(checked, 20);

	// A goal inside a post stands on no ground: it drops out, and the other answers.
	const SearchRun dropped = runInSlices(view, argsFor({200, 200}, {{1400, 2600}, {5800, 200}}, 0), kRunToEnd);
	ASSERT_EQ(dropped.progress, SearchProgress::Found);
	EXPECT_EQ(dropped.goal, 1);
	const SearchRun none = runInSlices(view, argsFor({200, 200}, {{1400, 2600}, {2400, 3000}}, 0), kRunToEnd);
	EXPECT_EQ(none.progress, SearchProgress::Exhausted);
	EXPECT_EQ(none.goal, -1);
}

namespace {

	// Posts on a 20 x 12 m field, as one patched mesh.
	PatchableMesh postField() {
		PatchableMesh mesh;
		PatchEdits	  edits;
		edits.adds.push_back({{{0, 0}, {20000, 0}, {20000, 12000}, {0, 12000}}, false, 1});
		for (std::int64_t x = 1500; x < 19000; x += 1700) {
			for (std::int64_t y = 1200; y < 11000; y += 1500) {
				const std::int64_t s = 300 + (x * 7 + y * 3) % 500;
				edits.adds.push_back({{{x, y}, {x + s, y}, {x + s, y + s}, {x, y + s}}, true, kTreeTag});
			}
		}
		mesh.applyPatch(edits);
		mesh.refreshWidths();
		return mesh;
	}

} // namespace

// Two searches on one mesh at once, interleaved a few expansions at a time on one thread
// and run side by side on two, answer as each does alone: a search's state is its own.
TEST(PathQuerySearch,ReadersSharingOneMeshAnswerAsAlone) {
	const PatchableMesh mesh = postField();
	const NavView		view = mesh.view();
	const SearchArgs	a		= argsFor({500, 500}, {{19500, 11500}}, 300);
	const SearchArgs	b		= argsFor({19000, 600}, {{1000, 11000}, {10000, 6000}}, 150);
	const SearchRun		aloneA	= runInSlices(view, a, kRunToEnd);
	const SearchRun		aloneB	= runInSlices(view, b, kRunToEnd);
	ASSERT_EQ(aloneA.progress, SearchProgress::Found);
	ASSERT_EQ(aloneB.progress, SearchProgress::Found);

	SearchState first(a);
	SearchState second(b);
	while (first.progress() == SearchProgress::Running || second.progress() == SearchProgress::Running) {
		first.step(view, 3);
		second.step(view, 5);
	}
	EXPECT_EQ(first.result().points, aloneA.result.points);
	EXPECT_EQ(first.expansions(), aloneA.expansions);
	EXPECT_EQ(std::vector<std::int32_t>(first.touchedTriangles().begin(), first.touchedTriangles().end()), aloneA.touched);
	EXPECT_EQ(second.result().points, aloneB.result.points);
	EXPECT_EQ(second.result().cost, aloneB.result.cost);
	EXPECT_EQ(second.goalIndex(), aloneB.goal);
	EXPECT_EQ(std::vector<std::int32_t>(second.touchedTriangles().begin(), second.touchedTriangles().end()), aloneB.touched);

	std::array<std::vector<SearchRun>, 2> side;
	std::thread							  other([&] {
		  for (int i = 0; i < 20; ++i) {
			  side[1].push_back(runInSlices(view, b, 11));
		  }
	  });
	for (int i = 0; i < 20; ++i) {
		side[0].push_back(runInSlices(view, a, 13));
	}
	other.join();
	for (int i = 0; i < 20; ++i) {
		expectSameRun(side[0][static_cast<std::size_t>(i)], aloneA, "thread 0 run " + std::to_string(i));
		expectSameRun(side[1][static_cast<std::size_t>(i)], aloneB, "thread 1 run " + std::to_string(i));
	}
}

// The touched set holds every triangle the search read: wreck every triangle outside it
// (tags and widths) and the search answers exactly as before.
TEST(PathQuerySearch,TouchedTrianglesAreAllItReads) {
	const NavMesh referee	 = buildRefereeMesh(refereeRects());
	const NavMesh hedge		 = hedgeMesh(200);
	const NavMesh sealedRoom = buildNavMesh(buildBoxRoom(false));
	const NavMesh doorRoom	 = buildNavMesh(buildBoxRoom(true));
	int			  wrecked	 = 0;
	for (const SearchCase& c : searchCases(referee, hedge, sealedRoom, doorRoom)) {
		const SearchRun run = runInSlices(navView(*c.mesh), c.args, kRunToEnd);
		const std::unordered_set<std::int32_t> touched(run.touched.begin(), run.touched.end());
		EXPECT_EQ(touched.size(), run.touched.size()) << c.name << ": each id once";
		NavMesh wreck	 = *c.mesh;
		wreck.generation = nextMeshGeneration();
		for (std::int32_t t = 0; t < static_cast<std::int32_t>(wreck.triangles.size()); ++t) {
			if (touched.count(t) != 0) {
				continue;
			}
			NavTriangle& tri	  = wreck.triangles[static_cast<std::size_t>(t)];
			tri.faceBlocker		  = -77;
			tri.faceOpening		  = kNoOpening;
			tri.edgePairWidthMm	  = {0, 0, 0};
			tri.edgeOpening		  = {kNoOpening, kNoOpening, kNoOpening};
			tri.edgeClearWidthMm  = {0, 0, 0};
			++wrecked;
		}
		expectSameRun(runInSlices(navView(wreck), c.args, 7), run, c.name);
	}
	EXPECT_GT(wrecked, 1000) << "the searches must leave most of the meshes unread for the test to bind";
}

namespace {

	// A pen open only on the side away from the start, filled with posts: from a goal
	// inside it by its closed side, a search must cross much of the pen to get out.
	PatchableMesh postedPen() {
		PatchableMesh mesh;
		PatchEdits	  edits;
		auto		  solid = [&](std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
			 edits.adds.push_back({{{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}}, true, kTreeTag});
		};
		edits.adds.push_back({{{0, 0}, {120000, 0}, {120000, 120000}, {0, 120000}}, false, 1});
		solid(40000, 20000, 40500, 100000);
		solid(40000, 20000, 100000, 20500);
		solid(40000, 99500, 100000, 100000);
		for (std::int64_t x = 42000; x < 98000; x += 1500) {
			for (std::int64_t y = 22000; y < 98000; y += 1500) {
				solid(x, y, x + 400, y + 400);
			}
		}
		mesh.applyPatch(edits);
		mesh.refreshWidths();
		return mesh;
	}

} // namespace

// At the default phase length, a goal whose side runs neither dry nor into the start
// within it: the search switches to the start side and answers as a start-only search.
TEST(PathQuerySearch,GoalSidePhaseSwitchesAtItsDefaultLength) {
	const PatchableMesh mesh  = postedPen();
	const NavView		view  = mesh.view();
	const Vec2i64		start{5000, 60000};
	const Vec2i64		goal{41500, 60000};
	const SearchRun		phase = runInSlices(view, argsFor(start, {goal}, 300), kRunToEnd);
	const SearchRun		ahead = runInSlices(view, argsFor(start, {goal}, 300, {}, 0), kRunToEnd);
	ASSERT_EQ(phase.progress, SearchProgress::Found);
	ASSERT_EQ(ahead.progress, SearchProgress::Found);
	EXPECT_GT(phase.expansions, static_cast<std::int64_t>(kGoalSidePhase)) << "the goal side neither ran dry nor met the start";
	EXPECT_EQ(phase.result.points.front(), start);
	EXPECT_EQ(phase.result.points.back(), goal);
	EXPECT_NEAR(walkLength(phase.result), walkLength(ahead.result), 2.0);
	expectSameRun(runInSlices(view, argsFor(start, {goal}, 300), 7), phase, "pen in slices of 7");
}

// More triangles round one vertex than any fixed count: a disc of ground with a narrow
// notch cut down to its centre, so the centre is a corner of the outline that every rim
// point meets. A disc passing above the notch walks straight on, and the touched set
// holds the whole fan.
TEST(PathQuerySearch,HighDegreeVertexWalksItsWholeFan) {
	constexpr int		 kSlices = 300;
	constexpr int		 kNotch	 = kSlices * 3 / 4; // the rim point straight below the centre
	PatchableMesh		 mesh;
	PatchEdits			 edits;
	std::vector<Vec2i64> outline{{0, 0}};
	for (int step = 1; step < kSlices; ++step) {
		const int	 k	  = (kNotch + step) % kSlices;
		const double turn = 2.0 * 3.14159265358979323846 * k / kSlices;
		outline.push_back({std::llround(100000.0 * std::cos(turn)), std::llround(100000.0 * std::sin(turn))});
	}
	edits.adds.push_back({outline, false, 1});
	mesh.applyPatch(edits);
	mesh.refreshWidths();
	const NavView			  view = mesh.view();
	std::vector<std::int32_t> fan;
	for (std::int32_t t = 0; t < view.triangleSlots(); ++t) {
		if (!view.isLive(t)) {
			continue;
		}
		for (const std::uint32_t v : view.triangle(t).v) {
			if (view.vertex(v) == Vec2i64{0, 0}) {
				fan.push_back(t);
			}
		}
	}
	ASSERT_GT(fan.size(), 256U);
	const Vec2i64	start{-50000, 400};
	const Vec2i64	goal{50000, 400};
	const SearchRun run = runInSlices(view, argsFor(start, {goal}, 300, {}, 0), kRunToEnd);
	ASSERT_EQ(run.progress, SearchProgress::Found);
	EXPECT_EQ(run.result.points, (std::vector<Vec2i64>{start, goal})) << "the notch's corner is below the disc's way";
	const std::unordered_set<std::int32_t> touched(run.touched.begin(), run.touched.end());
	for (const std::int32_t t : fan) {
		EXPECT_EQ(touched.count(t), 1U) << "fan triangle " << t;
	}
}

// A search never reads the component labels: they hold the whole mesh, not what the
// search has read, so a goal across water is still searched for (the search explores and
// runs dry). pathThrough keeps the labels' reject, before any search. Labels read every
// wall open, so a room sealed by walls is searched for by pathThrough too.
TEST(PathQuerySearch,SearchNeverConsultsTheComponentLabels) {
	PatchableMesh split;
	PatchEdits	  edits;
	edits.adds.push_back({{{0, 0}, {6000, 0}, {6000, 6000}, {0, 6000}}, false, 1});
	edits.adds.push_back({{{0, 2800}, {6000, 2800}, {6000, 3200}, {0, 3200}}, true, -1});
	split.applyPatch(edits);
	split.refreshWidths();
	const NavView	splitView = split.view();
	const SearchRun run		  = runInSlices(splitView, argsFor({1000, 1000}, {{1000, 5000}, {5000, 5000}}, 0), kRunToEnd);
	EXPECT_EQ(run.progress, SearchProgress::Exhausted);
	EXPECT_GT(run.expansions, 0) << "the goals across the water were searched for, not dropped";
	const PathResult across = pathThrough(splitView, {1000, 1000}, {1000, 5000}, 0);
	EXPECT_EQ(across.stop, PathStop::NoPath);
	EXPECT_EQ(across.nodesExpanded, 0) << "pathThrough rejects on the labels before it searches";

	PatchableMesh room;
	PatchEdits	  roomEdits;
	roomEdits.adds = buildBoxRoom(false).polygons;
	room.applyPatch(roomEdits);
	room.refreshWidths();
	const PathResult sealed = pathThrough(room.view(), kOutside, kInside, 0);
	EXPECT_EQ(sealed.stop, PathStop::NoPath);
	EXPECT_GT(sealed.nodesExpanded, 0) << "walls never split a label, so the sealed room is searched";
}
