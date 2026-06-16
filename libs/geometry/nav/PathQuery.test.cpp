#include "PathQuery.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"
#include "NavMesh.h"

#include <array>
#include <cstdint>
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
