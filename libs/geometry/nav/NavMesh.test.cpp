#include "NavMesh.h"
#include "../core/Int128.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"

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

	// BFS over neighbor[] from `start`; returns the set of reachable triangle ids.
	std::set<std::int32_t> reachable(const NavMesh& m, std::int32_t start) {
		std::set<std::int32_t> seen;
		std::queue<std::int32_t> q;
		seen.insert(start);
		q.push(start);
		while (!q.empty()) {
			std::int32_t cur = q.front();
			q.pop();
			for (std::int32_t n : m.triangles[cur].neighbor) {
				if (n >= 0 && seen.insert(n).second) {
					q.push(n);
				}
			}
		}
		return seen;
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

	// Area = border 1000^2 minus obstacle 200^2.
	EXPECT_EQ(totalArea2(m), Int128(2 * (1000 * 1000 - 200 * 200)));

	// No triangle centroid lands inside the obstacle.
	std::vector<Vec2i64> obstacle = {{400, 400}, {600, 400}, {600, 600}, {400, 600}};
	for (const NavTriangle& t : m.triangles) {
		EXPECT_NE(pointInPolygon(centroid(m.vertices, t.v), obstacle), PointInPolygon::Inside);
	}

	// The obstacle's four edges are boundary edges on the walkable side: each is a
	// triangle edge whose neighbor across it is -1 and whose provenance is the
	// obstacle's id.
	std::map<std::pair<Vec2i64, Vec2i64>, bool> obstacleEdgeBoundary;
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
				EXPECT_EQ(t.neighbor[e], -1) << "obstacle edge must be boundary";
				EXPECT_EQ(t.edgeProvenance[e], 7) << "obstacle edge carries provenance";
				++matched;
			}
		}
	}
	EXPECT_EQ(matched, 4) << "all four obstacle edges present as walkable-side boundary edges";
}

TEST(NavMesh, ClosedRoomTwoComponents) {
	// border + a closed room of four blocked wall rectangles (gap-less band). The
	// room interior and the exterior are distinct walkable faces with no shared
	// edge, so BFS from inside never reaches an outside triangle.
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
	// "outside" = a walkable triangle inside the border but outside the wall band.
	std::vector<Vec2i64> wallBlock = {{1000, 1000}, {2000, 1000}, {2000, 2000}, {1000, 2000}};

	std::int32_t inside	 = findTriangleInside(m, room);
	std::int32_t outside = findTriangleInsideButOutside(m, borderRing, wallBlock);
	ASSERT_GE(inside, 0) << "expected at least one triangle inside the room";
	ASSERT_GE(outside, 0) << "expected at least one triangle outside the room";

	std::set<std::int32_t> fromInside = reachable(m, inside);
	EXPECT_EQ(fromInside.count(outside), 0u) << "closed room must be disconnected from the exterior";
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
	// A blocked polygon spanning the full width of the border splits it into a top
	// and bottom walkable region; no BFS path links them.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	in.polygons.push_back(blocked({{0, 900}, {2000, 900}, {2000, 1100}, {0, 1100}}, 50));

	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());
	EXPECT_TRUE(allCcw(m));
	EXPECT_TRUE(neighborsConsistent(m));

	std::vector<Vec2i64> bottom = {{0, 0}, {2000, 0}, {2000, 400}, {0, 400}};
	std::vector<Vec2i64> top	= {{0, 1600}, {2000, 1600}, {2000, 2000}, {0, 2000}};
	std::int32_t		 b		= findTriangleInside(m, bottom);
	std::int32_t		 t		= findTriangleInside(m, top);
	ASSERT_GE(b, 0);
	ASSERT_GE(t, 0);

	std::set<std::int32_t> fromBottom = reachable(m, b);
	EXPECT_EQ(fromBottom.count(t), 0u) << "full-width blocker must disconnect the two sides";

	// Nothing tiles the blocker band.
	std::vector<Vec2i64> blocker = {{0, 900}, {2000, 900}, {2000, 1100}, {0, 1100}};
	for (const NavTriangle& tri : m.triangles) {
		EXPECT_NE(pointInPolygon(centroid(m.vertices, tri.v), blocker), PointInPolygon::Inside);
	}
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

	// Total covered area: first square (1000^2) plus second minus its obstacle.
	EXPECT_EQ(totalArea2(m), Int128(2 * (1000 * 1000 + 1000 * 1000 - 200 * 200)));

	std::vector<Vec2i64> regionA = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	std::vector<Vec2i64> regionB = {{2000, 0}, {3000, 0}, {3000, 1000}, {2000, 1000}};
	EXPECT_GE(findTriangleInside(m, regionA), 0) << "expected triangles in the first border";
	EXPECT_GE(findTriangleInside(m, regionB), 0) << "expected triangles in the second border";

	// No triangle centroid lands outside BOTH borders (i.e. in the dead band
	// between them), and none inside the obstacle.
	std::vector<Vec2i64> obstacle = {{2400, 400}, {2600, 400}, {2600, 600}, {2400, 600}};
	for (const NavTriangle& t : m.triangles) {
		Vec2i64 c		= centroid(m.vertices, t.v);
		bool	inAny	= pointInPolygon(c, regionA) == PointInPolygon::Inside ||
						pointInPolygon(c, regionB) == PointInPolygon::Inside;
		EXPECT_TRUE(inAny) << "triangle must lie inside one of the two borders";
		EXPECT_NE(pointInPolygon(c, obstacle), PointInPolygon::Inside);
	}
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
