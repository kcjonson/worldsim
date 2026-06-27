#include "NavMesh.h"
#include "../core/Int128.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"
#include "NavMeshRealRings.test.h"

#include <array>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;
using namespace geometry::nav;

namespace {

	using Tri = std::array<std::uint32_t, 3>;

	// Doubled signed area of a triangle (CCW positive), exact in 128-bit.
	Int128 triArea2(const std::vector<Vec2i64>& v, const Tri& t) {
		return cross(v[t[1]] - v[t[0]], v[t[2]] - v[t[0]]);
	}

	Int128 totalArea2(const NavMesh& m) {
		Int128 acc(0);
		for (const NavTriangle& t : m.triangles) {
			acc = acc + triArea2(m.vertices, t.v);
		}
		return acc;
	}

	bool allCcw(const NavMesh& m) {
		for (const NavTriangle& t : m.triangles) {
			if (orientation(m.vertices[t.v[0]], m.vertices[t.v[1]], m.vertices[t.v[2]]) !=
				Orientation::CounterClockwise) {
				return false;
			}
		}
		return true;
	}

	// Floored integer centroid; strictly interior for the well-shaped triangles
	// these tests produce.
	Vec2i64 centroid(const std::vector<Vec2i64>& v, const Tri& t) {
		return {(v[t[0]].x + v[t[1]].x + v[t[2]].x) / 3, (v[t[0]].y + v[t[1]].y + v[t[2]].y) / 3};
	}

	// Every triangle edge maps to its owning triangles. Manifold = every edge is
	// owned by either one (boundary) or two (interior) triangles, never more.
	bool isEdgeManifold(const NavMesh& m) {
		std::map<std::pair<std::uint32_t, std::uint32_t>, int> count;
		for (const NavTriangle& t : m.triangles) {
			for (int e = 0; e < 3; ++e) {
				std::uint32_t a = t.v[e];
				std::uint32_t b = t.v[(e + 1) % 3];
				if (a > b) {
					std::swap(a, b);
				}
				++count[{a, b}];
			}
		}
		for (const auto& kv : count) {
			if (kv.second > 2) {
				return false;
			}
		}
		return true;
	}

	// Cross-check the stored neighbor[] against a freshly hashed edge map: an edge
	// shared by two triangles must record each as the other's neighbor; a boundary
	// edge must record -1.
	bool neighborsConsistent(const NavMesh& m) {
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::pair<std::int32_t, int>>> owners;
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
			const NavTriangle& t = m.triangles[ti];
			for (int e = 0; e < 3; ++e) {
				std::uint32_t a = t.v[e];
				std::uint32_t b = t.v[(e + 1) % 3];
				if (a > b) {
					std::swap(a, b);
				}
				owners[{a, b}].push_back({ti, e});
			}
		}
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
			const NavTriangle& t = m.triangles[ti];
			for (int e = 0; e < 3; ++e) {
				std::uint32_t a = t.v[e];
				std::uint32_t b = t.v[(e + 1) % 3];
				if (a > b) {
					std::swap(a, b);
				}
				const auto& list = owners[{a, b}];
				if (list.size() == 1) {
					if (t.neighbor[e] != -1) {
						return false;
					}
				} else if (list.size() == 2) {
					std::int32_t other = (list[0].first == ti) ? list[1].first : list[0].first;
					if (t.neighbor[e] != other) {
						return false;
					}
				}
			}
		}
		return true;
	}

	// Index of the first triangle whose centroid is Inside `region`, or -1.
	std::int32_t findTriangleInside(const NavMesh& m, const std::vector<Vec2i64>& region) {
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
			if (pointInPolygon(centroid(m.vertices, m.triangles[ti].v), region) == PointInPolygon::Inside) {
				return ti;
			}
		}
		return -1;
	}

	// Index of the first triangle whose centroid is Inside `outer` but Outside
	// `hole` (e.g. the exterior region around an enclosed room), or -1.
	std::int32_t findTriangleInsideButOutside(
		const NavMesh& m, const std::vector<Vec2i64>& outer, const std::vector<Vec2i64>& hole) {
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
			Vec2i64 c = centroid(m.vertices, m.triangles[ti].v);
			if (pointInPolygon(c, outer) == PointInPolygon::Inside &&
				pointInPolygon(c, hole) == PointInPolygon::Outside) {
				return ti;
			}
		}
		return -1;
	}

	// A triangle is walkable under a TRUTH query (knows everything): real floor, or a
	// door span (faceBlocker>0 with an opening). Solid walls (faceBlocker>0, no
	// opening) and terrain sentinels (faceBlocker<0) block. Mirrors PathQuery's truth
	// predicate; the whole region is now triangulated, so reachability is a filtered
	// graph walk, not a topology fact.
	bool truthWalkable(const NavTriangle& t) {
		if (t.faceBlocker == kNoBlocker) {
			return true;
		}
		if (t.faceBlocker < 0) {
			return false;
		}
		return t.faceOpening != kNoOpening;
	}

	// BFS over neighbor[] from `start`, crossing only truth-walkable triangles when
	// `filter` is set (the default). With filtering off it walks the raw topology.
	std::set<std::int32_t> reachable(const NavMesh& m, std::int32_t start, bool filter = true) {
		std::set<std::int32_t> seen;
		std::queue<std::int32_t> q;
		seen.insert(start);
		q.push(start);
		while (!q.empty()) {
			std::int32_t cur = q.front();
			q.pop();
			for (std::int32_t n : m.triangles[cur].neighbor) {
				if (n < 0 || seen.count(n)) {
					continue;
				}
				if (filter && !truthWalkable(m.triangles[n])) {
					continue;
				}
				if (seen.insert(n).second) {
					q.push(n);
				}
			}
		}
		return seen;
	}

	// Index of the first triangle whose centroid is Inside `region`, restricted to
	// floor triangles (faceBlocker == kNoBlocker) so callers land on walkable floor,
	// not the wall band now triangulated inside the region.
	std::int32_t findFloorTriangleInside(const NavMesh& m, const std::vector<Vec2i64>& region) {
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
			if (m.triangles[ti].faceBlocker != kNoBlocker) {
				continue;
			}
			if (pointInPolygon(centroid(m.vertices, m.triangles[ti].v), region) == PointInPolygon::Inside) {
				return ti;
			}
		}
		return -1;
	}

	NavInputPolygon border(std::vector<Vec2i64> ring) {
		return {std::move(ring), false, 1};
	}

	NavInputPolygon blocked(std::vector<Vec2i64> ring, std::int64_t id) {
		return {std::move(ring), true, id};
	}

} // namespace

TEST(NavMesh, BorderOnlyTilesAndManifold) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_TRUE(allCcw(m));
	// Area conservation: the triangles tile the whole border square.
	EXPECT_EQ(totalArea2(m), Int128(2 * 1000 * 1000));
	EXPECT_TRUE(isEdgeManifold(m));
	EXPECT_TRUE(neighborsConsistent(m));
}

TEST(NavMesh, FreestandingObstacle) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));
	in.polygons.push_back(blocked({{400, 400}, {600, 400}, {600, 600}, {400, 600}}, 7));

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_TRUE(allCcw(m));
	EXPECT_TRUE(isEdgeManifold(m));
	EXPECT_TRUE(neighborsConsistent(m));

	// The whole border is now triangulated -- the obstacle interior is KEPT (tagged),
	// not discarded -- so the triangles tile the full 1000^2 square.
	EXPECT_EQ(totalArea2(m), Int128(2 * 1000 * 1000));

	// Every triangle whose centroid is inside the obstacle carries faceBlocker=7 with
	// no opening (a solid wall); every triangle outside it is real floor (kNoBlocker).
	std::vector<Vec2i64> obstacle = {{400, 400}, {600, 400}, {600, 600}, {400, 600}};
	int inObstacle = 0;
	for (const NavTriangle& t : m.triangles) {
		if (pointInPolygon(centroid(m.vertices, t.v), obstacle) == PointInPolygon::Inside) {
			EXPECT_EQ(t.faceBlocker, 7);
			EXPECT_EQ(t.faceOpening, kNoOpening);
			++inObstacle;
		} else {
			EXPECT_EQ(t.faceBlocker, kNoBlocker);
		}
	}
	EXPECT_GT(inObstacle, 0) << "the obstacle interior must now be triangulated and tagged";

	// The obstacle's four edges still carry the obstacle's provenance, but they are
	// now INTERIOR edges (floor and wall faces share them), which is what lets a
	// belief query traverse into the wall.
	auto edgeKey = [](Vec2i64 a, Vec2i64 b) {
		if (b < a) {
			std::swap(a, b);
		}
		return std::make_pair(a, b);
	};
	std::set<std::pair<Vec2i64, Vec2i64>> obstacleEdges = {
		edgeKey({400, 400}, {600, 400}), edgeKey({600, 400}, {600, 600}),
		edgeKey({600, 600}, {400, 600}), edgeKey({400, 600}, {400, 400})};
	int matched = 0;
	for (const NavTriangle& t : m.triangles) {
		for (int e = 0; e < 3; ++e) {
			auto k = edgeKey(m.vertices[t.v[e]], m.vertices[t.v[(e + 1) % 3]]);
			if (obstacleEdges.count(k)) {
				EXPECT_NE(t.neighbor[e], -1) << "obstacle edge now links floor to wall";
				EXPECT_EQ(t.edgeProvenance[e], 7) << "obstacle edge carries provenance";
				++matched;
			}
		}
	}
	EXPECT_EQ(matched, 8) << "each of the four obstacle edges is shared by two triangles";

	// A TRUTH query still cannot enter the obstacle: a truth-filtered BFS from a floor
	// triangle reaches no obstacle triangle (the wall blocks under truth).
	std::int32_t floorTri = findFloorTriangleInside(m, {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}});
	ASSERT_GE(floorTri, 0);
	std::set<std::int32_t> reach = reachable(m, floorTri);
	for (std::int32_t ti : reach) {
		EXPECT_NE(pointInPolygon(centroid(m.vertices, m.triangles[ti].v), obstacle), PointInPolygon::Inside)
			<< "truth-filtered reachability must not enter the solid obstacle";
	}
}

TEST(NavMesh, ClosedRoomTwoComponents) {
	// border + a closed room of four blocked wall rectangles (gap-less band). The wall
	// interiors are now triangulated and tagged, so the mesh is one connected graph;
	// but under TRUTH the solid walls block, so a truth-filtered BFS from a floor
	// triangle inside the room never reaches a floor triangle outside it.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	in.polygons.push_back(blocked({{1000, 1000}, {2000, 1000}, {2000, 1100}, {1000, 1100}}, 20)); // bottom
	in.polygons.push_back(blocked({{1000, 1900}, {2000, 1900}, {2000, 2000}, {1000, 2000}}, 21)); // top
	in.polygons.push_back(blocked({{1000, 1100}, {1100, 1100}, {1100, 1900}, {1000, 1900}}, 22)); // left
	in.polygons.push_back(blocked({{1900, 1100}, {2000, 1100}, {2000, 1900}, {1900, 1900}}, 23)); // right

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_TRUE(allCcw(m));
	EXPECT_TRUE(isEdgeManifold(m));
	EXPECT_TRUE(neighborsConsistent(m));

	std::vector<Vec2i64> room		= {{1100, 1100}, {1900, 1100}, {1900, 1900}, {1100, 1900}};
	std::vector<Vec2i64> borderRing = {{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}};
	// "outside" = a floor triangle inside the border but outside the wall band.
	std::vector<Vec2i64> wallBlock = {{1000, 1000}, {2000, 1000}, {2000, 2000}, {1000, 2000}};

	std::int32_t inside	 = findFloorTriangleInside(m, room);
	std::int32_t outside = -1;
	for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
		Vec2i64 c = centroid(m.vertices, m.triangles[ti].v);
		if (m.triangles[ti].faceBlocker == kNoBlocker && pointInPolygon(c, borderRing) == PointInPolygon::Inside &&
			pointInPolygon(c, wallBlock) == PointInPolygon::Outside) {
			outside = ti;
			break;
		}
	}
	ASSERT_GE(inside, 0) << "expected at least one floor triangle inside the room";
	ASSERT_GE(outside, 0) << "expected at least one floor triangle outside the room";

	std::set<std::int32_t> fromInside = reachable(m, inside); // truth-filtered
	EXPECT_EQ(fromInside.count(outside), 0u) << "closed room must be unreachable through solid walls under truth";
}

TEST(NavMesh, DoorGapConnectsAndTags) {
	// Same closed room, but the bottom wall has a doorway gap from x=1400..1600.
	// A DoorPortal spans the gap's two inner corners; inside and outside become a
	// single connected walkable face, and the portal edge carries the openingId.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	// Bottom wall split into two pieces leaving a gap [1400,1600] at the inner face y=1100.
	in.polygons.push_back(blocked({{1000, 1000}, {1400, 1000}, {1400, 1100}, {1000, 1100}}, 20)); // bottom-left
	in.polygons.push_back(blocked({{1600, 1000}, {2000, 1000}, {2000, 1100}, {1600, 1100}}, 24)); // bottom-right
	in.polygons.push_back(blocked({{1000, 1900}, {2000, 1900}, {2000, 2000}, {1000, 2000}}, 21)); // top
	in.polygons.push_back(blocked({{1000, 1100}, {1100, 1100}, {1100, 1900}, {1000, 1900}}, 22)); // left
	in.polygons.push_back(blocked({{1900, 1100}, {2000, 1100}, {2000, 1900}, {1900, 1900}}, 23)); // right

	// Doorway gap inner corners: (1400,1100) and (1600,1100).
	DoorPortal door;
	door.openingId	  = 99;
	door.a			  = {1400, 1100};
	door.b			  = {1600, 1100};
	door.clearWidthMm = 200;
	in.doors.push_back(door);

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_TRUE(allCcw(m));
	EXPECT_TRUE(isEdgeManifold(m));
	EXPECT_TRUE(neighborsConsistent(m));

	std::vector<Vec2i64> room		= {{1100, 1100}, {1900, 1100}, {1900, 1900}, {1100, 1900}};
	std::vector<Vec2i64> borderRing = {{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}};
	std::vector<Vec2i64> wallBlock	= {{1000, 1000}, {2000, 1000}, {2000, 2000}, {1000, 2000}};
	std::int32_t		 inside		= findTriangleInside(m, room);
	std::int32_t		 outside	= findTriangleInsideButOutside(m, borderRing, wallBlock);
	ASSERT_GE(inside, 0);
	ASSERT_GE(outside, 0);

	std::set<std::int32_t> fromInside = reachable(m, inside);
	EXPECT_GT(fromInside.count(outside), 0u) << "doorway must connect inside to outside";

	// The portal edge (1400,1100)-(1600,1100) carries openingId 99.
	bool taggedPortal = false;
	for (const NavTriangle& t : m.triangles) {
		for (int e = 0; e < 3; ++e) {
			Vec2i64 a = m.vertices[t.v[e]];
			Vec2i64 b = m.vertices[t.v[(e + 1) % 3]];
			if ((a == Vec2i64{1400, 1100} && b == Vec2i64{1600, 1100}) ||
				(a == Vec2i64{1600, 1100} && b == Vec2i64{1400, 1100})) {
				EXPECT_EQ(t.edgeOpening[e], 99) << "portal edge must carry the openingId";
				taggedPortal = true;
			}
		}
	}
	EXPECT_TRUE(taggedPortal) << "expected a triangle edge spanning the doorway gap";
}

TEST(NavMesh, FullWidthBlockerSeparatesSides) {
	// A blocked polygon spanning the full width of the border. Under TRUTH it blocks,
	// so a truth-filtered BFS never links the top and bottom floor regions.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	in.polygons.push_back(blocked({{0, 900}, {2000, 900}, {2000, 1100}, {0, 1100}}, 50));

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_TRUE(allCcw(m));
	EXPECT_TRUE(neighborsConsistent(m));

	std::vector<Vec2i64> bottom = {{0, 0}, {2000, 0}, {2000, 400}, {0, 400}};
	std::vector<Vec2i64> top	= {{0, 1600}, {2000, 1600}, {2000, 2000}, {0, 2000}};
	std::int32_t		 b		= findFloorTriangleInside(m, bottom);
	std::int32_t		 t		= findFloorTriangleInside(m, top);
	ASSERT_GE(b, 0);
	ASSERT_GE(t, 0);

	std::set<std::int32_t> fromBottom = reachable(m, b); // truth-filtered
	EXPECT_EQ(fromBottom.count(t), 0u) << "full-width blocker must disconnect the two sides under truth";

	// The blocker band is now triangulated and tagged with its provenance (a solid
	// wall: no opening), rather than left as a hole.
	std::vector<Vec2i64> blocker = {{0, 900}, {2000, 900}, {2000, 1100}, {0, 1100}};
	int inBlocker = 0;
	for (const NavTriangle& tri : m.triangles) {
		if (pointInPolygon(centroid(m.vertices, tri.v), blocker) == PointInPolygon::Inside) {
			EXPECT_EQ(tri.faceBlocker, 50);
			EXPECT_EQ(tri.faceOpening, kNoOpening);
			++inBlocker;
		}
	}
	EXPECT_GT(inBlocker, 0) << "the blocker band must now be triangulated and tagged";
}

TEST(NavMesh, OutsideBorderDiscarded) {
	// A blocked polygon straddling the border, plus geometry entirely beyond it.
	// No triangle centroid lands outside the border.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));
	in.polygons.push_back(blocked({{800, 800}, {1200, 800}, {1200, 1200}, {800, 1200}}, 30)); // straddles corner
	in.polygons.push_back(blocked({{2000, 2000}, {2500, 2000}, {2500, 2500}, {2000, 2500}}, 31)); // fully outside

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_TRUE(allCcw(m));

	std::vector<Vec2i64> borderRing = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	for (const NavTriangle& t : m.triangles) {
		EXPECT_NE(pointInPolygon(centroid(m.vertices, t.v), borderRing), PointInPolygon::Outside)
			<< "no triangle may lie outside the walkable bounds";
	}
}

TEST(NavMesh, MultipleDisjointBorders) {
	// Two disjoint unblocked border squares. The first is empty; the second has a
	// freestanding obstacle. Walkable triangles must appear in BOTH regions, none
	// may lie outside either border, and the obstacle interior stays empty.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));
	in.polygons.push_back(border({{2000, 0}, {3000, 0}, {3000, 1000}, {2000, 1000}}));
	in.polygons.push_back(blocked({{2400, 400}, {2600, 400}, {2600, 600}, {2400, 600}}, 9));

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_TRUE(allCcw(m));
	EXPECT_TRUE(isEdgeManifold(m));
	EXPECT_TRUE(neighborsConsistent(m));

	// Total covered area: both 1000^2 squares in full -- the obstacle interior is now
	// triangulated and tagged, not subtracted.
	EXPECT_EQ(totalArea2(m), Int128(2 * (1000 * 1000 + 1000 * 1000)));

	std::vector<Vec2i64> regionA = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	std::vector<Vec2i64> regionB = {{2000, 0}, {3000, 0}, {3000, 1000}, {2000, 1000}};
	EXPECT_GE(findTriangleInside(m, regionA), 0) << "expected triangles in the first border";
	EXPECT_GE(findTriangleInside(m, regionB), 0) << "expected triangles in the second border";

	// No triangle centroid lands outside BOTH borders (the dead band between them).
	// Triangles inside the obstacle carry its provenance (solid wall, no opening);
	// everything else is real floor.
	std::vector<Vec2i64> obstacle = {{2400, 400}, {2600, 400}, {2600, 600}, {2400, 600}};
	int inObstacle = 0;
	for (const NavTriangle& t : m.triangles) {
		Vec2i64 c		= centroid(m.vertices, t.v);
		bool	inAny	= pointInPolygon(c, regionA) == PointInPolygon::Inside ||
						pointInPolygon(c, regionB) == PointInPolygon::Inside;
		EXPECT_TRUE(inAny) << "triangle must lie inside one of the two borders";
		if (pointInPolygon(c, obstacle) == PointInPolygon::Inside) {
			EXPECT_EQ(t.faceBlocker, 9);
			EXPECT_EQ(t.faceOpening, kNoOpening);
			++inObstacle;
		} else {
			EXPECT_EQ(t.faceBlocker, kNoBlocker);
		}
	}
	EXPECT_GT(inObstacle, 0) << "the obstacle interior must now be triangulated and tagged";
}

TEST(NavMesh, Deterministic) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	in.polygons.push_back(blocked({{400, 400}, {700, 400}, {700, 700}, {400, 700}}, 5));
	in.polygons.push_back(blocked({{1200, 1200}, {1600, 1200}, {1600, 1600}, {1200, 1600}}, 6));

	NavMesh a = buildNavMesh(in);
	NavMesh b = buildNavMesh(in);
	ASSERT_EQ(a.vertices.size(), b.vertices.size());
	EXPECT_EQ(a.vertices, b.vertices);
	ASSERT_EQ(a.triangles.size(), b.triangles.size());
	for (std::size_t i = 0; i < a.triangles.size(); ++i) {
		EXPECT_EQ(a.triangles[i].v, b.triangles[i].v);
		EXPECT_EQ(a.triangles[i].neighbor, b.triangles[i].neighbor);
		EXPECT_EQ(a.triangles[i].edgeProvenance, b.triangles[i].edgeProvenance);
		EXPECT_EQ(a.triangles[i].edgeOpening, b.triangles[i].edgeOpening);
	}
}

// ---------------------------------------------------------------------------
// Corridor-width computation (Demyen-Buro, sec. 4.1) against common-knowledge
// obstacles. Widths are floored mm disc DIAMETERS; kUnconstrainedWidth means no
// such obstacle pinches the passage.
// ---------------------------------------------------------------------------

namespace {

	bool isCkBlocked(std::int64_t b) {
		return b < 0 && b != kNoBlocker;
	}

	// Is edge e of triangle ti an obstacle edge (one incident face common-knowledge
	// blocked, the other not)? Mirrors NavMesh.cpp's isObstacleEdge.
	bool obstacleEdge(const NavMesh& m, std::int32_t ti, int e) {
		bool		 self  = isCkBlocked(m.triangles[ti].faceBlocker);
		std::int32_t nb	   = m.triangles[ti].neighbor[e];
		bool		 other = nb < 0 ? false : isCkBlocked(m.triangles[nb].faceBlocker);
		return self != other;
	}

	// Smallest finite edge-pair width over genuinely TRAVERSABLE squeezes: a floor
	// triangle apex whose two incident edges are both non-obstacle (the agent can
	// enter one and leave the other). This excludes edge pairs that abut an obstacle
	// face -- those widths are computed but never gate a real crossing (the A* never
	// exits into a blocked face), so they would otherwise pollute the minimum.
	std::int64_t minTraversableWidth(const NavMesh& m) {
		std::int64_t best = kUnconstrainedWidth;
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
			const NavTriangle& t = m.triangles[ti];
			if (t.faceBlocker != kNoBlocker) {
				continue; // floor only
			}
			for (int apex = 0; apex < 3; ++apex) {
				// Edges incident to vertex v[apex] are edge `apex` and edge `(apex+2)%3`.
				if (obstacleEdge(m, ti, apex) || obstacleEdge(m, ti, (apex + 2) % 3)) {
					continue; // squeeze abuts an obstacle face: not a real crossing
				}
				if (t.edgePairWidthMm[apex] < best) {
					best = t.edgePairWidthMm[apex];
				}
			}
		}
		return best;
	}

	// A CK terrain ring (water/flora): negative provenance, always blocks.
	NavInputPolygon water(std::vector<Vec2i64> ring, std::int64_t id) {
		return {std::move(ring), true, id};
	}

} // namespace

TEST(NavMesh, CorridorWidthMatchesGapDistance) {
	// A full-width water band with a single 400 mm gap (x in [1800,2200]). The
	// tightest floor corridor width must be exactly that gap distance, measured as
	// the distance between the two gap-facing obstacle edges, not an edge length.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 2000}, {0, 2000}}));
	in.polygons.push_back(water({{0, 900}, {1800, 900}, {1800, 1100}, {0, 1100}}, -10));
	in.polygons.push_back(water({{2200, 900}, {4000, 900}, {4000, 1100}, {2200, 1100}}, -11));

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_EQ(minTraversableWidth(m), 400) << "tightest traversable corridor width must equal the 400 mm gap";

	// A wide-open floor triangle far from both obstacles stays unconstrained.
	bool sawUnconstrained = false;
	for (const NavTriangle& t : m.triangles) {
		if (t.faceBlocker != kNoBlocker) {
			continue;
		}
		Vec2i64 c = centroid(m.vertices, t.v);
		if (c.y < 400) { // well below the band
			for (int a = 0; a < 3; ++a) {
				if (t.edgePairWidthMm[a] == kUnconstrainedWidth) {
					sawUnconstrained = true;
				}
			}
		}
	}
	EXPECT_TRUE(sawUnconstrained) << "open floor away from obstacles must be unconstrained";
}

TEST(NavMesh, CorridorWidthPerpendicularChannel) {
	// Two parallel water bands leaving a 400 mm channel between y=800 and y=1200.
	// A floor triangle spanning the channel has long (~2000 mm) edges but its width
	// is the 400 mm perpendicular gap (Demyen Case 2), proving width != edge length.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 2000}, {0, 2000}}));
	in.polygons.push_back(water({{1000, 0}, {3000, 0}, {3000, 800}, {1000, 800}}, -10));
	in.polygons.push_back(water({{1000, 1200}, {3000, 1200}, {3000, 2000}, {1000, 2000}}, -11));

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_EQ(minTraversableWidth(m), 400) << "channel width is the 400 mm perpendicular gap";
}

TEST(NavMesh, CorridorWidthObtuseSqueeze) {
	// An obstacle nub rises from the bottom band (top at y=1100) toward a top band at
	// y=1500. Two constrictions: (a) the 400 mm vertical gap directly above the nub
	// (Case 2, perpendicular to the top band), and (b) an obtuse intra-triangle
	// squeeze from the top band corner past the nub's top corner. Both are distances
	// to obstacle features, never edge lengths.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 2000}, {0, 2000}}));
	in.polygons.push_back(water({{0, 0}, {4000, 0}, {4000, 600}, {0, 600}}, -10));
	in.polygons.push_back(water({{0, 1500}, {4000, 1500}, {4000, 2000}, {0, 2000}}, -11));
	in.polygons.push_back(water({{1900, 600}, {2100, 600}, {2100, 1100}, {1900, 1100}}, -12)); // nub

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	// The floor triangle [(4000,1500),(0,1500),(1900,1100)] spans from the top band to
	// the nub's top-left corner; its apex at (1900,1100) measures the 400 mm gap to the
	// top band (Case 2). Find it and check that apex width is 400.
	bool found400 = false;
	// And the obtuse case: triangle [(4000,1500),(1900,1100),(2100,1100)] has an apex at
	// (4000,1500) whose nearest in-wedge obstacle is the nub corner (2100,1100): an
	// obtuse squeeze = floor(sqrt(1900^2 + 400^2)) = 1941 mm (Case 1).
	bool found1941 = false;
	for (const NavTriangle& t : m.triangles) {
		if (t.faceBlocker != kNoBlocker) {
			continue;
		}
		for (int a = 0; a < 3; ++a) {
			Vec2i64 apex = m.vertices[t.v[a]];
			if (apex == Vec2i64{1900, 1100} && t.edgePairWidthMm[a] == 400) {
				found400 = true;
			}
			if (apex == Vec2i64{4000, 1500} && t.edgePairWidthMm[a] == 1941) {
				found1941 = true;
			}
		}
	}
	EXPECT_TRUE(found400) << "vertical gap above the nub must be the 400 mm perpendicular width";
	EXPECT_TRUE(found1941) << "obtuse squeeze to the nub corner must be floor(sqrt(1900^2+400^2))=1941";
}

TEST(NavMesh, SliverInOpenFloorIsUnconstrained) {
	// A long thin open strip: the CDT emits high-aspect sliver triangles with no
	// incident obstacle edge. Their tiny altitude must NOT register as a narrow
	// corridor; every edge-pair width must be unconstrained.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {6000, 0}, {6000, 40}, {0, 40}}));

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	for (const NavTriangle& t : m.triangles) {
		for (int a = 0; a < 3; ++a) {
			EXPECT_EQ(t.edgePairWidthMm[a], kUnconstrainedWidth)
				<< "an open-floor sliver has no obstacle, so it must be unconstrained";
		}
	}
}

TEST(NavMesh, DoorClearWidthThreadedOntoPortalEdge) {
	// A door span in a wall: DoorPortal::clearWidthMm must land on the portal edge's
	// edgeClearWidthMm for both triangles sharing it (the A* gates a door by it).
	NavMeshInput in;
	in.polygons.push_back(border({{-2000, -2000}, {4000, -2000}, {4000, 4000}, {-2000, 4000}}));
	in.polygons.push_back(blocked({{0, 0}, {800, 0}, {800, 200}, {0, 200}}, 50));
	in.polygons.push_back(NavInputPolygon{{{800, 0}, {1200, 0}, {1200, 200}, {800, 200}}, true, 50, 9}); // door span
	in.polygons.push_back(blocked({{1200, 0}, {2000, 0}, {2000, 200}, {1200, 200}}, 50));
	DoorPortal d;
	d.openingId	   = 9;
	d.a		   = {800, 0};
	d.b		   = {1200, 0};
	d.clearWidthMm = 400;
	in.doors.push_back(d);

	NavMesh m = buildNavMesh(in);
	int tagged = 0;
	for (const NavTriangle& t : m.triangles) {
		for (int e = 0; e < 3; ++e) {
			Vec2i64 a = m.vertices[t.v[e]];
			Vec2i64 b = m.vertices[t.v[(e + 1) % 3]];
			if ((a == Vec2i64{800, 0} && b == Vec2i64{1200, 0}) || (a == Vec2i64{1200, 0} && b == Vec2i64{800, 0})) {
				EXPECT_EQ(t.edgeOpening[e], 9);
				EXPECT_EQ(t.edgeClearWidthMm[e], 400) << "door clear width must thread onto the portal edge";
				++tagged;
			}
		}
	}
	EXPECT_EQ(tagged, 2) << "both triangles sharing the door edge carry the clear width";
}

// ---------------------------------------------------------------------------
// Localizing test for the "zero walkable faces" navmesh bug: a clearly walkable
// input (one large border ring + one small interior water ring) must yield at
// least one walkable floor face. Models the real buildInput shape, where the
// border carries kProvenanceBorder (-3) and water carries kProvenanceWater (-1)
// -- both NEGATIVE provenance. If buildNavMesh tags the in-bounds floor faces as
// common-knowledge blockers (faceBlocker < 0 && != kNoBlocker), the mesh has no
// walkable face and pathing dies. The provenance values here match
// engine::nav::kProvenanceBorder / kProvenanceWater exactly.
TEST(NavMesh, WalkableBorderWithInteriorWaterHasFloor) {
	constexpr std::int64_t kProvenanceWater	 = -1;
	constexpr std::int64_t kProvenanceBorder = -3;

	NavMeshInput in;
	// Large walkable border ring (blocked=false), provenance = border sentinel.
	in.polygons.push_back(
		NavInputPolygon{{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}}, false, kProvenanceBorder});
	// Small blocked water ring well inside it, provenance = water sentinel.
	in.polygons.push_back(
		NavInputPolygon{{{4000, 4000}, {6000, 4000}, {6000, 6000}, {4000, 6000}}, true, kProvenanceWater});

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty()) << "buildNavMesh produced no triangles for a walkable square";

	// Count floor (walkable) faces vs. common-knowledge terrain blockers.
	int floorFaces	 = 0;
	int terrainFaces = 0;
	for (const NavTriangle& t : m.triangles) {
		if (isFloorFace(t)) {
			++floorFaces;
		}
		if (isCommonKnowledgeTerrainFace(t)) {
			++terrainFaces;
		}
	}

	// The water interior is correctly a terrain blocker.
	EXPECT_GT(terrainFaces, 0) << "the interior water ring must be tagged as a terrain blocker";
	// The bug: every face is a terrain blocker, leaving ZERO walkable floor. A
	// clearly walkable square MUST yield at least one floor face.
	EXPECT_GT(floorFaces, 0)
		<< "a walkable border with an interior obstacle must leave walkable floor faces; "
		<< "got " << floorFaces << " floor / " << terrainFaces << " terrain out of " << m.triangles.size();

	// And the floor must be terrain-traversable (the predicate the runtime scan used).
	int traversable = 0;
	for (const NavTriangle& t : m.triangles) {
		if (terrainTraversable(t)) {
			++traversable;
		}
	}
	EXPECT_GT(traversable, 0) << "at least one triangle must be terrain-traversable (walkable)";
}

// The real "land island surrounded by water" topology that buildInput hands to
// buildNavMesh. extractWaterObstacles emits a water body as SEPARATE rings: a CCW
// OUTER water boundary plus a CW land-island HOLE, both pushed as independent
// BLOCKED, holeCapable water rings (it never pairs outer+hole into one polygon-
// with-hole). With even-odd containment parity, a point inside an even number of
// nested water rings (outer + island = 2) is land = floor; the island is walkable.
TEST(NavMesh, LandIslandInsideWater_HasFloor) {
	constexpr std::int64_t kProvenanceWater = -1;
	constexpr std::int64_t kProvenanceBorder = -3;

	NavMeshInput in;
	// Walkable border covering the whole area.
	in.polygons.push_back(
		NavInputPolygon{{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}}, false, kProvenanceBorder});
	// Water OUTER boundary (the big ring) -- holeCapable water.
	in.polygons.push_back(
		NavInputPolygon{{{1000, 1000}, {9000, 1000}, {9000, 9000}, {1000, 9000}}, true, kProvenanceWater, kNoOpening, true});
	// Land-island HOLE -- its OWN separate holeCapable water ring (the land the
	// colonist stands on).
	in.polygons.push_back(
		NavInputPolygon{{{4000, 4000}, {6000, 4000}, {6000, 6000}, {4000, 6000}}, true, kProvenanceWater, kNoOpening, true});

	NavMesh m = buildNavMesh(in);

	int floorFaces = 0;
	for (const NavTriangle& t : m.triangles) {
		if (isFloorFace(t)) {
			++floorFaces;
		}
	}
	// The land island between the two water rings is walkable floor (even depth = 2).
	EXPECT_GT(floorFaces, 0)
		<< "land island surrounded by water must be walkable floor under even-odd parity "
		<< "(floor=" << floorFaces << " of " << m.triangles.size() << " triangles)";

	// The island interior specifically must contain walkable floor.
	std::vector<Vec2i64> island = {{4000, 4000}, {6000, 4000}, {6000, 6000}, {4000, 6000}};
	EXPECT_GE(findFloorTriangleInside(m, island), 0) << "the island interior must be walkable floor";

	// Open water between the two rings (odd depth = 1) must NOT be floor.
	std::vector<Vec2i64> openWater = {{1000, 1000}, {9000, 1000}, {9000, 9000}, {1000, 9000}};
	for (const NavTriangle& t : m.triangles) {
		Vec2i64 c = centroid(m.vertices, t.v);
		if (pointInPolygon(c, openWater) == PointInPolygon::Inside &&
			pointInPolygon(c, island) == PointInPolygon::Outside) {
			EXPECT_FALSE(isFloorFace(t)) << "open water (odd containment depth) must not be walkable floor";
		}
	}
}

// NESTED: water outer (holeCapable) > land island > pond, all holeCapable water
// rings. Even-odd parity: a point on the island sits in {outer, island} = depth 2
// = land; a point in the pond sits in {outer, island, pond} = depth 3 = water. The
// island ring must be walkable, the pond interior must not.
TEST(NavMesh, NestedWaterIslandPondParity) {
	constexpr std::int64_t kProvenanceWater = -1;
	constexpr std::int64_t kProvenanceBorder = -3;

	NavMeshInput in;
	in.polygons.push_back(
		NavInputPolygon{{{0, 0}, {12000, 0}, {12000, 12000}, {0, 12000}}, false, kProvenanceBorder});
	// Outer water body.
	in.polygons.push_back(
		NavInputPolygon{{{1000, 1000}, {11000, 1000}, {11000, 11000}, {1000, 11000}}, true, kProvenanceWater, kNoOpening, true});
	// Land island inside the water.
	in.polygons.push_back(
		NavInputPolygon{{{3000, 3000}, {9000, 3000}, {9000, 9000}, {3000, 9000}}, true, kProvenanceWater, kNoOpening, true});
	// Pond on the island.
	in.polygons.push_back(
		NavInputPolygon{{{5000, 5000}, {7000, 5000}, {7000, 7000}, {5000, 7000}}, true, kProvenanceWater, kNoOpening, true});

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	// Island band (inside island ring, outside pond) = depth 2 = walkable floor.
	std::vector<Vec2i64> island = {{3000, 3000}, {9000, 3000}, {9000, 9000}, {3000, 9000}};
	std::vector<Vec2i64> pond	= {{5000, 5000}, {7000, 5000}, {7000, 7000}, {5000, 7000}};
	bool sawIslandFloor = false;
	for (const NavTriangle& t : m.triangles) {
		Vec2i64 c = centroid(m.vertices, t.v);
		if (pointInPolygon(c, island) == PointInPolygon::Inside &&
			pointInPolygon(c, pond) == PointInPolygon::Outside) {
			if (isFloorFace(t)) {
				sawIslandFloor = true;
			}
		}
		if (pointInPolygon(c, pond) == PointInPolygon::Inside) {
			EXPECT_FALSE(isFloorFace(t)) << "pond (depth 3, odd) must be water, not floor";
		}
	}
	EXPECT_TRUE(sawIslandFloor) << "the island band (depth 2, even) must be walkable floor";
}

// DISJOINT: two separate water bodies, each holeCapable. Disjoint bodies never
// contain each other, so a point counts only the rings of the one body it sits in.
// Land between and outside both bodies (depth 0) and land on a hole island in each
// body (depth 2) must be walkable; open water in each body (depth 1) must not.
TEST(NavMesh, DisjointWaterBodiesParity) {
	constexpr std::int64_t kProvenanceWater = -1;
	constexpr std::int64_t kProvenanceBorder = -3;

	NavMeshInput in;
	in.polygons.push_back(
		NavInputPolygon{{{0, 0}, {12000, 0}, {12000, 6000}, {0, 6000}}, false, kProvenanceBorder});
	// Body A: outer + island hole.
	in.polygons.push_back(
		NavInputPolygon{{{1000, 1000}, {4000, 1000}, {4000, 5000}, {1000, 5000}}, true, kProvenanceWater, kNoOpening, true});
	in.polygons.push_back(
		NavInputPolygon{{{2000, 2000}, {3000, 2000}, {3000, 4000}, {2000, 4000}}, true, kProvenanceWater, kNoOpening, true});
	// Body B: outer + island hole, disjoint from A.
	in.polygons.push_back(
		NavInputPolygon{{{8000, 1000}, {11000, 1000}, {11000, 5000}, {8000, 5000}}, true, kProvenanceWater, kNoOpening, true});
	in.polygons.push_back(
		NavInputPolygon{{{9000, 2000}, {10000, 2000}, {10000, 4000}, {9000, 4000}}, true, kProvenanceWater, kNoOpening, true});

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	// Land between the two bodies (depth 0) is walkable.
	std::vector<Vec2i64> between = {{5000, 1000}, {7000, 1000}, {7000, 5000}, {5000, 5000}};
	EXPECT_GE(findFloorTriangleInside(m, between), 0) << "land between disjoint water bodies must be walkable";

	// Island in each body (depth 2) is walkable.
	std::vector<Vec2i64> islandA = {{2000, 2000}, {3000, 2000}, {3000, 4000}, {2000, 4000}};
	std::vector<Vec2i64> islandB = {{9000, 2000}, {10000, 2000}, {10000, 4000}, {9000, 4000}};
	EXPECT_GE(findFloorTriangleInside(m, islandA), 0) << "island in body A must be walkable";
	EXPECT_GE(findFloorTriangleInside(m, islandB), 0) << "island in body B must be walkable";

	// Open water in each body (depth 1, between outer and island) must not be floor.
	std::vector<Vec2i64> outerA = {{1000, 1000}, {4000, 1000}, {4000, 5000}, {1000, 5000}};
	std::vector<Vec2i64> outerB = {{8000, 1000}, {11000, 1000}, {11000, 5000}, {8000, 5000}};
	for (const NavTriangle& t : m.triangles) {
		Vec2i64 c = centroid(m.vertices, t.v);
		bool inWaterA = pointInPolygon(c, outerA) == PointInPolygon::Inside &&
						pointInPolygon(c, islandA) == PointInPolygon::Outside;
		bool inWaterB = pointInPolygon(c, outerB) == PointInPolygon::Inside &&
						pointInPolygon(c, islandB) == PointInPolygon::Outside;
		if (inWaterA || inWaterB) {
			EXPECT_FALSE(isFloorFace(t)) << "open water (depth 1) in a disjoint body must not be floor";
		}
	}
}

// SOLID PRIORITY: a solid flora ring overlapping a water outer ring stays blocked.
// A point inside both the (even-depth-walkable) water region and a solid obstacle
// must be tagged by the solid ring, never left as floor.
TEST(NavMesh, SolidObstacleOverWaterStaysBlocked) {
	constexpr std::int64_t kProvenanceWater = -1;
	constexpr std::int64_t kProvenanceBorder = -3;
	constexpr std::int64_t kProvenanceTree	= -2;

	NavMeshInput in;
	in.polygons.push_back(
		NavInputPolygon{{{0, 0}, {10000, 0}, {10000, 10000}, {0, 10000}}, false, kProvenanceBorder});
	// Water outer + land island (island interior is even depth = walkable floor).
	in.polygons.push_back(
		NavInputPolygon{{{1000, 1000}, {9000, 1000}, {9000, 9000}, {1000, 9000}}, true, kProvenanceWater, kNoOpening, true});
	in.polygons.push_back(
		NavInputPolygon{{{3000, 3000}, {7000, 3000}, {7000, 7000}, {3000, 7000}}, true, kProvenanceWater, kNoOpening, true});
	// Solid flora (NOT holeCapable) on the island.
	in.polygons.push_back(
		NavInputPolygon{{{4500, 4500}, {5500, 4500}, {5500, 5500}, {4500, 5500}}, true, kProvenanceTree});

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	std::vector<Vec2i64> tree = {{4500, 4500}, {5500, 4500}, {5500, 5500}, {4500, 5500}};
	int inTree = 0;
	for (const NavTriangle& t : m.triangles) {
		if (pointInPolygon(centroid(m.vertices, t.v), tree) == PointInPolygon::Inside) {
			EXPECT_EQ(t.faceBlocker, kProvenanceTree) << "a solid obstacle over walkable water-island land must stay blocked";
			++inTree;
		}
	}
	EXPECT_GT(inTree, 0) << "the solid obstacle interior must be triangulated and tagged";
}

// RIVER EXITS THE AREA: the topology buildInput hands to buildNavMesh when water
// touches the marching grid on every side (a river/confluence flowing OUT of the
// simulation area). extractWaterObstacles then emits a water OUTER ring that
// SURROUNDS the whole border (its boundary is the grid perimeter, beyond the area),
// plus the dry land the colonist stands on as a CW hole ring strictly inside the
// border. Both are holeCapable water rings.
//
// The surrounding water face is annular (border minus the land hole): genuine water
// at even-odd depth 1. Its outer cycle is the border square, whose extraction
// representative point lands at the square's centre -- INSIDE the land hole, where
// the depth is 2 (outer water + land = even = land). Classifying the whole water
// face from that hole-bound point mistags the water as floor (and, in the in-game
// case, can leave zero genuine floor). The fix classifies each face from a point in
// its true interior (inside the outer cycle, outside its holes). Regression guard:
// the dry land must be floor and the surrounding water must block.
TEST(NavMesh, RiverExitsArea_OuterWaterSurroundsBorder_LandHasFloor) {
	constexpr std::int64_t kProvenanceWater	 = -1;
	constexpr std::int64_t kProvenanceBorder = -3;

	NavMeshInput in;
	// Border = the simulation area.
	in.polygons.push_back(
		NavInputPolygon{{{0, 0}, {9000, 0}, {9000, 9000}, {0, 9000}}, false, kProvenanceBorder});
	// Outer water ring SURROUNDING the border (the river exits the area on every side,
	// so the marching boundary closes against land outside the grid: a perimeter ring
	// larger than the border). CCW, holeCapable.
	in.polygons.push_back(NavInputPolygon{
		{{-1000, -1000}, {10000, -1000}, {10000, 10000}, {-1000, 10000}}, true, kProvenanceWater, kNoOpening, true});
	// Dry land as a CW hole ring strictly inside the border -- the land beside the river.
	in.polygons.push_back(
		NavInputPolygon{{{2000, 2000}, {2000, 7000}, {7000, 7000}, {7000, 2000}}, true, kProvenanceWater, kNoOpening, true});

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	int floorFaces = 0;
	for (const NavTriangle& t : m.triangles) {
		if (isFloorFace(t)) {
			++floorFaces;
		}
	}
	EXPECT_GT(floorFaces, 0) << "land beside a river that exits the area must be walkable floor; got 0 "
							 << "(the zero-walkable navmesh symptom)";

	// The dry land block (inside the inner ring) must be walkable floor.
	std::vector<Vec2i64> land = {{2000, 2000}, {7000, 2000}, {7000, 7000}, {2000, 7000}};
	EXPECT_GE(findFloorTriangleInside(m, land), 0) << "the dry land interior must be walkable floor";

	// The surrounding water frame (inside the border, outside the land block) must block.
	std::vector<Vec2i64> border = {{0, 0}, {9000, 0}, {9000, 9000}, {0, 9000}};
	bool sawFrameWater = false;
	for (const NavTriangle& t : m.triangles) {
		Vec2i64 c = centroid(m.vertices, t.v);
		if (pointInPolygon(c, border) == PointInPolygon::Inside && pointInPolygon(c, land) == PointInPolygon::Outside) {
			EXPECT_FALSE(isFloorFace(t)) << "the river water surrounding the land must block, not be floor";
			if (isCommonKnowledgeTerrainFace(t)) {
				sawFrameWater = true;
			}
		}
	}
	EXPECT_TRUE(sawFrameWater) << "the surrounding river water must be tagged a terrain blocker";
}

// THE REAL IN-GAME GEOMETRY. The quickstart area is a Y-shaped river confluence:
// three branches meet in the middle and each exits the 128 m area at a different
// edge. extractWaterObstacles emits the whole water region as ONE CCW holeCapable
// ring (a thin, highly non-convex loop occupying ~6% of the area); the open grass
// the colonist stands on, including the spawn at (1000, 4000), is the ~94% OUTSIDE
// that ring. The border is the area rectangle. Captured from the in-game dump (see
// NavMeshRealRings.test.h); trees are omitted as they do not bear on whether the
// open grass between them is floor.
//
// This reproduces the walkable=0 symptom: the land (depth 0, outside the single
// water ring) must be floor, and only the thin water loop interior (depth 1) must
// block. The point-in-water classification of the INPUT ring is correct (the
// colonist spawn is outside the ring); the bug is downstream in buildNavMesh.
TEST(NavMesh, RealYConfluence_OpenGrassIsFloor) {
	constexpr std::int64_t kProvenanceWater	 = -1;
	constexpr std::int64_t kProvenanceBorder = -3;

	constexpr std::int64_t kProvenanceTree = -2;

	NavMeshInput in;
	in.polygons.push_back(NavInputPolygon{testdata::realBorderRing(), false, kProvenanceBorder});
	in.polygons.push_back(
		NavInputPolygon{testdata::realWaterRing(), true, kProvenanceWater, kNoOpening, true});
	// The 637 scattered tree colliders on the open grass -- solid (not holeCapable).
	for (const std::vector<Vec2i64>& tree : testdata::realTreeRings()) {
		in.polygons.push_back(NavInputPolygon{tree, true, kProvenanceTree});
	}

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty()) << "the real Y-confluence produced no triangles at all";

	int floorFaces	 = 0;
	int terrainFaces = 0;
	for (const NavTriangle& t : m.triangles) {
		if (isFloorFace(t)) {
			++floorFaces;
		}
		if (isCommonKnowledgeTerrainFace(t)) {
			++terrainFaces;
		}
	}

	// The water loop interior must still block.
	EXPECT_GT(terrainFaces, 0) << "the river water loop must be tagged a terrain blocker";
	// The bug: every face is tagged water, leaving ZERO floor (the in-game symptom).
	// The vast open grass outside the thin water loop MUST be walkable floor.
	EXPECT_GT(floorFaces, 0)
		<< "open grass beside the river must be walkable floor; got " << floorFaces << " floor / " << terrainFaces
		<< " terrain out of " << m.triangles.size() << " (the walkable=0 navmesh symptom)";

	// A point in the open grass just beside the colonist spawn, OUTSIDE the water
	// ring (offset off the 1000 mm marching grid so it is strictly interior, not on
	// a ring vertex). The triangle under it must be floor.
	const Vec2i64 grass{1500, 4500};
	ASSERT_EQ(pointInPolygon(grass, testdata::realWaterRing()), PointInPolygon::Outside)
		<< "sanity: the grass point must be outside the water ring (open grass)";
	bool grassOnFloor = false;
	for (const NavTriangle& t : m.triangles) {
		const std::array<Vec2i64, 3> tri = {m.vertices[t.v[0]], m.vertices[t.v[1]], m.vertices[t.v[2]]};
		const std::vector<Vec2i64>	 triRing(tri.begin(), tri.end());
		if (pointInPolygon(grass, triRing) == PointInPolygon::Inside) {
			EXPECT_TRUE(isFloorFace(t)) << "the triangle under the open grass beside the spawn must be walkable floor";
			grassOnFloor = isFloorFace(t);
		}
	}
	EXPECT_TRUE(grassOnFloor) << "no floor triangle covers the open grass at (1500, 4500)";
}
