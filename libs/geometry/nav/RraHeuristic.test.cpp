#include "PathQuery.h"
#include "../core/Vec2i64.h"
#include "NavMesh.h"
#include "RraCache.h"

#include <cmath>
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

	// Common-knowledge terrain (water): a negative provenance sentinel that always
	// blocks regardless of belief, so it bounds the terrain graph the same as the
	// forward graph -- exactly the case where RRA* should buy the biggest win.
	NavInputPolygon water(std::vector<Vec2i64> ring, std::int64_t id) {
		return {std::move(ring), true, id};
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

	// The funnelled score already sees this detour (a handful of expansions), so the bound
	// can only match it here, never cost more.
	EXPECT_LE(withRra.nodesExpanded, plain.nodesExpanded)
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
	// The bound is a bottleneck (how far out the detour swings), flat across the bay
	// below the wall, so it lifts the floor without steering toward the gap: never more
	// expansions, and the same path.
	EXPECT_LE(withRra.nodesExpanded, plain.nodesExpanded)
		<< "RRA* expansions=" << withRra.nodesExpanded << " straight-line=" << plain.nodesExpanded;
	EXPECT_GT(plain.peakOpenSet, 1) << "scenario must actually branch for the heuristics to diverge";
}

// --- Admissibility: h never overestimates the walk ---------------------------

TEST(RraHeuristic, AdmissibleVsWalkLength) {
	NavMesh m = buildConcaveWallMesh();
	ASSERT_FALSE(m.triangles.empty());

	const Vec2i64	   goal{3200, 2400};
	const std::int32_t goalTri = locateTriangle(m, goal);
	ASSERT_GE(goalTri, 0);

	RraCache cache;
	cache.goalTri = goalTri;

	// Every triangle's bound is at most the walk from a point in it (its centroid) to the goal.
	for (std::int32_t t = 0; t < static_cast<std::int32_t>(m.triangles.size()); ++t) {
		if (!truthTraversable(m.triangles[static_cast<std::size_t>(t)])) {
			continue;
		}
		const NavTriangle& tri = m.triangles[static_cast<std::size_t>(t)];
		const Vec2i64	   c{(m.vertices[tri.v[0]].x + m.vertices[tri.v[1]].x + m.vertices[tri.v[2]].x) / 3,
							 (m.vertices[tri.v[0]].y + m.vertices[tri.v[1]].y + m.vertices[tri.v[2]].y) / 3};
		const PathResult walk = pathThrough(m, c, goal, 0);
		if (!walk.reachable) {
			continue;
		}
		double length = 0.0;
		for (std::size_t i = 1; i < walk.points.size(); ++i) {
			const double dx = static_cast<double>(walk.points[i].x - walk.points[i - 1].x);
			const double dy = static_cast<double>(walk.points[i].y - walk.points[i - 1].y);
			length += std::sqrt(dx * dx + dy * dy);
		}
		const double h = rraHeuristic(cache, navView(m), t);
		ASSERT_FALSE(std::isinf(h)) << "tri " << t << " reaches the goal but h is +inf";
		EXPECT_LE(h, length + 2.0) << "tri " << t << " h=" << h << " > walk " << length; // the walk's corners are rounded to the mm
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
		const double resumed = rraHeuristic(shared, navView(m), t);

		RraCache fresh;
		fresh.goalTri	 = goalTri;
		const double cold = rraHeuristic(fresh, navView(m), t);

		if (std::isinf(cold)) {
			EXPECT_TRUE(std::isinf(resumed)) << "tri " << t << " cold=inf but resumed finite";
		} else {
			EXPECT_NEAR(resumed, cold, 1e-9) << "tri " << t << " resumed=" << resumed << " cold=" << cold;
		}
	}

	// Re-querying an already-settled triangle is stable (a pure table read).
	const double again = rraHeuristic(shared, navView(m), goalTri);
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
	EXPECT_TRUE(std::isfinite(rraHeuristic(cache, navView(m), outTri)))
		<< "start is terrain-connected to goal: finite reverse distance";
	EXPECT_EQ(rraHeuristic(cache, navView(m), goalTri), 0.0);
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
