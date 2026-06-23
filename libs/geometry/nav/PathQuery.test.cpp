#include "PathQuery.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"
#include "NavMesh.h"

#include <array>
#include <cstdint>
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

TEST(PathQuery, SliverInOpenFloorDoesNotBlock) {
	// A long thin open strip triangulates into high-aspect slivers with no obstacle
	// edges. An agent must route end to end across them; the sliver altitude must not
	// be mistaken for a narrow corridor.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {6000, 0}, {6000, 40}, {0, 40}}));
	NavMesh m = buildNavMesh(in);

	const Vec2i64 a{200, 20};
	const Vec2i64 b{5800, 20};
	PathResult	  p = pathThrough(m, a, b, 15); // diameter 30, well inside the 40 mm strip
	ASSERT_TRUE(p.reachable) << "open-floor slivers must not block an agent that fits the strip";
	EXPECT_EQ(p.points.front(), a);
	EXPECT_EQ(p.points.back(), b);
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
