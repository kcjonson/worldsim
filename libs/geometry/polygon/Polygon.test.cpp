#include "Polygon.h"
#include "../core/Vec2i64.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// CCW square, side 1000 mm (1 m).
	Ring squareCcw() {
		return {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	}

	Ring squareCw() {
		return {{0, 0}, {0, 1000}, {1000, 1000}, {1000, 0}};
	}

} // namespace

TEST(Area, SignByWinding) {
	EXPECT_EQ(signedAreaDoubled(squareCcw()).sign(), 1);
	EXPECT_EQ(signedAreaDoubled(squareCw()).sign(), -1);
}

TEST(Area, MagnitudeSquareMeters) {
	// 1 m square = 1 m^2.
	EXPECT_NEAR(signedAreaSquareMeters(squareCcw()), 1.0, 1e-9);
	EXPECT_NEAR(signedAreaSquareMeters(squareCw()), -1.0, 1e-9);
}

TEST(Area, DegenerateTooFewVertices) {
	Ring two = {{0, 0}, {1000, 0}};
	EXPECT_EQ(signedAreaDoubled(two).sign(), 0);
	EXPECT_EQ(windingOrder(two), Winding::Degenerate);
}

TEST(Winding, EnsureCounterClockwise) {
	Ring r = squareCw();
	ensureCounterClockwise(r);
	EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);

	Ring already = squareCcw();
	Ring copy	 = already;
	ensureCounterClockwise(already);
	EXPECT_EQ(already, copy); // unchanged
}

TEST(IsSimple, ConvexPasses) {
	EXPECT_TRUE(isSimple(squareCcw()).pass);
}

TEST(IsSimple, ConcavePasses) {
	Ring chevron = {{0, 0}, {1000, 500}, {0, 1000}, {300, 500}};
	EXPECT_TRUE(isSimple(chevron).pass);
}

TEST(IsSimple, BowtieFails) {
	// Classic self-intersecting bowtie.
	Ring bowtie = {{0, 0}, {1000, 1000}, {1000, 0}, {0, 1000}};
	auto r		= isSimple(bowtie);
	EXPECT_FALSE(r.pass);
}

TEST(IsSimple, DuplicateVertexFails) {
	Ring dup = {{0, 0}, {1000, 0}, {1000, 0}, {0, 1000}};
	auto r	 = isSimple(dup);
	EXPECT_FALSE(r.pass);
	EXPECT_EQ(r.vertexIndex, 1u);
	EXPECT_EQ(r.otherIndex, 2u);
}

TEST(IsSimple, SpikeFails) {
	// A spike: an edge that doubles back through a non-adjacent edge.
	Ring spike = {{0, 0}, {1000, 0}, {1000, 1000}, {500, 1000}, {500, -500}, {0, 1000}};
	auto r	   = isSimple(spike);
	EXPECT_FALSE(r.pass);
}

// isSimple buckets edges on a grid; it must report exactly the pair an all-pairs
// scan in index order reports, on shoreline-sized rings with long and short edges.
TEST(IsSimple, MatchesAllPairsScanOnLargeRings) {
	auto allPairs = [](const Ring& ring) -> std::pair<std::size_t, std::size_t> {
		const std::size_t n = ring.size();
		for (std::size_t i = 0; i < n; ++i) {
			for (std::size_t k = i + 1; k < n; ++k) {
				if (k == i + 1 || (i == 0 && k == n - 1)) {
					continue;
				}
				if (intersectSegments(ring[i], ring[(i + 1) % n], ring[k], ring[(k + 1) % n]).relation != SegmentRelation::Disjoint) {
					return {i, k};
				}
			}
		}
		return {n, n};
	};

	// Three quarters of a wobbly 600-vertex circle closed by one ~30 m chord, then
	// copies with a fold spliced in at several places.
	Ring base;
	for (int i = 0; i < 600; ++i) {
		if (i > 0 && i < 150) {
			continue;
		}
		const double a	 = 6.283185307179586 * i / 600.0;
		const double rad = 20000.0 + 1500.0 * std::sin(7.0 * a) + 600.0 * std::cos(19.0 * a);
		base.push_back({std::llround(rad * std::cos(a)), std::llround(rad * std::sin(a))});
	}
	EXPECT_TRUE(isSimple(base).pass);

	for (const std::size_t at : {std::size_t{1}, std::size_t{5}, std::size_t{250}, std::size_t{449}}) {
		Ring folded = base;
		// Two vertices that reach across to the far side of the ring.
		folded.insert(folded.begin() + static_cast<std::ptrdiff_t>(at), {{-25000, 3000}, {-25000, -3000}});
		const auto expected = allPairs(folded);
		ASSERT_LT(expected.first, folded.size());
		const ConstraintResult r = isSimple(folded);
		EXPECT_FALSE(r.pass);
		EXPECT_EQ(r.vertexIndex, expected.first) << "fold at " << at;
		EXPECT_EQ(r.otherIndex, expected.second) << "fold at " << at;
	}
}

TEST(MinInteriorAngle, SquareNinetyDegrees) {
	auto r = minInteriorAngle(squareCcw(), 30.0);
	EXPECT_TRUE(r.pass);
	EXPECT_NEAR(r.measuredValue, 90.0, 1e-6);
}

TEST(MinInteriorAngle, SharpSpikeFails) {
	// A very thin triangle has one tiny apex angle.
	Ring thin = {{0, 0}, {1000, 0}, {500, 10}};
	auto r	  = minInteriorAngle(thin, 30.0);
	EXPECT_FALSE(r.pass);
	EXPECT_LT(r.measuredValue, 30.0);
}

TEST(MinInteriorAngle, BoundaryAroundThreshold) {
	// Right isoceles triangle: two 45 degree base angles are the minimum.
	Ring tri = {{0, 0}, {1000, 0}, {0, 1000}};
	EXPECT_NEAR(minInteriorAngle(tri, 0.0).measuredValue, 45.0, 1e-6);
	EXPECT_TRUE(minInteriorAngle(tri, 44.9).pass);
	EXPECT_FALSE(minInteriorAngle(tri, 45.1).pass);
}

TEST(MinInteriorAngle, DegenerateZeroLengthEdge) {
	Ring dup = {{0, 0}, {0, 0}, {1000, 0}};
	auto r	 = minInteriorAngle(dup, 30.0);
	EXPECT_FALSE(r.pass);
	EXPECT_EQ(r.measuredValue, 0.0);
}

TEST(MinVertexSpacing, Passes) {
	auto r = minVertexSpacing(squareCcw(), 500);
	EXPECT_TRUE(r.pass);
}

TEST(MinVertexSpacing, FailsTooClose) {
	Ring r = {{0, 0}, {100, 0}, {100, 1000}, {0, 1000}};
	auto c = minVertexSpacing(r, 500); // edge 0->1 is only 100 mm
	EXPECT_FALSE(c.pass);
	EXPECT_EQ(c.vertexIndex, 0u);
	EXPECT_EQ(c.otherIndex, 1u);
	EXPECT_NEAR(c.measuredValue, 0.1, 1e-6); // 100 mm = 0.1 m
}

TEST(MinVertexSpacing, BoundaryExactlyAtThreshold) {
	// Spacing exactly 1000 mm with threshold 1000 passes (>= via squared compare).
	Ring r = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	EXPECT_TRUE(minVertexSpacing(r, 1000).pass);
	EXPECT_FALSE(minVertexSpacing(r, 1001).pass);
}

TEST(MinEdgeClearance, WideSquarePasses) {
	// Opposite edges of a 1 m square are 1000 mm apart.
	auto r = minEdgeClearance(squareCcw(), 500);
	EXPECT_TRUE(r.pass);
}

TEST(MinEdgeClearance, ThinSlotFails) {
	// A C-shape with two nearly-touching parallel edges (a sliver slot).
	Ring slot = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}, {0, 600}, {900, 600}, {900, 400}, {0, 400}};
	auto r	  = minEdgeClearance(slot, 250); // slot gap is 200 mm
	EXPECT_FALSE(r.pass);
	EXPECT_LT(r.measuredValue, 0.25);
}

TEST(MinEdgeClearance, BoundaryAtThreshold) {
	// Opposite edges 1000 mm apart: threshold 1000 passes, 1001 fails.
	EXPECT_TRUE(minEdgeClearance(squareCcw(), 1000).pass);
	EXPECT_FALSE(minEdgeClearance(squareCcw(), 1001).pass);
}

TEST(PointInPolygonForward, Works) {
	EXPECT_EQ(pointInPolygon(Vec2i64{500, 500}, squareCcw()), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon(Vec2i64{2000, 500}, squareCcw()), PointInPolygon::Outside);
}
