#include "NavMesh.h"
#include "PathQuery.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"

#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_set>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;
using namespace geometry::nav;

// ---------------------------------------------------------------------------
// P3.3 width-aware connectivity + reachability. The forest answers two things
// cheaply: same-component (connectivity ignoring width) and the bottleneck (the
// widest disc any path admits). The reject is sound: reachable() false must mean
// pathThrough() also returns empty. These tests check that against brute force.
// ---------------------------------------------------------------------------

namespace {

	NavInputPolygon border(std::vector<Vec2i64> ring) {
		return {std::move(ring), false, 1};
	}

	// A common-knowledge terrain ring (water/flora): negative provenance, always
	// blocks under truth and belief alike.
	NavInputPolygon water(std::vector<Vec2i64> ring, std::int64_t id) {
		return {std::move(ring), true, id};
	}

	// A belief-gated wall ring (positive segment id, no door): solid under truth, but
	// the terrain forest treats it as open (the optimistic-belief assumption).
	NavInputPolygon wall(std::vector<Vec2i64> ring, std::int64_t segId) {
		return {std::move(ring), true, segId};
	}

	// Floor-only predicate flood-fill (terrain forest restricted to walls-open). The
	// brute-force ground truth for component membership: BFS over neighbor[] crossing
	// only triangles the predicate admits.
	template <typename Pred>
	std::set<std::int32_t> floodFill(const NavMesh& m, std::int32_t start, Pred admit) {
		std::set<std::int32_t>	 seen;
		std::queue<std::int32_t> q;
		if (start < 0 || !admit(m.triangles[start])) {
			return seen;
		}
		seen.insert(start);
		q.push(start);
		while (!q.empty()) {
			const std::int32_t cur = q.front();
			q.pop();
			for (std::int32_t nb : m.triangles[cur].neighbor) {
				if (nb < 0 || seen.count(nb)) {
					continue;
				}
				if (!admit(m.triangles[nb])) {
					continue;
				}
				seen.insert(nb);
				q.push(nb);
			}
		}
		return seen;
	}

	// Recompute a portal edge's capacity exactly as NavMesh.cpp step 10 does, for the
	// brute-force widest-path check. T-side cap = max of the two apex widths at the
	// portal's endpoints; portal cap = min over both sides and the door clear width.
	std::int64_t sideCap(const NavMesh& m, std::int32_t ti, int e) {
		const std::int64_t w0 = m.triangles[ti].edgePairWidthMm[e];
		const std::int64_t w1 = m.triangles[ti].edgePairWidthMm[(e + 1) % 3];
		return w0 > w1 ? w0 : w1;
	}

	int backEdgeOf(const NavMesh& m, std::int32_t from, std::int32_t to) {
		for (int k = 0; k < 3; ++k) {
			if (m.triangles[to].neighbor[k] == from) {
				return k;
			}
		}
		return -1;
	}

	std::int64_t portalCap(const NavMesh& m, std::int32_t ti, int e) {
		const std::int32_t nb = m.triangles[ti].neighbor[e];
		std::int64_t	   cap = sideCap(m, ti, e);
		const int		   back = backEdgeOf(m, ti, nb);
		if (back >= 0) {
			cap = std::min(cap, sideCap(m, nb, back));
		}
		if (m.triangles[ti].edgeOpening[e] != kNoOpening) {
			cap = std::min(cap, m.triangles[ti].edgeClearWidthMm[e]);
		}
		return cap;
	}

	// Brute-force widest path (max-over-paths-of-min-cap) from src to dst over the
	// predicate-admitted subgraph, via a max-cap Dijkstra. Returns kUnconstrainedWidth
	// for src == dst, 0 if disconnected. This is the ground truth for bottleneck().
	template <typename Pred>
	std::int64_t bruteBottleneck(const NavMesh& m, std::int32_t src, std::int32_t dst, Pred admit) {
		const std::int32_t n = static_cast<std::int32_t>(m.triangles.size());
		if (src == dst) {
			return kUnconstrainedWidth;
		}
		if (src < 0 || dst < 0 || !admit(m.triangles[src]) || !admit(m.triangles[dst])) {
			return 0;
		}
		std::vector<std::int64_t> best(n, 0);
		best[src] = kUnconstrainedWidth;
		// Max-heap on widest known cap to a node.
		using QN = std::pair<std::int64_t, std::int32_t>;
		std::priority_queue<QN> pq;
		pq.push({kUnconstrainedWidth, src});
		while (!pq.empty()) {
			const QN top = pq.top();
			pq.pop();
			const std::int64_t w  = top.first;
			const std::int32_t ti = top.second;
			if (w < best[ti]) {
				continue; // stale
			}
			if (ti == dst) {
				return w;
			}
			for (int e = 0; e < 3; ++e) {
				const std::int32_t nb = m.triangles[ti].neighbor[e];
				if (nb < 0 || !admit(m.triangles[nb])) {
					continue;
				}
				const std::int64_t edgeCap = portalCap(m, ti, e);
				const std::int64_t cand	   = std::min(w, edgeCap);
				if (cand > best[nb]) {
					best[nb] = cand;
					pq.push({cand, nb});
				}
			}
		}
		return best[dst];
	}

	// First triangle whose centroid lies inside `region`, restricted to floor faces so
	// callers land on walkable floor (not the wall band triangulated inside a region).
	Vec2i64 centroidOf(const NavMesh& m, std::int32_t ti) {
		const NavTriangle& t = m.triangles[ti];
		return {(m.vertices[t.v[0]].x + m.vertices[t.v[1]].x + m.vertices[t.v[2]].x) / 3,
				(m.vertices[t.v[0]].y + m.vertices[t.v[1]].y + m.vertices[t.v[2]].y) / 3};
	}

	std::int32_t floorTriInside(const NavMesh& m, const std::vector<Vec2i64>& region) {
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
			if (m.triangles[ti].faceBlocker != kNoBlocker) {
				continue;
			}
			if (pointInPolygon(centroidOf(m, ti), region) == PointInPolygon::Inside) {
				return ti;
			}
		}
		return -1;
	}

	// Deterministic LCG (Knuth MMIX), seeded constant: reproducible pseudo-random
	// int64 stream for the fuzz harness. No std::random, no RNG state across runs.
	struct LCG {
		std::uint64_t state;
		explicit LCG(std::uint64_t seed) : state(seed) {}
		std::int64_t next(std::int64_t lo, std::int64_t hi) {
			state					  = state * UINT64_C(6364136223846793005) + UINT64_C(1442695040888963407);
			const std::uint64_t range = static_cast<std::uint64_t>(hi - lo) + 1;
			return lo + static_cast<std::int64_t>((state >> 32) % range);
		}
	};

} // namespace

// --- Component correctness: forest connectivity == brute-force flood-fill ----

TEST(Reachability, ComponentMatchesFloodFillTruth) {
	// A multi-region mesh: an open border with a full-width water band (splits floor
	// into two regions) and a sealed wall room. Truth components must match a truth-
	// predicate flood-fill for every pair of floor triangles.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}}));
	// Water band splits the lower half from the upper half (common knowledge: blocks
	// truth and terrain alike).
	in.polygons.push_back(water({{0, 1900}, {4000, 1900}, {4000, 2100}, {0, 2100}}, -10));
	// A sealed wall room in the upper half (no door): truth-isolated interior.
	in.polygons.push_back(wall({{1000, 3000}, {3000, 3000}, {3000, 3100}, {1000, 3100}}, 20)); // bottom
	in.polygons.push_back(wall({{1000, 3500}, {3000, 3500}, {3000, 3600}, {1000, 3600}}, 21)); // top
	in.polygons.push_back(wall({{1000, 3100}, {1100, 3100}, {1100, 3500}, {1000, 3500}}, 22)); // left
	in.polygons.push_back(wall({{2900, 3100}, {3000, 3100}, {3000, 3500}, {2900, 3500}}, 23)); // right
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	auto truthPred = [](const NavTriangle& t) { return truthTraversable(t); };

	// For every traversable triangle, the forest component partition must agree with a
	// flood-fill from that triangle: same component iff reachable by the predicate BFS.
	for (std::int32_t a = 0; a < static_cast<std::int32_t>(m.triangles.size()); ++a) {
		if (!truthPred(m.triangles[a])) {
			EXPECT_EQ(m.truthForest.component[a], -1) << "non-node must have component -1";
			continue;
		}
		std::set<std::int32_t> reach = floodFill(m, a, truthPred);
		for (std::int32_t b = 0; b < static_cast<std::int32_t>(m.triangles.size()); ++b) {
			if (!truthPred(m.triangles[b])) {
				continue;
			}
			const bool sameComp = reachableInForest(m.truthForest, a, b);
			const bool flooded	= reach.count(b) != 0;
			ASSERT_EQ(sameComp, flooded) << "truth component mismatch a=" << a << " b=" << b;
		}
	}
}

TEST(Reachability, ComponentMatchesFloodFillTerrain) {
	// Same mesh; the terrain forest treats walls as OPEN, so the wall room's interior
	// joins the upper floor region. Only the water band still splits. Compare to a
	// terrain-predicate flood-fill.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 4000}, {0, 4000}}));
	in.polygons.push_back(water({{0, 1900}, {4000, 1900}, {4000, 2100}, {0, 2100}}, -10));
	in.polygons.push_back(wall({{1000, 3000}, {3000, 3000}, {3000, 3100}, {1000, 3100}}, 20));
	in.polygons.push_back(wall({{1000, 3500}, {3000, 3500}, {3000, 3600}, {1000, 3600}}, 21));
	in.polygons.push_back(wall({{1000, 3100}, {1100, 3100}, {1100, 3500}, {1000, 3500}}, 22));
	in.polygons.push_back(wall({{2900, 3100}, {3000, 3100}, {3000, 3500}, {2900, 3500}}, 23));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	auto terrainPred = [](const NavTriangle& t) { return terrainTraversable(t); };

	for (std::int32_t a = 0; a < static_cast<std::int32_t>(m.triangles.size()); ++a) {
		if (!terrainPred(m.triangles[a])) {
			EXPECT_EQ(m.terrainForest.component[a], -1);
			continue;
		}
		std::set<std::int32_t> reach = floodFill(m, a, terrainPred);
		for (std::int32_t b = 0; b < static_cast<std::int32_t>(m.triangles.size()); ++b) {
			if (!terrainPred(m.triangles[b])) {
				continue;
			}
			const bool sameComp = reachableInForest(m.terrainForest, a, b);
			const bool flooded	= reach.count(b) != 0;
			ASSERT_EQ(sameComp, flooded) << "terrain component mismatch a=" << a << " b=" << b;
		}
	}
}

// --- Across-water reject: different terrain component, no A* exploration ------

TEST(Reachability, AcrossWaterRejectedBothForests) {
	// Two land regions split by a full-width water band. Water is common knowledge, so
	// BOTH the truth and terrain forests place the two regions in different components.
	// reachable() is false for both belief modes, and pathThrough returns empty.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	in.polygons.push_back(water({{0, 900}, {2000, 900}, {2000, 1100}, {0, 1100}}, -1));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 below{1000, 300};
	const Vec2i64 above{1000, 1700};

	// Truth query (default belief): rejected.
	EXPECT_FALSE(reachable(m, below, above, 0));
	EXPECT_FALSE(pathThrough(m, below, above, 0).reachable);
	EXPECT_TRUE(pathThrough(m, below, above, 0).points.empty());

	// Belief query (terrain forest): an unseen wall would be optimistically crossed,
	// but water is common knowledge, so still rejected.
	std::unordered_set<std::uint64_t> noSegs;
	std::unordered_set<std::uint64_t> noOps;
	BeliefFilter					  belief{&noSegs, &noOps};
	EXPECT_FALSE(reachable(m, below, above, 0, belief));
	EXPECT_FALSE(pathThrough(m, below, above, 0, belief).reachable);

	// And the two start/goal triangles really are in different terrain components.
	const std::int32_t bt = floorTriInside(m, {{0, 0}, {2000, 0}, {2000, 400}, {0, 400}});
	const std::int32_t at = floorTriInside(m, {{0, 1600}, {2000, 1600}, {2000, 2000}, {0, 2000}});
	ASSERT_GE(bt, 0);
	ASSERT_GE(at, 0);
	EXPECT_FALSE(reachableInForest(m.terrainForest, bt, at)) << "water must split terrain components";
	EXPECT_FALSE(reachableInForest(m.truthForest, bt, at)) << "water must split truth components";
}

// --- Bottleneck correctness vs brute force on a known narrow gap -------------

TEST(Reachability, BottleneckMatchesBruteForce) {
	// A full-width water band with a single 400 mm gap. The bottleneck between a
	// triangle below and one above must equal the brute-force widest path (which is
	// pinned by the 400 mm gap), and that value must be 400 for the floor route.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 2000}, {0, 2000}}));
	in.polygons.push_back(water({{0, 900}, {1800, 900}, {1800, 1100}, {0, 1100}}, -10));
	in.polygons.push_back(water({{2200, 900}, {4000, 900}, {4000, 1100}, {2200, 1100}}, -11));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	auto terrainPred = [](const NavTriangle& t) { return terrainTraversable(t); };

	// Check bottleneck == brute force for every floor pair in a sampled set (the full
	// O(n^2) over all floor triangles, the mesh is small).
	std::vector<std::int32_t> floorTris;
	for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(m.triangles.size()); ++ti) {
		if (m.triangles[ti].faceBlocker == kNoBlocker) {
			floorTris.push_back(ti);
		}
	}
	ASSERT_FALSE(floorTris.empty());
	for (std::int32_t a : floorTris) {
		for (std::int32_t b : floorTris) {
			const std::int64_t forestBn = bottleneckInForest(m.terrainForest, a, b);
			const std::int64_t bruteBn	= bruteBottleneck(m, a, b, terrainPred);
			ASSERT_EQ(forestBn, bruteBn) << "bottleneck mismatch a=" << a << " b=" << b;
		}
	}

	// The specific below/above route is pinned by the 400 mm gap.
	const std::int32_t bt = floorTriInside(m, {{0, 0}, {4000, 0}, {4000, 400}, {0, 400}});
	const std::int32_t at = floorTriInside(m, {{0, 1600}, {4000, 1600}, {4000, 2000}, {0, 2000}});
	ASSERT_GE(bt, 0);
	ASSERT_GE(at, 0);
	EXPECT_EQ(bottleneckInForest(m.terrainForest, bt, at), 400) << "the route's bottleneck is the 400 mm gap";
}

// --- Fit-by-clearance: small agent passes, large agent rejected --------------

TEST(Reachability, FitByClearanceNarrowGap) {
	// Same 400 mm gap. A small agent (diameter 300 <= 400) is reachable and pathThrough
	// finds a path; a large agent (diameter 402 > 400) is rejected and pathThrough is
	// empty. The reachable() reject and the A* agree.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 2000}, {0, 2000}}));
	in.polygons.push_back(water({{0, 900}, {1800, 900}, {1800, 1100}, {0, 1100}}, -10));
	in.polygons.push_back(water({{2200, 900}, {4000, 900}, {4000, 1100}, {2200, 1100}}, -11));
	NavMesh m = buildNavMesh(in);

	const Vec2i64 below{2000, 300};
	const Vec2i64 above{2000, 1700};

	// Small agent: maybe-reachable AND actually pathable.
	EXPECT_TRUE(reachable(m, below, above, 150)) << "agent narrower than the gap must be reachable";
	EXPECT_TRUE(pathThrough(m, below, above, 150).reachable);

	// Large agent: rejected, and the A* short-circuit yields empty.
	EXPECT_FALSE(reachable(m, below, above, 201)) << "agent wider than the gap must be rejected";
	PathResult tooWide = pathThrough(m, below, above, 201);
	EXPECT_FALSE(tooWide.reachable);
	EXPECT_TRUE(tooWide.points.empty());

	// Exact threshold: diameter == gap passes, one mm over fails.
	EXPECT_TRUE(reachable(m, below, above, 200)) << "diameter 400 == gap 400 must be reachable";
	EXPECT_FALSE(reachable(m, below, above, 201)) << "diameter 402 > gap 400 must be rejected";
}

// --- Truth-unreachable but belief-reachable ---------------------------------

TEST(Reachability, TruthUnreachableButBeliefNotRejected) {
	// A region enclosed by walls with NO door. Under truth the interior is a separate
	// component (reject -- goal validity says no real route). But the terrain forest
	// treats the walls as open, so it is connected: a belief query that does NOT know
	// these walls must NOT be rejected (the agent could optimistically route through
	// the unseen walls). This proves the terrain reject does not false-fire.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	// Sealed wall room (segments 20..23, no door).
	in.polygons.push_back(wall({{1000, 1000}, {2000, 1000}, {2000, 1100}, {1000, 1100}}, 20)); // bottom
	in.polygons.push_back(wall({{1000, 1900}, {2000, 1900}, {2000, 2000}, {1000, 2000}}, 21)); // top
	in.polygons.push_back(wall({{1000, 1100}, {1100, 1100}, {1100, 1900}, {1000, 1900}}, 22)); // left
	in.polygons.push_back(wall({{1900, 1100}, {2000, 1100}, {2000, 1900}, {1900, 1900}}, 23)); // right
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 outside{500, 500};   // exterior floor
	const Vec2i64 inside{1500, 1500};  // sealed interior floor

	// Truth: the interior is a different truth component -> rejected, and pathThrough
	// (truth) is empty (matches the v1 sealed-room behaviour).
	EXPECT_FALSE(reachable(m, outside, inside, 0)) << "sealed room is truth-unreachable";
	EXPECT_FALSE(pathThrough(m, outside, inside, 0).reachable);

	// The interior and exterior really are different TRUTH components but the SAME
	// terrain component (walls open).
	const std::int32_t inT  = floorTriInside(m, {{1100, 1100}, {1900, 1100}, {1900, 1900}, {1100, 1900}});
	const std::int32_t outT = floorTriInside(m, {{0, 0}, {900, 0}, {900, 900}, {0, 900}});
	ASSERT_GE(inT, 0);
	ASSERT_GE(outT, 0);
	EXPECT_FALSE(reachableInForest(m.truthForest, inT, outT)) << "truth walls separate the interior";
	EXPECT_TRUE(reachableInForest(m.terrainForest, inT, outT)) << "terrain treats walls as open";

	// Belief query that knows NOTHING (knownSegments set but empty): the agent treats
	// the unseen walls as absent, so the terrain forest must NOT reject. reachable()
	// returns true (maybe) and pathThrough actually finds the optimistic route.
	std::unordered_set<std::uint64_t> noSegs;
	std::unordered_set<std::uint64_t> noOps;
	BeliefFilter					  belief{&noSegs, &noOps};
	EXPECT_TRUE(reachable(m, outside, inside, 0, belief))
		<< "terrain reject must not false-fire when an agent could route through unseen walls";
	EXPECT_TRUE(pathThrough(m, outside, inside, 0, belief).reachable)
		<< "the optimistic belief route through unseen walls must succeed";
}

// --- Door span: terrain reject must not block a door route either ------------

TEST(Reachability, DoorRouteNotRejected) {
	// A room with a 200 mm-clear door. Truth: reachable through the door (the door
	// passes); the forest must not reject a fitting agent, and must reject one too wide
	// for the door clear width.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {3000, 0}, {3000, 3000}, {0, 3000}}));
	std::vector<Vec2i64> bottomLeft	 = {{1000, 1000}, {1400, 1000}, {1400, 1100}, {1000, 1100}};
	std::vector<Vec2i64> bottomRight = {{1600, 1000}, {2000, 1000}, {2000, 1100}, {1600, 1100}};
	std::vector<Vec2i64> top		 = {{1000, 1900}, {2000, 1900}, {2000, 2000}, {1000, 2000}};
	std::vector<Vec2i64> left		 = {{1000, 1100}, {1100, 1100}, {1100, 1900}, {1000, 1900}};
	std::vector<Vec2i64> right		 = {{1900, 1100}, {2000, 1100}, {2000, 1900}, {1900, 1900}};
	in.polygons.push_back(wall(bottomLeft, 20));
	in.polygons.push_back(wall(bottomRight, 24));
	in.polygons.push_back(wall(top, 21));
	in.polygons.push_back(wall(left, 22));
	in.polygons.push_back(wall(right, 23));
	DoorPortal door;
	door.openingId	  = 99;
	door.a			  = {1400, 1100};
	door.b			  = {1600, 1100};
	door.clearWidthMm = 200;
	in.doors.push_back(door);
	NavMesh m = buildNavMesh(in);

	const Vec2i64 start{1500, 500};
	const Vec2i64 goal{1500, 1500};

	// Truth, agent fits the 200 mm door (diameter 160): reachable + pathable.
	EXPECT_TRUE(reachable(m, start, goal, 80));
	EXPECT_TRUE(pathThrough(m, start, goal, 80).reachable);
	// Truth, agent too wide for the door (diameter 300 > 200): rejected via the door
	// clear width in the bottleneck.
	EXPECT_FALSE(reachable(m, start, goal, 150));
	EXPECT_TRUE(pathThrough(m, start, goal, 150).points.empty());
}

// --- CRITICAL soundness fuzz: reachable() false => pathThrough() empty --------

namespace {

	// A small parametric mesh family for the fuzz: a border with a water band gap of
	// variable width, optionally a sealed wall room, so we exercise both forests, door
	// caps, and disconnects. The variant index selects the layout deterministically.
	NavMesh buildFuzzMesh(int variant) {
		NavMeshInput in;
		in.polygons.push_back(border({{0, 0}, {5000, 0}, {5000, 5000}, {0, 5000}}));
		switch (variant % 5) {
			case 0:
				// Open box, no obstacles.
				break;
			case 1:
				// Full-width water band, single 400 mm gap.
				in.polygons.push_back(water({{0, 2400}, {2300, 2400}, {2300, 2600}, {0, 2600}}, -10));
				in.polygons.push_back(water({{2700, 2400}, {5000, 2400}, {5000, 2600}, {2700, 2600}}, -11));
				break;
			case 2:
				// Full-width water band, NO gap (hard disconnect).
				in.polygons.push_back(water({{0, 2400}, {5000, 2400}, {5000, 2600}, {0, 2600}}, -10));
				break;
			case 3: {
				// Sealed wall room (no door) in the lower-left.
				in.polygons.push_back(wall({{500, 500}, {2000, 500}, {2000, 600}, {500, 600}}, 20));
				in.polygons.push_back(wall({{500, 1900}, {2000, 1900}, {2000, 2000}, {500, 2000}}, 21));
				in.polygons.push_back(wall({{500, 600}, {600, 600}, {600, 1900}, {500, 1900}}, 22));
				in.polygons.push_back(wall({{1900, 600}, {2000, 600}, {2000, 1900}, {1900, 1900}}, 23));
				break;
			}
			case 4: {
				// Wall room WITH a 300 mm-clear door in its bottom wall.
				in.polygons.push_back(wall({{500, 500}, {1100, 500}, {1100, 600}, {500, 600}}, 20));
				in.polygons.push_back(wall({{1400, 500}, {2000, 500}, {2000, 600}, {1400, 600}}, 24));
				in.polygons.push_back(wall({{500, 1900}, {2000, 1900}, {2000, 2000}, {500, 2000}}, 21));
				in.polygons.push_back(wall({{500, 600}, {600, 600}, {600, 1900}, {500, 1900}}, 22));
				in.polygons.push_back(wall({{1900, 600}, {2000, 600}, {2000, 1900}, {1900, 1900}}, 23));
				DoorPortal d;
				d.openingId	   = 7;
				d.a			   = {1100, 600};
				d.b			   = {1400, 600};
				d.clearWidthMm = 300;
				in.doors.push_back(d);
				break;
			}
		}
		return buildNavMesh(in);
	}

} // namespace

TEST(Reachability, SoundnessFuzzRejectImpliesNoPath) {
	// The core invariant: if reachable() returns false, pathThrough() with the SAME
	// (start, goal, radius, belief) must return an empty/unreachable path. A reject
	// must never hide a real route. Generated arithmetically (LCG), no RNG.
	LCG rng(0x5EED1234ABCDULL);

	int rejected		 = 0; // count of false reachable() (the interesting branch)
	int overApproxAllowed = 0; // reachable() true but pathThrough empty (fine)
	for (int variant = 0; variant < 5; ++variant) {
		NavMesh m = buildFuzzMesh(variant);
		ASSERT_FALSE(m.triangles.empty());

		// Two belief modes: truth (nullptr) and "knows nothing" (empty sets -> terrain).
		std::unordered_set<std::uint64_t> noSegs;
		std::unordered_set<std::uint64_t> noOps;

		for (int i = 0; i < 400; ++i) {
			const Vec2i64 start{rng.next(-200, 5200), rng.next(-200, 5200)};
			const Vec2i64 goal{rng.next(-200, 5200), rng.next(-200, 5200)};
			const std::int64_t radius = rng.next(0, 400);
			const bool		   useBelief = (rng.next(0, 1) == 1);
			BeliefFilter	   belief	 = useBelief ? BeliefFilter{&noSegs, &noOps} : BeliefFilter{};

			const bool		 maybe = reachable(m, start, goal, radius, belief);
			const PathResult path  = pathThrough(m, start, goal, radius, belief);

			if (!maybe) {
				// SOUND: a reject must imply no path.
				ASSERT_FALSE(path.reachable)
					<< "UNSOUND reject: variant=" << variant << " i=" << i << " start=(" << start.x << ","
					<< start.y << ") goal=(" << goal.x << "," << goal.y << ") r=" << radius
					<< " belief=" << useBelief;
				ASSERT_TRUE(path.points.empty());
				++rejected;
			} else if (!path.reachable) {
				++overApproxAllowed; // reachable() optimistic, A* said no: allowed
			}
		}
	}
	// Sanity: the fuzz actually exercised the reject branch (disconnects + too-wide
	// agents guarantee some), so the invariant is not vacuously satisfied.
	EXPECT_GT(rejected, 0) << "fuzz must hit the reject branch to be meaningful";
	// (overApproxAllowed may be 0 or positive; both are acceptable.)
	(void)overApproxAllowed;
}

TEST(Reachability, SameTriangleIsReachable) {
	// Degenerate: start and goal in the same triangle. reachable() is true regardless
	// of radius (the bottleneck is unconstrained), matching pathThrough's same-triangle
	// fast path.
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}));
	NavMesh m = buildNavMesh(in);
	ASSERT_FALSE(m.triangles.empty());

	// Build two points guaranteed to share a triangle: the centroid, and the centroid
	// nudged a few mm toward vertex 0 (still strictly interior to the same triangle).
	const std::int32_t ti = 0;
	const Vec2i64	   a  = centroidOf(m, ti);
	const Vec2i64&	   v0 = m.vertices[m.triangles[ti].v[0]];
	const Vec2i64	   b{a.x + (v0.x - a.x) / 8, a.y + (v0.y - a.y) / 8};
	ASSERT_EQ(locateTriangle(m, a), ti) << "test setup: centroid must be in triangle 0";
	ASSERT_EQ(locateTriangle(m, b), ti) << "test setup: both points must share a triangle";
	EXPECT_TRUE(reachable(m, a, b, 100));
	EXPECT_TRUE(reachable(m, a, b, 0));
}

TEST(Reachability, OffMeshIsUnreachable) {
	NavMeshInput in;
	in.polygons.push_back(border({{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}}));
	NavMesh m = buildNavMesh(in);

	EXPECT_FALSE(reachable(m, Vec2i64{-100, 500}, Vec2i64{500, 500}, 0)) << "off-mesh start";
	EXPECT_FALSE(reachable(m, Vec2i64{500, 500}, Vec2i64{9000, 9000}, 0)) << "off-mesh goal";
}
