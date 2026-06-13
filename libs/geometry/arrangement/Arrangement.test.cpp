#include "Arrangement.h"
#include "../core/Vec2i64.h"

#include <algorithm>
#include <cstdint>
#include <random>
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
