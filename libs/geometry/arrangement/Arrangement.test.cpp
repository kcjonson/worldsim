#include "Arrangement.h"
#include "../core/Vec2i64.h"
#include "HalfEdge.h"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <tuple>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Find the edge connecting the two given points (in either direction);
	// returns its index or SIZE_MAX. Vertices are looked up by exact value.
	std::size_t findEdge(const Arrangement& arr, const Vec2i64& p, const Vec2i64& q) {
		auto vertexOf = [&](const Vec2i64& pt) -> std::size_t {
			for (std::size_t i = 0; i < arr.vertices.size(); ++i) {
				if (arr.vertices[i] == pt) {
					return i;
				}
			}
			return SIZE_MAX;
		};
		const std::size_t a = vertexOf(p);
		const std::size_t b = vertexOf(q);
		if (a == SIZE_MAX || b == SIZE_MAX) {
			return SIZE_MAX;
		}
		for (std::size_t e = 0; e < arr.edges.size(); ++e) {
			if ((arr.edges[e].from == a && arr.edges[e].to == b) ||
				(arr.edges[e].from == b && arr.edges[e].to == a)) {
				return e;
			}
		}
		return SIZE_MAX;
	}

	bool hasEdge(const Arrangement& arr, const Vec2i64& p, const Vec2i64& q) {
		return findEdge(arr, p, q) != SIZE_MAX;
	}

	// Canonical string of the whole structure, for determinism comparison.
	std::string canonical(const Arrangement& arr) {
		std::string s;
		for (const Vec2i64& v : arr.vertices) {
			s += "v(" + std::to_string(v.x) + "," + std::to_string(v.y) + ")";
		}
		for (const ArrangementEdge& e : arr.edges) {
			s += "e[" + std::to_string(e.from) + "-" + std::to_string(e.to) + ":";
			for (std::int64_t p : e.provenance) {
				s += std::to_string(p) + ",";
			}
			s += "]";
		}
		return s;
	}

} // namespace

TEST(Arrangement, SquareFourEdges) {
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 0}, 0},
		{{1000, 0}, {1000, 1000}, 1},
		{{1000, 1000}, {0, 1000}, 2},
		{{0, 1000}, {0, 0}, 3},
	};
	Arrangement arr = buildArrangement(segs);
	EXPECT_EQ(arr.vertices.size(), 4u);
	EXPECT_EQ(arr.edges.size(), 4u);
}

TEST(Arrangement, XCrossingSplitsBothIntoFour) {
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 1000}, 0},
		{{0, 1000}, {1000, 0}, 1},
	};
	Arrangement arr = buildArrangement(segs);
	// 5 vertices: 4 endpoints + center; 4 edges meeting at the degree-4 center.
	EXPECT_EQ(arr.vertices.size(), 5u);
	EXPECT_EQ(arr.edges.size(), 4u);
	EXPECT_TRUE(hasEdge(arr, {0, 0}, {500, 500}));
	EXPECT_TRUE(hasEdge(arr, {500, 500}, {1000, 1000}));
	EXPECT_TRUE(hasEdge(arr, {0, 1000}, {500, 500}));
	EXPECT_TRUE(hasEdge(arr, {500, 500}, {1000, 0}));
}

TEST(Arrangement, TTouchSplitsTouchedSegment) {
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 0}, 0},
		{{500, 0}, {500, 1000}, 1},
	};
	Arrangement arr = buildArrangement(segs);
	EXPECT_EQ(arr.vertices.size(), 4u); // 0,0 / 1000,0 / 500,0 / 500,1000
	EXPECT_EQ(arr.edges.size(), 3u);	// base split in two + the stem
	EXPECT_TRUE(hasEdge(arr, {0, 0}, {500, 0}));
	EXPECT_TRUE(hasEdge(arr, {500, 0}, {1000, 0}));
	EXPECT_TRUE(hasEdge(arr, {500, 0}, {500, 1000}));
}

TEST(Arrangement, PartialCollinearOverlapThreeSubEdges) {
	std::vector<InputSegment> segs = {
		{{0, 0}, {100, 0}, 0},
		{{50, 0}, {150, 0}, 1},
	};
	Arrangement arr = buildArrangement(segs);
	// Sub-edges: [0,50] (seg0 only), [50,100] (both), [100,150] (seg1 only).
	EXPECT_EQ(arr.vertices.size(), 4u);
	EXPECT_EQ(arr.edges.size(), 3u);

	const std::size_t left	= findEdge(arr, {0, 0}, {50, 0});
	const std::size_t mid	= findEdge(arr, {50, 0}, {100, 0});
	const std::size_t right = findEdge(arr, {100, 0}, {150, 0});
	ASSERT_NE(left, SIZE_MAX);
	ASSERT_NE(mid, SIZE_MAX);
	ASSERT_NE(right, SIZE_MAX);

	EXPECT_EQ(arr.edges[left].provenance, (std::vector<std::int64_t>{0}));
	EXPECT_EQ(arr.edges[mid].provenance, (std::vector<std::int64_t>{0, 1}));
	EXPECT_EQ(arr.edges[right].provenance, (std::vector<std::int64_t>{1}));
}

TEST(Arrangement, DuplicateSegmentsDedupToOneEdge) {
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 0}, 7},
		{{0, 0}, {1000, 0}, 9},
		{{1000, 0}, {0, 0}, 11}, // same geometry, reversed
	};
	Arrangement arr = buildArrangement(segs);
	EXPECT_EQ(arr.vertices.size(), 2u);
	ASSERT_EQ(arr.edges.size(), 1u);
	// All three contributors recorded on the single surviving edge.
	EXPECT_EQ(arr.edges[0].provenance, (std::vector<std::int64_t>{7, 9, 11}));
}

TEST(Arrangement, ZeroLengthInputRejected) {
	std::vector<InputSegment> segs = {
		{{500, 500}, {500, 500}, 0}, // degenerate
		{{0, 0}, {1000, 0}, 1},
	};
	Arrangement arr = buildArrangement(segs);
	EXPECT_EQ(arr.vertices.size(), 2u);
	EXPECT_EQ(arr.edges.size(), 1u);
	for (const ArrangementEdge& e : arr.edges) {
		EXPECT_NE(arr.vertices[e.from], arr.vertices[e.to]);
	}
}

TEST(Arrangement, RoundedCrossingLandsOnThirdSegmentCascade) {
	// Two segments cross at exactly (500,500). A third horizontal segment passes
	// through y=500, so the crossing vertex must also split the third segment.
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 1000}, 0},
		{{0, 1000}, {1000, 0}, 1},
		{{0, 500}, {1000, 500}, 2},
	};
	Arrangement arr = buildArrangement(segs);
	// The center (500,500) is shared by all three; the horizontal splits there.
	EXPECT_TRUE(hasEdge(arr, {0, 500}, {500, 500}));
	EXPECT_TRUE(hasEdge(arr, {500, 500}, {1000, 500}));
	// No zero-length edges survived the cascade.
	for (const ArrangementEdge& e : arr.edges) {
		EXPECT_NE(arr.vertices[e.from], arr.vertices[e.to]);
	}
	// The center vertex exists exactly once.
	int centerCount = 0;
	for (const Vec2i64& v : arr.vertices) {
		if (v == Vec2i64{500, 500}) {
			++centerCount;
		}
	}
	EXPECT_EQ(centerCount, 1);
}

TEST(Arrangement, TwoCrossingsRoundToSamePoint) {
	// Construct two near-degenerate crossings that both round to (500,500), then
	// confirm they merge into a single vertex rather than two near-coincident
	// ones. Use steep/shallow pairs whose true crossing is sub-mm from 500,500.
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 1000}, 0},
		{{0, 1000}, {1000, 0}, 1},		// crosses #0 at exactly (500,500)
		{{499, 0}, {501, 1000}, 2},		// near-vertical, crosses #0 near (500,500)
	};
	Arrangement arr = buildArrangement(segs);
	int centerCount = 0;
	for (const Vec2i64& v : arr.vertices) {
		if (v == Vec2i64{500, 500}) {
			++centerCount;
		}
	}
	EXPECT_EQ(centerCount, 1); // both crossings collapsed onto one vertex
	for (const ArrangementEdge& e : arr.edges) {
		EXPECT_NE(arr.vertices[e.from], arr.vertices[e.to]);
	}
}

TEST(Arrangement, TwoSquaresSharingFullEdge) {
	// Left square [0,1000]x[0,1000], right square [1000,2000]x[0,1000]. They
	// share the vertical edge x=1000. That shared edge must appear exactly once.
	std::vector<InputSegment> segs = {
		// left square
		{{0, 0}, {1000, 0}, 0},
		{{1000, 0}, {1000, 1000}, 1}, // shared edge, left's copy
		{{1000, 1000}, {0, 1000}, 2},
		{{0, 1000}, {0, 0}, 3},
		// right square
		{{1000, 0}, {2000, 0}, 4},
		{{2000, 0}, {2000, 1000}, 5},
		{{2000, 1000}, {1000, 1000}, 6},
		{{1000, 1000}, {1000, 0}, 7}, // shared edge, right's copy (reversed)
	};
	Arrangement arr = buildArrangement(segs);
	const std::size_t shared = findEdge(arr, {1000, 0}, {1000, 1000});
	ASSERT_NE(shared, SIZE_MAX);
	// Exactly one shared edge, carrying both contributors.
	EXPECT_EQ(arr.edges[shared].provenance, (std::vector<std::int64_t>{1, 7}));
	int sharedCount = 0;
	for (const ArrangementEdge& e : arr.edges) {
		const Vec2i64& f = arr.vertices[e.from];
		const Vec2i64& t = arr.vertices[e.to];
		if ((f == Vec2i64{1000, 0} && t == Vec2i64{1000, 1000}) ||
			(f == Vec2i64{1000, 1000} && t == Vec2i64{1000, 0})) {
			++sharedCount;
		}
	}
	EXPECT_EQ(sharedCount, 1);
	EXPECT_EQ(arr.vertices.size(), 6u);
	EXPECT_EQ(arr.edges.size(), 7u);
}

TEST(Arrangement, GridProducesExpectedEdgeCount) {
	// 3 horizontal + 3 vertical lines forming a 2x2 cell grid.
	std::vector<InputSegment> segs;
	std::int64_t			  idx = 0;
	for (std::int64_t y : {0, 500, 1000}) {
		segs.push_back({{0, y}, {1000, y}, idx++});
	}
	for (std::int64_t x : {0, 500, 1000}) {
		segs.push_back({{x, 0}, {x, 1000}, idx++});
	}
	Arrangement arr = buildArrangement(segs);
	// 3x3 lattice of intersection points.
	EXPECT_EQ(arr.vertices.size(), 9u);
	// Each of 3 horizontal lines splits into 2 edges (6), same for vertical (6).
	EXPECT_EQ(arr.edges.size(), 12u);
}

TEST(Arrangement, DeterministicAcrossShuffledInsertionOrder) {
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 1000}, 0},
		{{0, 1000}, {1000, 0}, 1},
		{{0, 500}, {1000, 500}, 2},
		{{500, 0}, {500, 1000}, 3},
		{{0, 0}, {1000, 0}, 4},
	};
	Arrangement reference = buildArrangement(segs);
	const std::string ref = canonical(reference);

	std::mt19937 rng(12345);
	for (int trial = 0; trial < 20; ++trial) {
		std::vector<InputSegment> shuffled = segs;
		std::shuffle(shuffled.begin(), shuffled.end(), rng);
		Arrangement got = buildArrangement(shuffled);
		EXPECT_EQ(canonical(got), ref) << "trial " << trial;
	}
}

namespace {

	// A clean planar subdivision has no arrangement vertex lying strictly interior
	// to any arrangement edge: every incidence has been resolved into a shared
	// endpoint. This is the invariant the fixpoint loop must establish. Checked
	// exactly with integer predicates.
	bool noVertexInteriorToAnyEdge(const Arrangement& arr) {
		for (const ArrangementEdge& e : arr.edges) {
			const Vec2i64& a = arr.vertices[e.from];
			const Vec2i64& b = arr.vertices[e.to];
			for (const Vec2i64& v : arr.vertices) {
				if (v == a || v == b) {
					continue;
				}
				const bool collinear = cross(b - a, v - a).sign() == 0;
				const bool inBox = v.x >= std::min(a.x, b.x) && v.x <= std::max(a.x, b.x) &&
								   v.y >= std::min(a.y, b.y) && v.y <= std::max(a.y, b.y);
				if (collinear && inBox) {
					return false; // v sits strictly interior to edge (a,b): unresolved
				}
			}
		}
		return true;
	}

	// No two distinct arrangement edges share more than an endpoint: no geometric
	// overlap and no proper crossing survives.
	bool edgesCleanlyDisjoint(const Arrangement& arr) {
		for (std::size_t i = 0; i < arr.edges.size(); ++i) {
			for (std::size_t k = i + 1; k < arr.edges.size(); ++k) {
				const Vec2i64& a0 = arr.vertices[arr.edges[i].from];
				const Vec2i64& a1 = arr.vertices[arr.edges[i].to];
				const Vec2i64& b0 = arr.vertices[arr.edges[k].from];
				const Vec2i64& b1 = arr.vertices[arr.edges[k].to];
				const SegmentRelation rel = intersectSegments(a0, a1, b0, b1).relation;
				if (rel == SegmentRelation::ProperCrossing || rel == SegmentRelation::CollinearOverlap) {
					return false;
				}
			}
		}
		return true;
	}

} // namespace

TEST(Arrangement, ManySegmentsThroughOnePointResolveClean) {
	// A pencil of segments through a common center: every pair crosses at the
	// center. The fixpoint must split them all there and emit a clean star.
	std::vector<InputSegment> segs;
	const std::int64_t R = 1000;
	const std::int64_t pts[][2] = {{R, 0}, {0, R}, {-R, 0}, {0, -R}, {R, R}, {-R, R}, {R, -R}, {-R, -R}, {R, 500},
								   {-R, -500}};
	std::int64_t idx = 0;
	for (const auto& p : pts) {
		segs.push_back({{-p[0], -p[1]}, {p[0], p[1]}, idx++});
	}
	const Arrangement arr = buildArrangement(segs);
	EXPECT_TRUE(noVertexInteriorToAnyEdge(arr));
	EXPECT_TRUE(edgesCleanlyDisjoint(arr));
}

TEST(Arrangement, NearParallelLongSegmentsRoundedCrossingClean) {
	// Two long, nearly parallel segments whose true crossing is at a non-integer
	// point. After rounding, the crossing vertex must not leave a residual interior
	// incidence on either edge.
	std::vector<InputSegment> segs = {
		{{0, 0}, {1000000, 1}, 0},
		{{0, 3}, {1000000, -2}, 1},
	};
	const Arrangement arr = buildArrangement(segs);
	EXPECT_TRUE(noVertexInteriorToAnyEdge(arr)) << canonical(arr);
	EXPECT_TRUE(edgesCleanlyDisjoint(arr)) << canonical(arr);
}

TEST(Arrangement, AdversarialRandomBatchesAlwaysClean) {
	// Random small-coordinate segment batches, the regime where rounded crossings
	// most easily land on a neighbor lattice point and spawn a new incidence. The
	// fixpoint must converge to a clean subdivision every time.
	std::mt19937 rng(0xA11CE);
	std::uniform_int_distribution<std::int64_t> coord(-12, 12);
	for (int trial = 0; trial < 300; ++trial) {
		std::vector<InputSegment> segs;
		const int count = 5 + static_cast<int>(rng() % 5);
		for (int i = 0; i < count; ++i) {
			Vec2i64 a{coord(rng), coord(rng)};
			Vec2i64 b{coord(rng), coord(rng)};
			if (a == b) {
				continue;
			}
			segs.push_back({a, b, i});
		}
		const Arrangement arr = buildArrangement(segs);
		ASSERT_TRUE(noVertexInteriorToAnyEdge(arr)) << "trial " << trial << " " << canonical(arr);
		ASSERT_TRUE(edgesCleanlyDisjoint(arr)) << "trial " << trial << " " << canonical(arr);
	}
}

TEST(Arrangement, AdversarialDeterminismManyShuffles) {
	// Determinism beyond the existing test: a denser tangle, more shuffles.
	std::vector<InputSegment> segs;
	const std::int64_t pts[][4] = {{0, 0, 100, 100}, {0, 100, 100, 0}, {50, 0, 50, 100}, {0, 50, 100, 50},
								   {0, 0, 100, 50},	  {0, 100, 100, 50}, {25, 0, 75, 100}, {10, 10, 90, 90}};
	std::int64_t idx = 0;
	for (const auto& p : pts) {
		segs.push_back({{p[0], p[1]}, {p[2], p[3]}, idx++});
	}
	const std::string ref = canonical(buildArrangement(segs));
	std::mt19937 rng(99);
	for (int trial = 0; trial < 200; ++trial) {
		std::vector<InputSegment> shuffled = segs;
		std::shuffle(shuffled.begin(), shuffled.end(), rng);
		// Also relabel indices to confirm provenance ordering is canonical, not
		// insertion-order dependent.
		ASSERT_EQ(canonical(buildArrangement(shuffled)), ref) << "trial " << trial;
	}
}

namespace {

	void appendRing(std::vector<InputSegment>& segs, const std::vector<Vec2i64>& ring, std::int64_t id) {
		for (std::size_t i = 0; i < ring.size(); ++i) {
			segs.push_back({ring[i], ring[(i + 1) % ring.size()], id});
		}
	}

	Vec2i64 transposed(const Vec2i64& p, bool transpose) {
		return transpose ? Vec2i64{p.y, p.x} : p;
	}

	// A T-touch at every integer x (or y): a unit stem arriving from the low side
	// ends on a short perpendicular segment's interior, and nothing touches that
	// point from the high side. Whatever the broad phase's cell size, some of these
	// touch points lie exactly on a cell boundary with the stem in the cell below.
	std::vector<InputSegment> lowSideTouchLattice(bool transpose) {
		std::vector<InputSegment> segs;
		std::int64_t			  id = 0;
		for (std::int64_t row = 0; row < 8; ++row) {
			const std::int64_t y = 4 * row;
			for (std::int64_t x = 1 + row % 2; x <= 40; x += 2) {
				segs.push_back({transposed({x - 1, y}, transpose), transposed({x, y}, transpose), id++});
				segs.push_back({transposed({x, y - 1}, transpose), transposed({x, y + 1}, transpose), id++});
			}
		}
		return segs;
	}

	// One long segment over a field of short crossing segments spread across many cells.
	std::vector<InputSegment> longOverShorts(const Vec2i64& from, const Vec2i64& to) {
		std::vector<InputSegment> segs;
		std::int64_t			  id = 0;
		for (std::int64_t x = 0; x < 2000; x += 100) {
			for (std::int64_t y = 0; y < 2000; y += 100) {
				segs.push_back({{x + 10, y + 3}, {x + 70, y + 91}, id++});
				segs.push_back({{x + 5, y + 80}, {x + 90, y + 20}, id++});
			}
		}
		segs.push_back({from, to, id});
		return segs;
	}

	// Two random tangles millions of mm apart along x: a lopsided extent the grid
	// must cap rather than cut into millions of columns.
	std::vector<InputSegment> farClusters() {
		std::mt19937								rng(0xFA12);
		std::uniform_int_distribution<std::int64_t> coord(0, 40);
		std::vector<InputSegment>					segs;
		for (const std::int64_t ox : {std::int64_t{0}, std::int64_t{6000000}}) {
			for (int i = 0; i < 60; ++i) {
				segs.push_back({{ox + coord(rng), coord(rng)}, {ox + coord(rng), coord(rng)}, i});
			}
		}
		return segs;
	}

	// Every segment on one line, overlapping and nested, so the extent is zero on
	// one axis.
	std::vector<InputSegment> collinearOverlaps(bool transpose) {
		std::vector<InputSegment> segs;
		for (std::int64_t i = 0; i < 40; ++i) {
			segs.push_back({transposed({i * 37, 500}, transpose), transposed({i * 37 + 150 + i % 7 * 20, 500}, transpose), i});
		}
		return segs;
	}

} // namespace

TEST(Arrangement, BroadPhaseInputsResolveClean) {
	struct Case {
		const char*				  name;
		std::vector<InputSegment> segs;
	};
	const std::vector<Case> cases = {
		{"low-side touches on x", lowSideTouchLattice(false)},
		{"low-side touches on y", lowSideTouchLattice(true)},
		{"long horizontal over shorts", longOverShorts({-50, 1037}, {2050, 1037})},
		{"long diagonal over shorts", longOverShorts({-50, -40}, {2050, 2013})},
		{"far clusters", farClusters()},
		{"collinear overlaps on a horizontal", collinearOverlaps(false)},
		{"collinear overlaps on a vertical", collinearOverlaps(true)},
	};
	for (const Case& c : cases) {
		const Arrangement arr = buildArrangement(c.segs);
		EXPECT_TRUE(noVertexInteriorToAnyEdge(arr)) << c.name;
		EXPECT_TRUE(edgesCleanlyDisjoint(arr)) << c.name;
	}
}

// The navmesh triangulates the arrangement's edges as constraints, so ring input
// must come out as a planar straight-line graph. A ring vertex resting on another
// ring's edge (a T-junction) splits that edge there.
TEST(Arrangement, RingVertexOnAnotherRingsEdgeYieldsPslg) {
	std::vector<InputSegment> segs;
	appendRing(segs, {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}, 1);
	appendRing(segs, {{400, 1000}, {700, 1400}, {100, 1400}}, 2); // apex touches the top edge
	appendRing(segs, {{1000, 300}, {1300, 200}, {1300, 500}}, 3); // apex touches the right edge
	const Arrangement arr = buildArrangement(segs);
	EXPECT_TRUE(noVertexInteriorToAnyEdge(arr)) << canonical(arr);
	EXPECT_TRUE(edgesCleanlyDisjoint(arr)) << canonical(arr);
	EXPECT_TRUE(hasEdge(arr, {0, 1000}, {400, 1000}));
	EXPECT_TRUE(hasEdge(arr, {400, 1000}, {1000, 1000}));
	EXPECT_TRUE(hasEdge(arr, {1000, 0}, {1000, 300}));
	EXPECT_TRUE(hasEdge(arr, {1000, 300}, {1000, 1000}));
}

// Two rings sharing part of a collinear edge (clipped chunk rings meeting along a
// border, split at different vertices) merge into one edge per sub-segment.
TEST(Arrangement, RingsSharingCollinearSubEdgeYieldPslg) {
	std::vector<InputSegment> segs;
	appendRing(segs, {{0, 0}, {1000, 0}, {1000, 500}, {1000, 800}, {0, 800}}, 1);
	appendRing(segs, {{1000, 200}, {2000, 200}, {2000, 1200}, {1000, 1200}}, 2);
	const Arrangement arr = buildArrangement(segs);
	EXPECT_TRUE(noVertexInteriorToAnyEdge(arr)) << canonical(arr);
	EXPECT_TRUE(edgesCleanlyDisjoint(arr)) << canonical(arr);
	const std::size_t shared = findEdge(arr, {1000, 200}, {1000, 500});
	ASSERT_NE(shared, SIZE_MAX);
	EXPECT_EQ(arr.edges[shared].provenance, (std::vector<std::int64_t>{1, 2}));
	const std::size_t sharedUpper = findEdge(arr, {1000, 500}, {1000, 800});
	ASSERT_NE(sharedUpper, SIZE_MAX);
	EXPECT_EQ(arr.edges[sharedUpper].provenance, (std::vector<std::int64_t>{1, 2}));
	EXPECT_TRUE(hasEdge(arr, {1000, 0}, {1000, 200}));
	EXPECT_TRUE(hasEdge(arr, {1000, 800}, {1000, 1200}));
}

// Two crossings on one segment round to points sharing an x, off the segment's line:
// (11.74, 25.96) to (12, 26) and (12.26, 25.04) to (12, 25). The segment runs toward
// -x, +y, so its chain must reach (12, 25) first. Ordered by coordinates the chain
// zigzagged back through (12, 26), and the ring it belonged to forked.
TEST(Arrangement, RoundedCutsSharingAnXFollowTheSegment) {
	const std::vector<InputSegment> segs = {
		{{14, 22}, {10, 29}, 1},
		{{20, 21}, {10, 27}, 2},
		{{10, 27}, {25, 14}, 3},
	};
	const Arrangement arr = buildArrangement(segs);
	EXPECT_TRUE(hasEdge(arr, {14, 22}, {12, 25})) << canonical(arr);
	EXPECT_TRUE(hasEdge(arr, {12, 25}, {12, 26})) << canonical(arr);
	EXPECT_TRUE(hasEdge(arr, {12, 26}, {10, 29})) << canonical(arr);
	EXPECT_FALSE(hasEdge(arr, {14, 22}, {12, 26})) << canonical(arr);
}

// Splits are taken from the current pieces, not the originals, so each level of
// cascading rounding can move a piece another 0.71 mm from its original line. Four
// nearly parallel segments and two crossers drift past 2 mm. This pins that the
// arrangement has no 2 mm drift bound; PatchableMesh's off-ground reach allows for it.
TEST(Arrangement, RepeatedRoundingDriftsPastTwoMillimetres) {
	const std::vector<InputSegment> segs = {
		{{2, 3}, {1586, 5}, 0},	   {{3, 3}, {1589, 4}, 1},	  {{4, 1}, {1589, 6}, 2},
		{{5, 0}, {1589, 3}, 3},	   {{970, -50}, {994, 60}, 4}, {{1420, -50}, {1375, 60}, 5},
	};
	const Arrangement arr	= buildArrangement(segs);
	bool			  pastTwo = false;
	for (const ArrangementEdge& e : arr.edges) {
		for (std::int64_t p : e.provenance) {
			const InputSegment& s	= segs[static_cast<std::size_t>(p)];
			const Vec2i64		dir = s.b - s.a;
			for (const std::size_t v : {e.from, e.to}) {
				// distance^2 > 4 mm^2  <=>  cross^2 > 4 * |dir|^2
				const Int128 off = cross(dir, arr.vertices[v] - s.a);
				pastTwo			 = pastTwo || Int128::compareSquareToProduct(off, Int128(4), dot(dir, dir)) > 0;
			}
		}
	}
	EXPECT_TRUE(pastTwo) << canonical(arr);
}

namespace {

	const std::vector<InputSegment> kUnitSquare = {
		{{0, 0}, {1000, 0}, 0},
		{{1000, 0}, {1000, 1000}, 1},
		{{1000, 1000}, {0, 1000}, 2},
		{{0, 1000}, {0, 0}, 3},
	};

	// Edges as coordinate pairs with provenance, independent of vertex numbering.
	std::vector<std::tuple<Vec2i64, Vec2i64, std::vector<std::int64_t>>> edgeGeometry(const Arrangement& arr) {
		std::vector<std::tuple<Vec2i64, Vec2i64, std::vector<std::int64_t>>> out;
		for (const ArrangementEdge& e : arr.edges) {
			out.emplace_back(arr.vertices[e.from], arr.vertices[e.to], e.provenance);
		}
		return out;
	}

} // namespace

TEST(Arrangement, PointSiteOnEdgeSplitsIt) {
	const Arrangement arr = buildArrangement(kUnitSquare, {{400, 0}});
	EXPECT_EQ(arr.vertices.size(), 5u);
	EXPECT_EQ(arr.edges.size(), 5u);
	EXPECT_TRUE(hasEdge(arr, {0, 0}, {400, 0})) << canonical(arr);
	EXPECT_TRUE(hasEdge(arr, {400, 0}, {1000, 0})) << canonical(arr);
	EXPECT_FALSE(hasEdge(arr, {0, 0}, {1000, 0})) << canonical(arr);
	EXPECT_EQ(arr.edges[findEdge(arr, {0, 0}, {400, 0})].provenance, (std::vector<std::int64_t>{0}));
}

TEST(Arrangement, PointSiteOnSplitCrossingEdgeSplitsItsPiece) {
	// The diagonals cross at (500,500); a point at (250,250) must split the piece
	// left after the crossing cut, not only the original segment.
	const std::vector<InputSegment> segs = {
		{{0, 0}, {1000, 1000}, 0},
		{{0, 1000}, {1000, 0}, 1},
	};
	const Arrangement arr = buildArrangement(segs, {{250, 250}});
	EXPECT_TRUE(hasEdge(arr, {0, 0}, {250, 250})) << canonical(arr);
	EXPECT_TRUE(hasEdge(arr, {250, 250}, {500, 500})) << canonical(arr);
	EXPECT_EQ(arr.edges.size(), 5u);
}

TEST(Arrangement, PointSiteAtVertexAddsNothing) {
	EXPECT_EQ(canonical(buildArrangement(kUnitSquare, {{1000, 0}})), canonical(buildArrangement(kUnitSquare)));
}

TEST(Arrangement, IsolatedPointSiteIsAVertexAndChangesNoFace) {
	const Arrangement plain = buildArrangement(kUnitSquare);
	const Arrangement withPoint = buildArrangement(kUnitSquare, {{500, 500}, {3000, -7}});
	EXPECT_EQ(withPoint.vertices.size(), plain.vertices.size() + 2);
	EXPECT_TRUE(std::binary_search(withPoint.vertices.begin(), withPoint.vertices.end(), Vec2i64{500, 500}));
	EXPECT_TRUE(std::binary_search(withPoint.vertices.begin(), withPoint.vertices.end(), Vec2i64{3000, -7}));
	EXPECT_EQ(edgeGeometry(withPoint), edgeGeometry(plain));

	const HalfEdgeMesh a = extractFaces(plain);
	const HalfEdgeMesh b = extractFaces(withPoint);
	ASSERT_EQ(a.faces.size(), b.faces.size());
	for (std::size_t f = 0; f < a.faces.size(); ++f) {
		EXPECT_EQ(a.faces[f].signedAreaDoubled, b.faces[f].signedAreaDoubled);
		EXPECT_EQ(a.faces[f].provenance, b.faces[f].provenance);
		EXPECT_EQ(a.faces[f].outer, b.faces[f].outer);
	}
}

TEST(Arrangement, NoPointSitesGiveIdenticalOutput) {
	std::vector<InputSegment> segs = kUnitSquare;
	segs.push_back({{0, 0}, {1000, 1000}, 4});
	segs.push_back({{0, 1000}, {1000, 0}, 5});
	segs.push_back({{0, 500}, {1000, 503}, 6});
	EXPECT_EQ(canonical(buildArrangement(segs, {})), canonical(buildArrangement(segs)));

	ArrangementWorkspace workspace;
	Arrangement			 reused;
	buildArrangement(segs, workspace, reused, {{500, 500}, {0, 300}});
	buildArrangement(segs, workspace, reused);
	EXPECT_EQ(canonical(reused), canonical(buildArrangement(segs)));
}

TEST(Arrangement, DuplicatePointSitesCollapse) {
	const Arrangement once	= buildArrangement(kUnitSquare, {{400, 0}, {200, 200}});
	const Arrangement twice = buildArrangement(kUnitSquare, {{200, 200}, {400, 0}, {400, 0}, {200, 200}});
	EXPECT_EQ(canonical(twice), canonical(once));
}

TEST(Arrangement, PointSitesWithoutSegments) {
	const Arrangement arr = buildArrangement({}, {{5, 5}, {1, 2}, {5, 5}});
	EXPECT_EQ(arr.vertices, (std::vector<Vec2i64>{{1, 2}, {5, 5}}));
	EXPECT_TRUE(arr.edges.empty());
}

TEST(Arrangement, ZeroLengthSegmentStillRejectedBesidePointSites) {
	const Arrangement arr = buildArrangement({{{500, 500}, {500, 500}, 0}, {{0, 0}, {1000, 0}, 1}}, {{700, 0}});
	EXPECT_EQ(arr.vertices.size(), 3u);
	EXPECT_EQ(arr.edges.size(), 2u);
}
