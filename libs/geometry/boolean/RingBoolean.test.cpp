#include "RingBoolean.h"
#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Coordinates are in millimeters; 1000 mm = 1 m keeps the golden math in whole
	// meters. Rings are CCW with the closing edge implicit.

	Ring square(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	}

	std::int64_t area2(const Ring& r) {
		return static_cast<std::int64_t>(signedAreaDoubled(r).toDouble());
	}

	// Rotate a ring so it starts at its lexicographically smallest vertex, for
	// order-independent golden comparison. Assumes CCW input (preserves winding).
	Ring canonical(Ring r) {
		if (r.empty()) {
			return r;
		}
		std::size_t minIdx = 0;
		for (std::size_t i = 1; i < r.size(); ++i) {
			if (r[i] < r[minIdx]) {
				minIdx = i;
			}
		}
		std::rotate(r.begin(), r.begin() + static_cast<std::ptrdiff_t>(minIdx), r.end());
		return r;
	}

	bool sameRing(const Ring& got, const Ring& want) {
		return canonical(got) == canonical(want);
	}

} // namespace

// ---------------------------------------------------------------------------
// Union: shared-edge cases first (the normal Add case, the riskiest code).
// ---------------------------------------------------------------------------

TEST(UnionRings, FullSharedEdgeMergesToRectangleCollinearMidpointsDropped) {
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(1000, 0, 2000, 1000);

	const BooleanResult r = unionRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);

	// The shared edge is gone and the collinear midpoints at (1000,0)/(1000,1000)
	// are removed by simplification: a clean 4-vertex 2x1 rectangle.
	EXPECT_EQ(r.ring.size(), 4u);
	EXPECT_TRUE(sameRing(r.ring, square(0, 0, 2000, 1000)));
	EXPECT_EQ(area2(r.ring), 4'000'000);
	EXPECT_TRUE(isSimple(r.ring).pass);
}

TEST(UnionRings, PartialSharedEdgeGivesEightVertexStaircase) {
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(1000, 500, 2000, 1500); // shares x=1000, y in [500,1000]

	const BooleanResult r = unionRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);

	const Ring expected = {
		{0, 0}, {1000, 0}, {1000, 500}, {2000, 500}, {2000, 1500}, {1000, 1500}, {1000, 1000}, {0, 1000},
	};
	EXPECT_EQ(r.ring.size(), 8u);
	EXPECT_TRUE(sameRing(r.ring, expected));
	EXPECT_EQ(area2(r.ring), 4'000'000); // two disjoint-area squares
	EXPECT_TRUE(isSimple(r.ring).pass);
}

TEST(UnionRings, SharedVertexOnlyIsPinchRejection) {
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(1000, 1000, 2000, 2000); // touch only at corner (1000,1000)

	const BooleanResult r = unionRings(a, b);
	EXPECT_EQ(r.status, BooleanStatus::PinchVertex);
}

TEST(UnionRings, ProperOverlapGivesLShapeWithExpectedArea) {
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(500, 500, 1500, 1500);

	const BooleanResult r = unionRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);

	// 1,000,000 + 1,000,000 - 250,000 overlap = 1,750,000 -> doubled 3,500,000.
	EXPECT_EQ(area2(r.ring), 3'500'000);
	EXPECT_EQ(r.ring.size(), 8u); // L-shape
	EXPECT_TRUE(isSimple(r.ring).pass);
}

TEST(UnionRings, IdenticalRingsReturnThatRing) {
	const Ring a = square(0, 0, 1000, 1000);

	const BooleanResult r = unionRings(a, a);
	ASSERT_EQ(r.status, BooleanStatus::Ok);
	EXPECT_TRUE(sameRing(r.ring, a));
	EXPECT_EQ(area2(r.ring), 2'000'000);
}

TEST(UnionRings, BInsideAReturnsA) {
	const Ring a = square(0, 0, 2000, 2000);
	const Ring b = square(500, 500, 1500, 1500);

	const BooleanResult r = unionRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);
	EXPECT_TRUE(sameRing(r.ring, a));
	EXPECT_EQ(area2(r.ring), 8'000'000);
}

TEST(UnionRings, DisjointRejected) {
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(2000, 2000, 3000, 3000);

	const BooleanResult r = unionRings(a, b);
	EXPECT_EQ(r.status, BooleanStatus::Disjoint);
}

TEST(UnionRings, ConcaveUPlusBarClosingItRejectedAsHole) {
	// A U-shape opening upward, plus a bar across the top forming an enclosed
	// void. Union encloses a hole -> rejected.
	const Ring u = {
		{0, 0}, {3000, 0}, {3000, 3000}, {2000, 3000}, {2000, 1000}, {1000, 1000}, {1000, 3000}, {0, 3000},
	};
	const Ring bar = square(0, 3000, 3000, 4000); // caps the opening at the top

	const BooleanResult r = unionRings(u, bar);
	EXPECT_EQ(r.status, BooleanStatus::ResultHasHole);
}

TEST(UnionRings, NearCollinearCrossingSliverCollapsesButStaysSimple) {
	// B is a thin slab overlapping A's top edge; its bottom edge runs at a very
	// shallow angle (30 mm rise over 60,000 mm) just inside A, so the two side
	// edges cross A's top edge and the bottom-edge crossings round to the grid.
	// Simplification must drop the rounded slivers and leave a simple ring with
	// area matching the true union (A plus the 60,000 x 1,000 bump above it).
	const Ring a = square(0, 0, 100000, 100000);
	const Ring b = {{20000, 99000}, {80000, 99030}, {80000, 101000}, {20000, 101000}};

	const BooleanResult r = unionRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);
	EXPECT_TRUE(isSimple(r.ring).pass);

	// True union: 100000^2 + 60000 x 1000 bump = 10,060,000,000. Allow a small
	// rounding band for the collapsed near-collinear sliver.
	const std::int64_t got	= area2(r.ring) / 2;
	const std::int64_t want = 10'060'000'000;
	EXPECT_LT(std::llabs(got - want), 100'000);
}

TEST(UnionRings, InvalidInputRejected) {
	const Ring degenerate = {{0, 0}, {1000, 0}};	  // fewer than 3 vertices
	const Ring a		  = square(0, 0, 1000, 1000);
	EXPECT_EQ(unionRings(degenerate, a).status, BooleanStatus::InvalidInput);

	const Ring selfIntersecting = {{0, 0}, {1000, 1000}, {1000, 0}, {0, 1000}}; // bowtie
	EXPECT_EQ(unionRings(selfIntersecting, a).status, BooleanStatus::InvalidInput);
}

// ---------------------------------------------------------------------------
// Subtract.
// ---------------------------------------------------------------------------

TEST(SubtractRings, NotchFromEdge) {
	// Carve a 400x400 notch out of the top edge of a 1000x1000 square.
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(300, 700, 700, 1400); // crosses the top edge y=1000

	const BooleanResult r = subtractRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);
	EXPECT_TRUE(isSimple(r.ring).pass);

	const Ring expected = {
		{0, 0}, {1000, 0}, {1000, 1000}, {700, 1000}, {700, 700}, {300, 700}, {300, 1000}, {0, 1000},
	};
	EXPECT_TRUE(sameRing(r.ring, expected));
	// 1,000,000 - 400x300 notch = 1,000,000 - 120,000 = 880,000.
	EXPECT_EQ(area2(r.ring), 1'760'000);
}

TEST(SubtractRings, CornerBite) {
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(700, 700, 1400, 1400); // bites the top-right corner

	const BooleanResult r = subtractRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);

	const Ring expected = {
		{0, 0}, {1000, 0}, {1000, 700}, {700, 700}, {700, 1000}, {0, 1000},
	};
	EXPECT_TRUE(sameRing(r.ring, expected));
	// 1,000,000 - 300x300 corner = 910,000.
	EXPECT_EQ(area2(r.ring), 1'820'000);
}

TEST(SubtractRings, BStrictlyInsideRejectedAsHole) {
	const Ring a = square(0, 0, 2000, 2000);
	const Ring b = square(500, 500, 1500, 1500);

	const BooleanResult r = subtractRings(a, b);
	EXPECT_EQ(r.status, BooleanStatus::ResultHasHole);
}

TEST(SubtractRings, BCoveringARejectedAsConsumed) {
	const Ring a = square(500, 500, 1500, 1500);
	const Ring b = square(0, 0, 2000, 2000);

	const BooleanResult r = subtractRings(a, b);
	EXPECT_EQ(r.status, BooleanStatus::ConsumesInput);
}

TEST(SubtractRings, DumbbellCutSplitsRejected) {
	// A wide-short bar; b is a tall slab through its middle, cutting it in two.
	const Ring a = square(0, 0, 3000, 1000);
	const Ring b = square(1000, -500, 2000, 1500);

	const BooleanResult r = subtractRings(a, b);
	EXPECT_EQ(r.status, BooleanStatus::ResultSplits);
}

TEST(SubtractRings, SharedEdgeCutIn) {
	// b shares the left edge of a (x=0) and cuts a rectangular bite inward.
	const Ring a = square(0, 0, 2000, 2000);
	const Ring b = square(0, 500, 800, 1500); // left edge coincident with a's

	const BooleanResult r = subtractRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);
	EXPECT_TRUE(isSimple(r.ring).pass);

	const Ring expected = {
		{0, 0}, {2000, 0}, {2000, 2000}, {0, 2000}, {0, 1500}, {800, 1500}, {800, 500}, {0, 500},
	};
	EXPECT_TRUE(sameRing(r.ring, expected));
	// 4,000,000 - 800x1000 = 4,000,000 - 800,000 = 3,200,000.
	EXPECT_EQ(area2(r.ring), 6'400'000);
}

TEST(SubtractRings, DisjointNoEffect) {
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(2000, 2000, 3000, 3000);

	const BooleanResult r = subtractRings(a, b);
	EXPECT_EQ(r.status, BooleanStatus::NoEffect);
}

TEST(SubtractRings, EdgeAdjacentNoEffect) {
	// b shares only an edge with a (no interior overlap): nothing is removed.
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(1000, 0, 2000, 1000);

	const BooleanResult r = subtractRings(a, b);
	EXPECT_EQ(r.status, BooleanStatus::NoEffect);
}

TEST(SubtractRings, SubMmSliverIsConsumedNotReturned) {
	// b removes all of a above y=1, leaving a remainder 1 mm tall. A 1 mm-tall
	// region has no strictly-interior lattice point, so face extraction produces
	// no kept face: the sliver collapses below grid resolution and the subtract
	// reports ConsumesInput rather than returning a degenerate hairline ring.
	// This is the deliberate sub-mm behavior (reject-don't-repair, integer grid).
	const Ring a = square(0, 0, 100000, 100000);
	const Ring b = square(0, 1, 100000, 200000);

	const BooleanResult r = subtractRings(a, b);
	EXPECT_EQ(r.status, BooleanStatus::ConsumesInput);
}

TEST(SubtractRings, ThinButResolvableStripSurvives) {
	// The contrast to the sub-mm case: a 100 mm-tall remainder is thick enough to
	// contain interior lattice points, so it survives as a valid simple ring.
	const Ring a = square(0, 0, 100000, 100000);
	const Ring b = square(0, 100, 100000, 200000); // removes everything above y=100

	const BooleanResult r = subtractRings(a, b);
	ASSERT_EQ(r.status, BooleanStatus::Ok);
	EXPECT_TRUE(isSimple(r.ring).pass);
	// Remaining strip: 100000 wide x 100 tall = 10,000,000 -> doubled 20,000,000.
	EXPECT_EQ(area2(r.ring), 20'000'000);
}

TEST(SubtractRings, InvalidInputRejected) {
	const Ring a		  = square(0, 0, 1000, 1000);
	const Ring degenerate = {{0, 0}, {1000, 0}};
	EXPECT_EQ(subtractRings(a, degenerate).status, BooleanStatus::InvalidInput);
}

// ---------------------------------------------------------------------------
// ringsInteriorOverlap.
// ---------------------------------------------------------------------------

TEST(RingsInteriorOverlap, OverlappingTrue) {
	EXPECT_TRUE(ringsInteriorOverlap(square(0, 0, 1000, 1000), square(500, 500, 1500, 1500)));
}

TEST(RingsInteriorOverlap, EdgeAdjacentFalse) {
	EXPECT_FALSE(ringsInteriorOverlap(square(0, 0, 1000, 1000), square(1000, 0, 2000, 1000)));
}

TEST(RingsInteriorOverlap, VertexAdjacentFalse) {
	EXPECT_FALSE(ringsInteriorOverlap(square(0, 0, 1000, 1000), square(1000, 1000, 2000, 2000)));
}

TEST(RingsInteriorOverlap, ContainmentTrue) {
	EXPECT_TRUE(ringsInteriorOverlap(square(0, 0, 2000, 2000), square(500, 500, 1500, 1500)));
}

TEST(RingsInteriorOverlap, DisjointFalse) {
	EXPECT_FALSE(ringsInteriorOverlap(square(0, 0, 1000, 1000), square(2000, 2000, 3000, 3000)));
}

TEST(RingsInteriorOverlap, PartialSharedEdgeNoInteriorFalse) {
	// Squares sharing only part of an edge: still no shared interior.
	EXPECT_FALSE(ringsInteriorOverlap(square(0, 0, 1000, 1000), square(1000, 500, 2000, 1500)));
}

// ---------------------------------------------------------------------------
// Property checks: area conservation and simplicity over assorted inputs.
// ---------------------------------------------------------------------------

TEST(BooleanProperties, UnionAreaEqualsSumMinusIntersectionAxisAligned) {
	const Ring a = square(0, 0, 1000, 1000);
	const Ring b = square(400, 400, 1400, 1400);

	const BooleanResult u = unionRings(a, b);
	ASSERT_EQ(u.status, BooleanStatus::Ok);
	// Overlap is 600x600 = 360,000. Union = 2,000,000 - 360,000 = 1,640,000.
	EXPECT_EQ(area2(u.ring), 3'280'000);
	EXPECT_TRUE(isSimple(u.ring).pass);
}

TEST(BooleanProperties, SubtractAreaEqualsAMinusIntersection) {
	const Ring a = square(0, 0, 2000, 1000);
	const Ring b = square(1500, -500, 2500, 500); // bites the bottom-right corner

	const BooleanResult d = subtractRings(a, b);
	ASSERT_EQ(d.status, BooleanStatus::Ok);
	// a area 2,000,000; overlap 500x500 = 250,000; remainder 1,750,000.
	EXPECT_EQ(area2(d.ring), 3'500'000);
	EXPECT_TRUE(isSimple(d.ring).pass);
}

// ---------------------------------------------------------------------------
// Adversarial cases: bridge shapes, two-stretch shared edges, status reachability.
// ---------------------------------------------------------------------------

TEST(UnionRings, BridgeAcrossSlotEnclosesHoleRejected) {
	// A is a U with tall prongs (slot interior x in [20,40], y in [20,60]). B is a
	// bar spanning the slot at y in [40,50], sharing a vertical edge stretch on each
	// prong's inner wall (x=20 and x=40, two disjoint shared stretches). The union
	// caps the slot, enclosing the region below the bar as a void: must reject.
	Ring a = {{0, 0}, {60, 0}, {60, 60}, {40, 60}, {40, 20}, {20, 20}, {20, 60}, {0, 60}};
	Ring b = {{20, 40}, {40, 40}, {40, 50}, {20, 50}};
	const BooleanResult r = unionRings(a, b);
	EXPECT_FALSE(r.ok());
	EXPECT_EQ(r.status, BooleanStatus::ResultHasHole) << "status=" << static_cast<int>(r.status);
}

TEST(UnionRings, TwoDisjointEdgeStretchesNoHoleMerges) {
	// Control: B fills the entire slot mouth (x in [20,40], y in [20,60]), sharing
	// the same two vertical stretches but enclosing nothing. The union is a solid
	// rectangle 60x60 and must succeed.
	Ring a = {{0, 0}, {60, 0}, {60, 60}, {40, 60}, {40, 20}, {20, 20}, {20, 60}, {0, 60}};
	Ring b = square(20, 20, 40, 60);
	const BooleanResult r = unionRings(a, b);
	ASSERT_TRUE(r.ok()) << "status=" << static_cast<int>(r.status);
	EXPECT_EQ(area2(r.ring), 60 * 60 * 2);
}

TEST(SubtractRings, BoundaryRunsAlongThenCutsIn) {
	// B's boundary runs ALONG a's bottom edge for a stretch, then cuts up into a's
	// interior and back out: a bite whose mouth is a sub-stretch of a's edge. The
	// remainder stays a single simple ring.
	Ring a = square(0, 0, 100, 100);
	Ring b = {{20, 0}, {60, 0}, {60, 40}, {20, 40}}; // sits on a's bottom edge y=0
	const BooleanResult r = subtractRings(a, b);
	ASSERT_TRUE(r.ok()) << "status=" << static_cast<int>(r.status);
	EXPECT_EQ(area2(r.ring), (100 * 100 - 40 * 40) * 2);
	EXPECT_TRUE(isSimple(r.ring).pass);
}

TEST(RingsInteriorOverlap, RingEntersThroughSharedEdge) {
	// B shares a stretch of a's edge but pokes its interior across into a: the
	// interiors DO overlap, so this must be true (it is not the legal edge-adjacent
	// case). B straddles a's right edge x=100.
	Ring a = square(0, 0, 100, 100);
	Ring b = {{80, 20}, {120, 20}, {120, 60}, {80, 60}};
	EXPECT_TRUE(ringsInteriorOverlap(a, b));
}

TEST(RingsInteriorOverlap, SharedEdgeStretchOnlyNoInterior) {
	// B sits entirely outside a, flush against a's right edge along a sub-stretch.
	// Edge contact only: no interior overlap.
	Ring a = square(0, 0, 100, 100);
	Ring b = {{100, 20}, {160, 20}, {160, 60}, {100, 60}};
	EXPECT_FALSE(ringsInteriorOverlap(a, b));
}

TEST(SubtractRings, SplitWhenBSpansFullWidthThroughMiddle) {
	// B cuts clean across a from one edge to the opposite edge, sharing edge
	// stretches on both, severing a into top and bottom: ResultSplits.
	Ring a = square(0, 0, 100, 100);
	Ring b = square(0, 40, 100, 60); // spans the full width, touching both side edges
	const BooleanResult r = subtractRings(a, b);
	EXPECT_FALSE(r.ok());
	EXPECT_EQ(r.status, BooleanStatus::ResultSplits) << "status=" << static_cast<int>(r.status);
}
