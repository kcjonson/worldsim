#include "PathQuery.h"
#include "../core/Vec2i64.h"
#include "NavMesh.h"
#include "RraCache.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <queue>
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

	// Common-knowledge terrain (water): a negative provenance sentinel that always
	// blocks regardless of belief, so it bounds the terrain graph the same as the
	// forward graph -- exactly the case where RRA* should buy the biggest win.
	NavInputPolygon water(std::vector<Vec2i64> ring, std::int64_t id) {
		return {std::move(ring), true, id};
	}

	double centroidDist(const NavMesh& m, std::int32_t a, std::int32_t b) {
		const NavTriangle& ta = m.triangles[a];
		const NavTriangle& tb = m.triangles[b];
		const Vec2i64	   ca{(m.vertices[ta.v[0]].x + m.vertices[ta.v[1]].x + m.vertices[ta.v[2]].x) / 3,
							  (m.vertices[ta.v[0]].y + m.vertices[ta.v[1]].y + m.vertices[ta.v[2]].y) / 3};
		const Vec2i64	   cb{(m.vertices[tb.v[0]].x + m.vertices[tb.v[1]].x + m.vertices[tb.v[2]].x) / 3,
							  (m.vertices[tb.v[0]].y + m.vertices[tb.v[1]].y + m.vertices[tb.v[2]].y) / 3};
		const double	   dx = static_cast<double>(cb.x - ca.x);
		const double	   dy = static_cast<double>(cb.y - ca.y);
		return std::sqrt(dx * dx + dy * dy);
	}

	// Mirror of PathQuery::traversable for a TRUTH query (belief.knownSegments ==
	// nullptr): floor, or a wall face a door spans; common-knowledge terrain blocks.
	// Sufficient for the admissibility checks, which all use truth + radius 0 so the
	// width gates are vacuous (diameter 0 <= every width).
	bool forwardTruthTraversable(const NavTriangle& t) {
		return truthTraversable(t);
	}

	// Exact shortest cost from EVERY forward state (triangle, entry-edge) to the goal,
	// over the radius-0 truth forward graph (same centroid-to-centroid cost the real A*
	// uses). A reverse Dijkstra from the goal -- the ground truth the heuristic must not
	// exceed. State packing matches PathQuery: id = tri*4 + entry, entry 3 = "no entry".
	//
	// Reverse edges: a forward step (a,entryA) -> (b, back) exists when b is a neighbor
	// of a across edge e (e != entryA), b is traversable, and back = a's edge index in b.
	// We invert it: for each forward edge we relax the source state from the dest state.
	std::vector<double> trueForwardCostToGoal(const NavMesh& m, std::int32_t goalTri) {
		const std::int32_t n	= static_cast<std::int32_t>(m.triangles.size());
		const double	   kInf = std::numeric_limits<double>::infinity();
		std::vector<double> dist(static_cast<std::size_t>(n) * 4, kInf);

		struct QN {
			double		 d;
			std::int32_t s;
		};
		struct Worse {
			bool operator()(const QN& a, const QN& b) const { return a.d > b.d; }
		};
		std::priority_queue<QN, std::vector<QN>, Worse> pq;

		// Seed every state AT the goal triangle with distance 0 (any entry edge, plus the
		// start-entry sentinel): the forward search stops the instant it pops a goal-tri
		// state, so reaching any of them costs 0 more.
		for (int entry = 0; entry < 4; ++entry) {
			const std::int32_t gs = goalTri * 4 + entry;
			dist[static_cast<std::size_t>(gs)] = 0.0;
			pq.push({0.0, gs});
		}

		auto sharedEdge = [&](const NavTriangle& from, std::int32_t to) {
			for (int e = 0; e < 3; ++e) {
				if (from.neighbor[e] == to) {
					return e;
				}
			}
			return -1;
		};

		while (!pq.empty()) {
			const QN cur = pq.top();
			pq.pop();
			const std::int32_t s = cur.s;
			if (cur.d > dist[static_cast<std::size_t>(s)]) {
				continue;
			}
			const std::int32_t b	= s / 4;
			const int		   back = s % 4;
			// The forward search STOPS the instant it pops a goal-triangle state, so no
			// forward path passes THROUGH the goal. Don't expand goal-tri states backward,
			// or we'd let predecessors route through the goal and underestimate the true
			// cost (which would make this bound wrong-tight). Their own dist (0) stands.
			if (b == goalTri) {
				continue;
			}
			// We are at dest state (b, back). Find every forward source (a, entryA) that
			// steps INTO (b, back): a is the neighbor of b across edge `back` (the edge of
			// b that faces a). For the start sentinel back==3 there is no entry edge, so no
			// predecessor edge constraint of that kind -- skip (start states have no
			// inbound forward edges; they only ever originate).
			if (back == 3) {
				continue;
			}
			const NavTriangle& tb = m.triangles[b];
			const std::int32_t a  = tb.neighbor[back];
			if (a < 0 || !forwardTruthTraversable(m.triangles[a])) {
				continue;
			}
			const int e = sharedEdge(m.triangles[a], b); // a's edge index facing b
			if (e < 0) {
				continue;
			}
			// Forward edge (a, entryA) -> (b, back) is legal for every entryA != e (the
			// U-turn ban), including entryA == 3 (start). Relax each such source state.
			const double step = centroidDist(m, a, b);
			for (int entryA = 0; entryA < 4; ++entryA) {
				if (entryA == e) {
					continue;
				}
				const std::int32_t as = a * 4 + entryA;
				const double	   nd = dist[static_cast<std::size_t>(s)] + step;
				if (nd < dist[static_cast<std::size_t>(as)]) {
					dist[static_cast<std::size_t>(as)] = nd;
					pq.push({nd, as});
				}
			}
		}
		return dist;
	}

	// The tightest admissibility bound for triangle t: the min true forward cost over
	// all of t's states. h(t) must not exceed this.
	double minTrueCostAtTri(const std::vector<double>& stateCost, std::int32_t tri) {
		double best = std::numeric_limits<double>::infinity();
		for (int entry = 0; entry < 4; ++entry) {
			best = std::min(best, stateCost[static_cast<std::size_t>(tri) * 4 + entry]);
		}
		return best;
	}

	// A mesh with a long horizontal wall slab the path must round: a C/U detour where the
	// straight-line heuristic points THROUGH the slab and badly under-guides, so RRA*
	// should prune a lot. The wall spans x in [600,3400] at y in [1400,1600] inside a
	// 4000x3000 region, open at both ends.
	NavMesh buildConcaveWallMesh() {
		NavMeshInput in;
		in.polygons.push_back(border({{0, 0}, {4000, 0}, {4000, 3000}, {0, 3000}}));
		in.polygons.push_back(blocked({{600, 1400}, {3400, 1400}, {3400, 1600}, {600, 1600}}, 7));
		return buildNavMesh(in);
	}

	// A wide region split by a near-spanning wall pierced by obstacle pillars on BOTH
	// sides, so the dual graph has real branching (wide open bays, not a single-file
	// corridor). The wall runs across the middle with its only gap at the far LEFT;
	// start sits bottom-RIGHT, goal top-RIGHT. The straight line points straight up
	// through the wall, so the straight-line heuristic fans across the whole wide bottom
	// bay toward the distant gap before committing -- exactly the wrong-direction fan RRA*
	// prunes. Scattered pillars keep multiple competing frontier triangles alive (so
	// peakOpenSet > 1 and the two heuristics genuinely diverge).
	NavMesh buildWideDetourMesh() {
		NavMeshInput in;
		in.polygons.push_back(border({{0, 0}, {8000, 0}, {8000, 6000}, {0, 6000}}));
		// Mid wall spanning x in [800,8000] at y in [2900,3100]: gap is the far left
		// strip x in [0,800].
		in.polygons.push_back(water({{800, 2900}, {8000, 2900}, {8000, 3100}, {800, 3100}}, -10));
		// Pillars in the lower bay (below the wall) -- force the search to weave, creating
		// several simultaneous open frontier triangles.
		in.polygons.push_back(water({{2000, 900}, {2400, 900}, {2400, 1300}, {2000, 1300}}, -20));
		in.polygons.push_back(water({{4000, 1500}, {4400, 1500}, {4400, 1900}, {4000, 1900}}, -21));
		in.polygons.push_back(water({{6000, 900}, {6400, 900}, {6400, 1300}, {6000, 1300}}, -22));
		in.polygons.push_back(water({{3000, 2000}, {3400, 2000}, {3400, 2400}, {3000, 2400}}, -23));
		in.polygons.push_back(water({{5000, 2000}, {5400, 2000}, {5400, 2400}, {5000, 2400}}, -24));
		// A couple of pillars in the upper bay too, so the approach to the goal also forks.
		in.polygons.push_back(water({{3000, 3600}, {3400, 3600}, {3400, 4000}, {3000, 4000}}, -30));
		in.polygons.push_back(water({{5000, 3600}, {5400, 3600}, {5400, 4000}, {5000, 4000}}, -31));
		return buildNavMesh(in);
	}

} // namespace

// --- Same path, strictly fewer expansions -----------------------------------

TEST(RraHeuristic, SamePathFewerExpansionsConcaveWall) {
	NavMesh m = buildConcaveWallMesh();
	ASSERT_FALSE(m.triangles.empty());

	// Start below the slab on the left, goal above the slab on the right: the straight
	// line crosses the wall, so a good heuristic must "see" the detour around an end.
	const Vec2i64 start{800, 800};
	const Vec2i64 goal{3200, 2400};

	const std::int32_t goalTri = locateTriangle(m, goal);
	ASSERT_GE(goalTri, 0);

	PathResult plain = pathThrough(m, start, goal, 0); // straight-line heuristic
	ASSERT_TRUE(plain.reachable);

	RraCache cache;
	cache.goalTri	 = goalTri;
	PathResult withRra = pathThrough(m, start, goal, 0, {}, &cache);
	ASSERT_TRUE(withRra.reachable);

	// Identical path: RRA* changes only heuristic VALUES, never cost/tie-break/funnel.
	ASSERT_EQ(plain.points.size(), withRra.points.size());
	for (std::size_t i = 0; i < plain.points.size(); ++i) {
		EXPECT_EQ(plain.points[i], withRra.points[i]) << "point " << i << " differs";
	}

	// The whole point: the exact-distance guide expands strictly fewer forward states.
	EXPECT_LT(withRra.nodesExpanded, plain.nodesExpanded)
		<< "RRA* expansions=" << withRra.nodesExpanded << " straight-line=" << plain.nodesExpanded;
	// Both populated the instrumentation.
	EXPECT_GT(plain.nodesExpanded, 0);
	EXPECT_GT(withRra.nodesExpanded, 0);
	EXPECT_GT(plain.peakOpenSet, 0);
	EXPECT_GT(withRra.peakOpenSet, 0);
}

TEST(RraHeuristic, SamePathFewerExpansionsWideDetour) {
	NavMesh m = buildWideDetourMesh();
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 start{7600, 600};	 // bottom-right bay
	const Vec2i64 goal{7600, 5400};	 // top-right bay (straight up runs into the wall)

	const std::int32_t goalTri = locateTriangle(m, goal);
	ASSERT_GE(goalTri, 0);

	PathResult plain = pathThrough(m, start, goal, 0);
	ASSERT_TRUE(plain.reachable);

	RraCache cache;
	cache.goalTri = goalTri;
	PathResult withRra = pathThrough(m, start, goal, 0, {}, &cache);
	ASSERT_TRUE(withRra.reachable);

	ASSERT_EQ(plain.points.size(), withRra.points.size());
	for (std::size_t i = 0; i < plain.points.size(); ++i) {
		EXPECT_EQ(plain.points[i], withRra.points[i]);
	}
	// A wide open detour with branching: the straight-line heuristic fans across the
	// whole wrong-direction bay, RRA* heads for the gap. The win is large here (measured
	// ~82 -> ~16 expansions); assert the qualitative property, not the exact count.
	EXPECT_LT(withRra.nodesExpanded, plain.nodesExpanded)
		<< "RRA* expansions=" << withRra.nodesExpanded << " straight-line=" << plain.nodesExpanded;
	EXPECT_GT(plain.peakOpenSet, 1) << "scenario must actually branch for the heuristics to diverge";
}

// --- Admissibility: h never overestimates the true forward cost --------------

TEST(RraHeuristic, AdmissibleVsTrueForwardCost) {
	NavMesh m = buildConcaveWallMesh();
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64	   goal{3200, 2400};
	const std::int32_t goalTri = locateTriangle(m, goal);
	ASSERT_GE(goalTri, 0);

	const std::vector<double> trueCost = trueForwardCostToGoal(m, goalTri);

	RraCache cache;
	cache.goalTri = goalTri;

	// Check every triangle: the heuristic must be <= the tightest true remaining cost
	// (min over the triangle's entry states), with a small epsilon for float summation
	// order (RRA* and the brute force sum the same edge lengths in different orders).
	const double n = static_cast<double>(m.triangles.size());
	(void)n;
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(m.triangles.size()); ++t) {
		const double h	  = rraHeuristic(cache, m, t);
		const double real = minTrueCostAtTri(trueCost, t);
		if (std::isinf(real)) {
			continue; // t cannot reach the goal through the forward graph: nothing to bound
		}
		ASSERT_FALSE(std::isinf(h)) << "tri " << t << " forward-reachable but h is +inf";
		EXPECT_LE(h, real + 1e-6) << "tri " << t << " h=" << h << " > true " << real;
	}
}

// --- Resume correctness: incremental queries match fresh caches --------------

TEST(RraHeuristic, ResumeMatchesFreshCache) {
	NavMesh m = buildWideDetourMesh();
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64	   goal{7600, 5400};
	const std::int32_t goalTri = locateTriangle(m, goal);
	ASSERT_GE(goalTri, 0);

	const std::int32_t n = static_cast<std::int32_t>(m.triangles.size());

	// One shared cache queried for a scattered sequence of triangles (resuming as it
	// goes) must agree, per triangle, with a FRESH cache asked that triangle cold.
	RraCache shared;
	shared.goalTri = goalTri;

	for (std::int32_t t = 0; t < n; ++t) {
		const double resumed = rraHeuristic(shared, m, t);

		RraCache fresh;
		fresh.goalTri	 = goalTri;
		const double cold = rraHeuristic(fresh, m, t);

		if (std::isinf(cold)) {
			EXPECT_TRUE(std::isinf(resumed)) << "tri " << t << " cold=inf but resumed finite";
		} else {
			EXPECT_NEAR(resumed, cold, 1e-9) << "tri " << t << " resumed=" << resumed << " cold=" << cold;
		}
	}

	// Re-querying an already-settled triangle is stable (a pure table read).
	const double again = rraHeuristic(shared, m, goalTri);
	EXPECT_EQ(again, 0.0) << "goal triangle distance-to-goal is exactly 0";
}

TEST(RraHeuristic, WarmedCacheGivesSamePath) {
	NavMesh m = buildConcaveWallMesh();
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64	   start{800, 800};
	const Vec2i64	   goal{3200, 2400};
	const std::int32_t goalTri = locateTriangle(m, goal);
	ASSERT_GE(goalTri, 0);

	RraCache cache;
	cache.goalTri = goalTri;

	PathResult first = pathThrough(m, start, goal, 0, {}, &cache);
	ASSERT_TRUE(first.reachable);

	// Second query reuses the WARMED cache (most triangles already settled): same path.
	PathResult second = pathThrough(m, start, goal, 0, {}, &cache);
	ASSERT_TRUE(second.reachable);
	ASSERT_EQ(first.points.size(), second.points.size());
	for (std::size_t i = 0; i < first.points.size(); ++i) {
		EXPECT_EQ(first.points[i], second.points[i]);
	}
	// Warm reuse never expands MORE forward states than the cold run (the heuristic
	// values are identical; the cache only saves reverse-search work).
	EXPECT_EQ(first.nodesExpanded, second.nodesExpanded);
}

// --- Belief- and radius-agnostic: one goal cache serves every agent ----------

namespace {
	// 2000x2000 box, walls 200 mm inset, each wall its own belief-gated segment; the
	// south wall's middle [800,1200] is a pathable door span. Same shape the PathQuery
	// belief tests use, so a truth query routes through the door and a blind belief
	// punches straight through the (unseen) wall.
	constexpr std::int64_t kSegS = 100;
	constexpr std::int64_t kSegN = 101;
	constexpr std::int64_t kSegW = 102;
	constexpr std::int64_t kSegE = 103;
	constexpr std::int64_t kDoorOp = 7;

	NavInputPolygon doorSpan(std::vector<Vec2i64> ring, std::int64_t segId, std::int64_t openingId) {
		return {std::move(ring), true, segId, openingId};
	}

	NavMesh buildDoorBox() {
		NavMeshInput in;
		in.polygons.push_back(border({{-2000, -2000}, {4000, -2000}, {4000, 4000}, {-2000, 4000}}));
		in.polygons.push_back(blocked({{0, 1800}, {2000, 1800}, {2000, 2000}, {0, 2000}}, kSegN));
		in.polygons.push_back(blocked({{0, 0}, {200, 0}, {200, 2000}, {0, 2000}}, kSegW));
		in.polygons.push_back(blocked({{1800, 0}, {2000, 0}, {2000, 2000}, {1800, 2000}}, kSegE));
		in.polygons.push_back(blocked({{0, 0}, {800, 0}, {800, 200}, {0, 200}}, kSegS));
		in.polygons.push_back(doorSpan({{800, 0}, {1200, 0}, {1200, 200}, {800, 200}}, kSegS, kDoorOp));
		in.polygons.push_back(blocked({{1200, 0}, {2000, 0}, {2000, 200}, {1200, 200}}, kSegS));
		return buildNavMesh(in);
	}
} // namespace

TEST(RraHeuristic, OneCacheServesTruthAndBeliefAndRadii) {
	NavMesh m = buildDoorBox();
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64	   outside{1000, -500};
	const Vec2i64	   inside{1000, 1000};
	const std::int32_t goalTri = locateTriangle(m, inside);
	ASSERT_GE(goalTri, 0);

	// ONE cache, keyed on the goal triangle, shared by every query below.
	RraCache cache;
	cache.goalTri = goalTri;

	// Truth query (default belief), radius 0: routes through the real door.
	PathResult truth = pathThrough(m, outside, inside, 0, {}, &cache);
	EXPECT_TRUE(truth.reachable) << "truth query routes through the door";

	// Truth query at a non-zero radius reuses the SAME cache (radius-agnostic).
	PathResult truthWide = pathThrough(m, outside, inside, 150, {}, &cache);
	EXPECT_TRUE(truthWide.reachable) << "wider agent still fits the door, same cache";

	// Blind belief (knows nothing): every wall is absent, so it punches straight through
	// where the south wall sits -- the SAME goal cache must still admit it.
	std::unordered_set<std::uint64_t> noSegs;
	std::unordered_set<std::uint64_t> noOps;
	BeliefFilter blind{&noSegs, &noOps};
	PathResult	 belief = pathThrough(m, outside, inside, 0, blind, &cache);
	EXPECT_TRUE(belief.reachable) << "blind belief routes straight through the unseen wall";

	// And the cache yields the SAME paths as no-cache runs (optimal either way): the
	// heuristic only reorders the search, it never changes the routing rules.
	PathResult truthPlain  = pathThrough(m, outside, inside, 0);
	PathResult beliefPlain = pathThrough(m, outside, inside, 0, blind);
	ASSERT_EQ(truth.points.size(), truthPlain.points.size());
	for (std::size_t i = 0; i < truth.points.size(); ++i) {
		EXPECT_EQ(truth.points[i], truthPlain.points[i]) << "truth path point " << i;
	}
	ASSERT_EQ(belief.points.size(), beliefPlain.points.size());
	for (std::size_t i = 0; i < belief.points.size(); ++i) {
		EXPECT_EQ(belief.points[i], beliefPlain.points[i]) << "belief path point " << i;
	}

	// The reverse search built on the terrain graph (width-unfiltered) yields the SAME
	// admissible distances no matter the agent. The belief route crosses the south wall
	// face -- a terrain-graph node, since terrainTraversable treats any wall as open --
	// so the heuristic is finite and admissible for the blind agent too. Spot-check the
	// goal-triangle distance is 0 and the start triangles have finite h under this cache.
	const std::int32_t outTri = locateTriangle(m, outside);
	ASSERT_GE(outTri, 0);
	EXPECT_TRUE(std::isfinite(rraHeuristic(cache, m, outTri)))
		<< "start is terrain-connected to goal: finite reverse distance";
	EXPECT_EQ(rraHeuristic(cache, m, goalTri), 0.0);
}

// --- Mismatched goal falls back to straight-line (no corruption) -------------

TEST(RraHeuristic, MismatchedGoalCacheFallsBackToStraightLine) {
	NavMesh m = buildConcaveWallMesh();
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64 start{800, 800};
	const Vec2i64 goal{3200, 2400};

	// Cache built for a DIFFERENT goal triangle than this query's goal. pathThrough must
	// detect the mismatch and use the straight-line heuristic, producing the exact same
	// result (and expansion count) as a no-cache run.
	RraCache wrongCache;
	wrongCache.goalTri = locateTriangle(m, Vec2i64{200, 200}); // some other triangle
	ASSERT_GE(wrongCache.goalTri, 0);
	ASSERT_NE(wrongCache.goalTri, locateTriangle(m, goal));

	PathResult plain	= pathThrough(m, start, goal, 0);
	PathResult mismatch = pathThrough(m, start, goal, 0, {}, &wrongCache);

	ASSERT_TRUE(mismatch.reachable);
	ASSERT_EQ(plain.points.size(), mismatch.points.size());
	for (std::size_t i = 0; i < plain.points.size(); ++i) {
		EXPECT_EQ(plain.points[i], mismatch.points[i]);
	}
	EXPECT_EQ(plain.nodesExpanded, mismatch.nodesExpanded) << "mismatch must behave exactly like no cache";
}
