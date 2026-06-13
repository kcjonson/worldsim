#include "WallOffset.h"
#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Sum of |signedAreaDoubled| over a set of rings, as a double (mm^2 * 2).
	double totalAreaDoubled(const std::vector<Ring>& rings) {
		double acc = 0.0;
		for (const Ring& r : rings) {
			acc += std::abs(signedAreaDoubled(r).toDouble());
		}
		return acc;
	}

	bool ringContains(const Ring& ring, const Vec2i64& p) {
		return std::find(ring.begin(), ring.end(), p) != ring.end();
	}

	// Every ring is simple, CCW, non-zero area.
	void expectAllRingsValid(const WallBands& wb) {
		for (const Ring& r : wb.bands) {
			EXPECT_GE(r.size(), 3u);
			EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
			EXPECT_TRUE(isSimple(r).pass);
		}
		for (const Ring& r : wb.junctions) {
			EXPECT_GE(r.size(), 3u);
			EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
			EXPECT_TRUE(isSimple(r).pass);
		}
	}

	// True when the open interiors of a and b are disjoint: no vertex of one lies
	// strictly inside the other, and no edges properly cross. Edge-to-edge touching
	// (shared boundary) is allowed; that is exactly how tiled pieces meet.
	bool interiorsDisjoint(const Ring& a, const Ring& b) {
		for (const Vec2i64& p : a) {
			if (pointInPolygon(p, b) == PointInPolygon::Inside) {
				return false;
			}
		}
		for (const Vec2i64& p : b) {
			if (pointInPolygon(p, a) == PointInPolygon::Inside) {
				return false;
			}
		}
		// Centroid-of-edge sampling catches overlap an all-vertices-outside case
		// would miss (one ring fully crossing the other without a vertex inside).
		const std::size_t na = a.size();
		for (std::size_t i = 0; i < na; ++i) {
			const Vec2i64& p = a[i];
			const Vec2i64& q = a[(i + 1) % na];
			const Vec2i64  mid{(p.x + q.x) / 2, (p.y + q.y) / 2};
			if (pointInPolygon(mid, b) == PointInPolygon::Inside) {
				return false;
			}
		}
		const std::size_t nb = b.size();
		for (std::size_t i = 0; i < nb; ++i) {
			const Vec2i64& p = b[i];
			const Vec2i64& q = b[(i + 1) % nb];
			const Vec2i64  mid{(p.x + q.x) / 2, (p.y + q.y) / 2};
			if (pointInPolygon(mid, a) == PointInPolygon::Inside) {
				return false;
			}
		}
		return true;
	}

	void expectPairwiseDisjoint(const WallBands& wb) {
		std::vector<Ring> all = wb.bands;
		all.insert(all.end(), wb.junctions.begin(), wb.junctions.end());
		for (std::size_t i = 0; i < all.size(); ++i) {
			for (std::size_t k = i + 1; k < all.size(); ++k) {
				EXPECT_TRUE(interiorsDisjoint(all[i], all[k])) << "rings " << i << " and " << k << " overlap";
			}
		}
	}

} // namespace

// --- band() ------------------------------------------------------------------

TEST(Band, AxisAlignedExactGolden) {
	// Horizontal centerline (0,0)->(1000,0), half 100. Offsets are exactly
	// integer: left (0,+100), right (0,-100). CCW ring is the rectangle.
	Ring r = band({0, 0}, {1000, 0}, 100);
	EXPECT_EQ(r.size(), 4u);
	EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
	// Area = 1000 mm * 200 mm = 200000 mm^2; doubled = 400000.
	EXPECT_EQ(signedAreaDoubled(r).sign(), 1);
	EXPECT_NEAR(std::abs(signedAreaDoubled(r).toDouble()), 400000.0, 0.5);
	EXPECT_TRUE(ringContains(r, Vec2i64{0, 100}));
	EXPECT_TRUE(ringContains(r, Vec2i64{1000, 100}));
	EXPECT_TRUE(ringContains(r, Vec2i64{1000, -100}));
	EXPECT_TRUE(ringContains(r, Vec2i64{0, -100}));
}

TEST(Band, VerticalIsCcw) {
	Ring r = band({0, 0}, {0, 1000}, 200);
	EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
	EXPECT_TRUE(isSimple(r).pass);
}

TEST(Band, DiagonalRoundsButStaysValid) {
	// 45-degree centerline: perpendicular offset is 100/sqrt2 ~ 70.7, rounds to
	// 71. Band must still be simple, CCW, near the expected area.
	Ring r = band({0, 0}, {1000, 1000}, 100);
	EXPECT_EQ(windingOrder(r), Winding::CounterClockwise);
	EXPECT_TRUE(isSimple(r).pass);
	// Expected area: length sqrt(2)*1000 * width 200 = 282842.7 mm^2; doubled
	// ~565685. Allow rounding slack from the 71 vs 70.7 offset.
	EXPECT_NEAR(std::abs(signedAreaDoubled(r).toDouble()), 565685.0, 4000.0);
}

// --- trimmedBand() -----------------------------------------------------------

TEST(TrimmedBand, CutsBothEnds) {
	Ring r;
	ASSERT_TRUE(trimmedBand({0, 0}, {1000, 0}, 100, 100, 200, r));
	// Remaining centerline 100..800 -> length 700, width 200, area 140000,
	// doubled 280000.
	EXPECT_NEAR(std::abs(signedAreaDoubled(r).toDouble()), 280000.0, 0.5);
	EXPECT_TRUE(ringContains(r, Vec2i64{100, 100}));
	EXPECT_TRUE(ringContains(r, Vec2i64{800, -100}));
}

TEST(TrimmedBand, ZeroTrimEqualsBand) {
	Ring t;
	ASSERT_TRUE(trimmedBand({0, 0}, {1000, 0}, 100, 0, 0, t));
	Ring b = band({0, 0}, {1000, 0}, 100);
	ensureCounterClockwise(b);
	// Same area; vertex sets coincide.
	EXPECT_NEAR(std::abs(signedAreaDoubled(t).toDouble()), std::abs(signedAreaDoubled(b).toDouble()), 0.5);
}

TEST(TrimmedBand, OverrunRejected) {
	Ring r;
	EXPECT_FALSE(trimmedBand({0, 0}, {1000, 0}, 100, 600, 600, r));
	EXPECT_FALSE(trimmedBand({0, 0}, {1000, 0}, 100, 500, 500, r)); // exactly meets
}

TEST(TrimmedBand, ZeroLengthRejected) {
	Ring r;
	EXPECT_FALSE(trimmedBand({0, 0}, {0, 0}, 100, 0, 0, r));
}

// --- simplifyRing() ----------------------------------------------------------

TEST(Simplify, DropsCollinearVertex) {
	Ring r = {{0, 0}, {500, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	simplifyRing(r, kDefaultSimplifyEpsMm);
	EXPECT_EQ(r.size(), 4u);
	EXPECT_FALSE(ringContains(r, Vec2i64{500, 0}));
}

TEST(Simplify, DropsSubMmSliver) {
	// Vertex (500,1) sits 1 mm off the straight edge (0,0)-(1000,0): a sliver.
	Ring r = {{0, 0}, {500, 1}, {1000, 0}, {1000, 1000}, {0, 1000}};
	simplifyRing(r, 1);
	EXPECT_EQ(r.size(), 4u);
}

TEST(Simplify, KeepsRealCorners) {
	Ring r = {{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}};
	simplifyRing(r, kDefaultSimplifyEpsMm);
	EXPECT_EQ(r.size(), 4u);
}

// --- straight continuation (degree-2, 180 deg) -------------------------------

TEST(Junction, StraightContinuationNoPolygonNoTrim) {
	// Two colinear segments sharing (1000,0): (0,0)->(1000,0) and
	// (1000,0)->(2000,0). They continue straight; bands should abut, no junction
	// polygon, no trim.
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 0}, 100},
		{{1000, 0}, {2000, 0}, 100},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	EXPECT_EQ(wb.status, OffsetStatus::Ok);
	EXPECT_TRUE(wb.junctions.empty());
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);
	// Each band keeps its full length: total doubled area = 2 * 400000.
	EXPECT_NEAR(totalAreaDoubled(wb.bands), 800000.0, 1.0);
}

// --- right-angle corner (golden) ---------------------------------------------

TEST(Junction, RightAngleCornerGolden) {
	// L-corner at origin: (1000,0)->(0,0) and (0,0)->(0,1000), both half 100.
	// Centerlines meet at right angle. The inner miter apex is (100,100), the
	// outer apex is (-100,-100) wait: with both half 100 the band corners are at
	// +-100. Assert structural tiling and that the junction polygon welds to the
	// trimmed band corners.
	std::vector<WallSegment> segs = {
		{{1000, 0}, {0, 0}, 100},
		{{0, 0}, {0, 1000}, 100},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.bands.size(), 2u);
	ASSERT_EQ(wb.junctions.size(), 1u);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);

	// Mitered L of two 200-thick walls meeting at a right angle. The inner
	// 100x100 corner square is shared by both bands; trimming removes it from the
	// bands and the junction polygon adds it back. The outer corner is filled by
	// the miter (a 100x100 square outside both naive rectangles). Inner removal
	// and outer fill are equal and opposite, so the tiled total equals the naive
	// sum: 2*(1000*200) = 400000 mm^2, doubled 800000. (This is the defining
	// property of a miter join: it neither gains nor loses area versus the naive
	// rectangles, it just relocates the corner so the join reads as one wall.)
	const double total = totalAreaDoubled(wb.bands) + totalAreaDoubled(wb.junctions);
	EXPECT_NEAR(total, 800000.0, 2000.0);
}

// --- 45 and 30 degree corners (miter) ----------------------------------------

TEST(Junction, FortyFiveDegreeCornerTiles) {
	// (1000,0)->(0,0) then (0,0)->(1000,1000): 45-degree bend.
	std::vector<WallSegment> segs = {
		{{1000, 0}, {0, 0}, 100},
		{{0, 0}, {1000, 1000}, 100},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);
	EXPECT_EQ(wb.junctions.size(), 1u);
}

TEST(Junction, ThirtyishDegreeCornerTiles) {
	// Sharp bend near the 30-degree minimum. dir1 along -x, dir2 at ~30 deg from
	// the continuation. Use (1000,0)->(0,0) and (0,0)->(866,500) (30 deg above
	// +x, so ~150 deg interior bend from the incoming -x... still > 30 deg
	// between outgoing dirs). Miter within the limit.
	std::vector<WallSegment> segs = {
		{{1000, 0}, {0, 0}, 100},
		{{0, 0}, {866, 500}, 100},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);
}

TEST(Junction, SharpCornerBevelsBeyondLimit) {
	// A sharp but domain-valid corner: outgoing directions ~35 deg apart (above
	// the 30 deg minimum). The acute-wedge miter reaches ~3.3x half-thickness; a
	// tight miter limit of 2.0 forces the squared-off bevel fallback (D2).
	std::vector<WallSegment> segs = {
		{{1000, 0}, {0, 0}, 100},		// outgoing +x
		{{0, 0}, {819, 574}, 100},		// outgoing ~35 deg above +x
	};
	WallBands wb = resolveWallBands(segs, 2.0);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);
	EXPECT_EQ(wb.junctions.size(), 1u);
}

// --- T-junctions -------------------------------------------------------------

TEST(Junction, TJunctionSameThicknessGolden) {
	// Three segments meeting at origin: along +x, along -x, along +y. A T.
	// Stem up the +y, crossbar along x. All half 100.
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 0}, 100},
		{{0, 0}, {-1000, 0}, 100},
		{{0, 0}, {0, 1000}, 100},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.bands.size(), 3u);
	ASSERT_EQ(wb.junctions.size(), 1u);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);
}

TEST(Junction, TJunctionMixedThickness) {
	// Thick crossbar (half 200), thin stem (half 100).
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 0}, 200},
		{{0, 0}, {-1000, 0}, 200},
		{{0, 0}, {0, 1000}, 100},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);
}

// --- 4-way junction ----------------------------------------------------------

TEST(Junction, FourWayCross) {
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 0}, 100},
		{{0, 0}, {-1000, 0}, 100},
		{{0, 0}, {0, 1000}, 100},
		{{0, 0}, {0, -1000}, 100},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.junctions.size(), 1u);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);
}

// --- free ends ---------------------------------------------------------------

TEST(Junction, FreeEndsGetFlatCaps) {
	// A single isolated segment: both ends are degree-1 free ends, no junction
	// polygon, full-length band.
	std::vector<WallSegment> segs = {{{0, 0}, {1000, 0}, 100}};
	WallBands				 wb	  = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	EXPECT_TRUE(wb.junctions.empty());
	EXPECT_NEAR(totalAreaDoubled(wb.bands), 400000.0, 1.0);
}

// --- closed square room (area conservation) ----------------------------------

TEST(Junction, ClosedSquareRoomAreaConserved) {
	// 4 walls forming a 1m x 1m room, centerlines on the square (0,0)-(1000,0)-
	// (1000,1000)-(0,1000), all half 100. Four right-angle corners. The total
	// tiled area must equal the true wall-band union.
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 0}, 100},
		{{1000, 0}, {1000, 1000}, 100},
		{{1000, 1000}, {0, 1000}, 100},
		{{0, 1000}, {0, 0}, 100},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.bands.size(), 4u);
	ASSERT_EQ(wb.junctions.size(), 4u);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);

	// True union area: outer square is 1200x1200 = 1,440,000; inner hole is
	// 800x800 = 640,000; wall band union = 1,440,000 - 640,000 = 800,000 mm^2,
	// doubled 1,600,000. Tiled pieces must sum to this (no double-counted corners,
	// no gaps). Allow per-corner rounding slack.
	const double total = totalAreaDoubled(wb.bands) + totalAreaDoubled(wb.junctions);
	EXPECT_NEAR(total, 1600000.0, 4000.0);
}

// --- thickness extremes ------------------------------------------------------

TEST(Junction, ThinAndThickExtremes) {
	std::vector<WallSegment> thin = {
		{{1000, 0}, {0, 0}, 100},
		{{0, 0}, {0, 1000}, 100},
	};
	std::vector<WallSegment> thick = {
		{{2000, 0}, {0, 0}, 500},
		{{0, 0}, {0, 2000}, 500},
	};
	WallBands a = resolveWallBands(thin, kDefaultMiterLimit);
	WallBands b = resolveWallBands(thick, kDefaultMiterLimit);
	EXPECT_EQ(a.status, OffsetStatus::Ok);
	EXPECT_EQ(b.status, OffsetStatus::Ok);
	expectAllRingsValid(a);
	expectAllRingsValid(b);
	expectPairwiseDisjoint(a);
	expectPairwiseDisjoint(b);
}

// --- rounding robustness -----------------------------------------------------

TEST(Junction, NonIntegerPerpendicularAnglesRobust) {
	// A run of segments at angles that produce non-integer perpendicular offsets
	// (17-degree-ish bends). Must resolve without degeneracy.
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 300}, 120},
		{{1000, 300}, {1700, 1100}, 120},
		{{1700, 1100}, {2000, 2200}, 120},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	expectAllRingsValid(wb);
	expectPairwiseDisjoint(wb);
	EXPECT_EQ(wb.junctions.size(), 2u);
}

// --- rejection (reject-don't-repair) -----------------------------------------

TEST(Validation, ZeroLengthSegmentRejected) {
	std::vector<WallSegment> segs = {{{0, 0}, {0, 0}, 100}};
	WallBands				 wb	  = resolveWallBands(segs, kDefaultMiterLimit);
	EXPECT_EQ(wb.status, OffsetStatus::ZeroLengthSegment);
}

TEST(Validation, ResolveJunctionDirect) {
	// Direct degree-2 junction call: right-angle, both half 100.
	std::vector<IncidentSegment> incidents = {
		{{1000, 0}, 100, 0},
		{{0, 1000}, 100, 1},
	};
	JunctionResolution jr = resolveJunction({0, 0}, incidents, kDefaultMiterLimit);
	EXPECT_EQ(jr.status, OffsetStatus::Ok);
	ASSERT_EQ(jr.trims.size(), 2u);
	// Both segments trim back by the inner-corner reach (100 mm for a square
	// right-angle corner with equal thickness).
	EXPECT_GT(jr.trims[0].trimMm, 0);
	EXPECT_GT(jr.trims[1].trimMm, 0);
}

// --- adversarial tiling / trim ----------------------------------------------

namespace {

	// Strict exact pairwise non-overlap: no edge of one ring properly crosses an
	// edge of the other, and no vertex of one is strictly inside the other. This is
	// stronger than the midpoint-sampling helper; it catches any interior overlap.
	bool exactlyDisjointInteriors(const Ring& a, const Ring& b) {
		const std::size_t na = a.size();
		const std::size_t nb = b.size();
		for (std::size_t i = 0; i < na; ++i) {
			for (std::size_t k = 0; k < nb; ++k) {
				const SegmentRelation rel =
					intersectSegments(a[i], a[(i + 1) % na], b[k], b[(k + 1) % nb]).relation;
				if (rel == SegmentRelation::ProperCrossing) {
					return false;
				}
			}
		}
		for (const Vec2i64& p : a) {
			if (pointInPolygon(p, b) == PointInPolygon::Inside) {
				return false;
			}
		}
		for (const Vec2i64& p : b) {
			if (pointInPolygon(p, a) == PointInPolygon::Inside) {
				return false;
			}
		}
		return true;
	}

	void expectExactPairwiseDisjoint(const WallBands& wb) {
		std::vector<Ring> all = wb.bands;
		all.insert(all.end(), wb.junctions.begin(), wb.junctions.end());
		for (std::size_t i = 0; i < all.size(); ++i) {
			for (std::size_t k = i + 1; k < all.size(); ++k) {
				EXPECT_TRUE(exactlyDisjointInteriors(all[i], all[k]))
					<< "rings " << i << " and " << k << " interiors overlap";
			}
		}
	}

} // namespace

TEST(Junction, ClosedTriangleRoomTilesWithoutOverlap) {
	// A closed triangle room with one acute (~25 deg) corner, sharper than the
	// square test's right angles. Three degree-2 junctions, half 60. Must tile with
	// no overlap and conserve area (within per-corner rounding slack).
	std::vector<WallSegment> segs = {
		{{0, 0}, {2000, 0}, 60},
		{{2000, 0}, {400, 300}, 60},
		{{400, 300}, {0, 0}, 60},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok) << "status=" << static_cast<int>(wb.status);
	ASSERT_EQ(wb.bands.size(), 3u);
	EXPECT_EQ(wb.junctions.size(), 3u);
	expectAllRingsValid(wb);
	expectExactPairwiseDisjoint(wb);
}

TEST(Junction, ShortMiddleSegmentBetweenSharpCornersRejectsOverrun) {
	// A short middle segment whose two endpoints are sharp corners. The miter trims
	// demanded at each end exceed the segment length, so the band would invert:
	// resolveWallBands must reject with TrimOverrunsSegment, never emit overlapping
	// or inverted rings.
	std::vector<WallSegment> segs = {
		{{-2000, 1000}, {0, 0}, 300}, // arrives at left corner steeply
		{{0, 0}, {100, 0}, 300},	  // SHORT middle segment, length 100
		{{100, 0}, {2100, 1000}, 300},// leaves right corner steeply
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	EXPECT_EQ(wb.status, OffsetStatus::TrimOverrunsSegment) << "status=" << static_cast<int>(wb.status);
}

TEST(Junction, FourWayCrossExactNoOverlap) {
	// Denser exact-overlap check on a 4-way cross with mixed thickness.
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 0}, 120},
		{{0, 0}, {-1000, 0}, 80},
		{{0, 0}, {0, 1000}, 120},
		{{0, 0}, {0, -1000}, 80},
	};
	WallBands wb = resolveWallBands(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok) << "status=" << static_cast<int>(wb.status);
	expectAllRingsValid(wb);
	expectExactPairwiseDisjoint(wb);
}
