#include "WallOffset.h"
#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <random>
#include <string>
#include <tuple>
#include <utility>
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

TEST(Simplify, PinnedVerticesSurvive) {
	// (500,0) is collinear and (1500,1) a sliver; pinned, both stay.
	Ring					  r		 = {{0, 0}, {500, 0}, {1000, 0}, {1500, 1}, {2000, 0}, {2000, 1000}, {0, 1000}};
	std::vector<std::uint8_t> pinned = {0, 1, 0, 1, 0, 0, 0};
	simplifyRing(r, 10, pinned);
	const Ring expected = {{0, 0}, {500, 0}, {1500, 1}, {2000, 0}, {2000, 1000}, {0, 1000}};
	EXPECT_EQ(r, expected);
	const std::vector<std::uint8_t> expectedPinned = {0, 1, 1, 0, 0, 0};
	EXPECT_EQ(pinned, expectedPinned);
}

TEST(Simplify, AllUnpinnedMatchesUnpinnedPass) {
	Ring a = {{0, 0}, {300, 40}, {700, -30}, {1000, 0}, {1400, 700}, {1000, 1000}, {500, 1020}, {0, 1000}, {-20, 500}};
	Ring b = a;
	std::vector<std::uint8_t> pinned(b.size(), 0);
	simplifyRing(a, 50);
	simplifyRing(b, 50, pinned);
	EXPECT_EQ(a, b);
	EXPECT_EQ(pinned.size(), b.size());
}

// A 350 m radius shore resampled at 250 mm: removing vertices one at a time
// against their current neighbors lets the error ratchet up to meters along a
// gentle curve. Every original vertex must stay within epsilon of the result.
TEST(Simplify, ErrorStaysWithinEpsilonOnGentleCurves) {
	constexpr double	   kRadius = 350000.0;
	constexpr std::int64_t kEps	   = 100;
	Ring				   circle;
	const int			   count = static_cast<int>(6.283185307179586 * kRadius / 250.0);
	for (int i = 0; i < count; ++i) {
		const double a = 6.283185307179586 * i / count;
		circle.push_back({std::llround(kRadius * std::cos(a)), std::llround(kRadius * std::sin(a))});
	}
	Ring simplified = circle;
	simplifyRing(simplified, kEps);

	// A chord with a 100 mm sagitta on this radius is ~16.7 m, so the fewest
	// vertices is ~130; Douglas-Peucker's splits land within about twice that.
	EXPECT_GT(simplified.size(), 100U);
	EXPECT_LT(simplified.size(), 300U);
	for (const Vec2i64& v : circle) {
		bool near = false;
		for (std::size_t i = 0; i < simplified.size() && !near; ++i) {
			near = withinDistanceOfSegment(v, simplified[i], simplified[(i + 1) % simplified.size()], kEps);
		}
		EXPECT_TRUE(near) << v.x << ", " << v.y;
	}
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
	// Centerlines meet at a right angle. With both half 100 the band edges sit
	// at +-100, so the inner miter apex is (100,100) and the outer apex is
	// (-100,-100). Assert structural tiling and that the junction polygon welds
	// to the trimmed band corners.
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

	// The junction reports the segments incident to it (both, at this corner), aligned
	// with `junctions`, so a renderer can style each junction from its own segments.
	ASSERT_EQ(wb.junctionSegments.size(), wb.junctions.size());
	std::vector<std::size_t> incident = wb.junctionSegments[0];
	std::sort(incident.begin(), incident.end());
	EXPECT_EQ(incident, (std::vector<std::size_t>{0u, 1u}));

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

// --- determinism (exact angular ordering, no atan2/double comparators) -------

TEST(Junction, IncidentOrderPermutationInvariant) {
	// Resolving the same junction with its incident segments supplied in every
	// permutation must yield byte-identical trims and polygon. atan2/double
	// ordering could reorder nearly-collinear directions platform-dependently;
	// the exact comparator with an index tiebreak makes the result canonical.
	// Use a 4-way junction of distinct directions and thicknesses.
	std::vector<IncidentSegment> base = {
		{{1000, 0}, 120, 0},
		{{0, 1000}, 100, 1},
		{{-1000, 0}, 140, 2},
		{{0, -1000}, 80, 3},
	};

	const JunctionResolution reference = resolveJunction({0, 0}, base, kDefaultMiterLimit);
	ASSERT_EQ(reference.status, OffsetStatus::Ok);

	std::vector<std::size_t> perm = {0, 1, 2, 3};
	std::sort(perm.begin(), perm.end());
	do {
		std::vector<IncidentSegment> shuffled;
		shuffled.reserve(base.size());
		for (std::size_t p : perm) {
			shuffled.push_back(base[p]);
		}
		const JunctionResolution jr = resolveJunction({0, 0}, shuffled, kDefaultMiterLimit);
		ASSERT_EQ(jr.status, OffsetStatus::Ok);

		// The polygon is geometry about the vertex; it must be identical
		// regardless of input order.
		EXPECT_EQ(jr.polygon, reference.polygon);

		// Trims are keyed by the caller index, so build an index->trim map for
		// each result and compare those (the trims vector order tracks input
		// order, the mapping does not).
		std::map<std::size_t, std::int64_t> refTrim;
		for (const JunctionTrim& t : reference.trims) {
			refTrim[t.index] = t.trimMm;
		}
		std::map<std::size_t, std::int64_t> gotTrim;
		for (const JunctionTrim& t : jr.trims) {
			gotTrim[t.index] = t.trimMm;
		}
		EXPECT_EQ(gotTrim, refTrim);
	} while (std::next_permutation(perm.begin(), perm.end()));
}

// --- mixed thickness through a straight or near-straight join ---------------

namespace {

	bool insideAnyRing(const std::vector<Ring>& rings, const Vec2i64& p) {
		for (const Ring& r : rings) {
			if (pointInPolygon(p, r) == PointInPolygon::Inside) {
				return true;
			}
		}
		return false;
	}

	// resolveWallBands and then wallPiecesAndOutline, the way the renderer calls
	// them.
	struct Resolved : WallBands {
		std::vector<std::vector<Ring>> pieces;
		std::vector<OutlineEdge>	   outline;
	};

	Resolved resolveDrawn(const std::vector<WallSegment>& segments, double miterLimit,
						  const std::vector<std::vector<WallGap>>& gaps = {}) {
		Resolved resolved;
		static_cast<WallBands&>(resolved) = resolveWallBands(segments, miterLimit);
		if (resolved.status == OffsetStatus::Ok) {
			WallPiecesAndOutline shape = wallPiecesAndOutline(segments, gaps, resolved);
			resolved.pieces			   = std::move(shape.pieces);
			resolved.outline		   = std::move(shape.outline);
		}
		return resolved;
	}

	std::vector<Ring> wallShape(const Resolved& wb) {
		std::vector<Ring> all;
		for (const std::vector<Ring>& pieces : wb.pieces) {
			all.insert(all.end(), pieces.begin(), pieces.end());
		}
		all.insert(all.end(), wb.junctions.begin(), wb.junctions.end());
		return all;
	}

	double outlineLength(const Resolved& wb) {
		double total = 0.0;
		for (const OutlineEdge& e : wb.outline) {
			total += std::hypot(static_cast<double>(e.to.x - e.from.x), static_cast<double>(e.to.y - e.from.y));
		}
		return total;
	}

	// Every outline edge is on the boundary of the joined shape with the wall on
	// its left: a few mm to the left of its middle is wall, to the right is not. A
	// seam between two pieces has wall on both sides and fails. Edges of a few mm
	// (where rounding left two rings' corners a mm apart) are too short to probe.
	void expectOutlineOnBoundary(const Resolved& wb) {
		const std::vector<Ring> shape = wallShape(wb);
		for (const OutlineEdge& e : wb.outline) {
			const double dx	 = static_cast<double>(e.to.x - e.from.x);
			const double dy	 = static_cast<double>(e.to.y - e.from.y);
			const double len = std::hypot(dx, dy);
			ASSERT_GT(len, 0.0);
			if (len < 10.0) {
				continue;
			}
			const double  midX = (static_cast<double>(e.from.x) + static_cast<double>(e.to.x)) * 0.5;
			const double  midY = (static_cast<double>(e.from.y) + static_cast<double>(e.to.y)) * 0.5;
			const Vec2i64 left{std::llround(midX - dy / len * 4.0), std::llround(midY + dx / len * 4.0)};
			const Vec2i64 right{std::llround(midX + dy / len * 4.0), std::llround(midY - dx / len * 4.0)};
			EXPECT_TRUE(insideAnyRing(shape, left) && !insideAnyRing(shape, right))
				<< "edge (" << e.from.x << "," << e.from.y << ")->(" << e.to.x << "," << e.to.y
				<< ") is a seam or has the wall on its right";
		}
	}

} // namespace

TEST(Junction, StraightMixedThicknessStepsWithoutSeam) {
	// Light (half 75) meets Heavy (half 150) head on. The bands abut with no
	// polygon and no trim; the outline steps out along the Heavy cap on both
	// sides and never crosses the joint.
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 0}, 75},
		{{1000, 0}, {2000, 0}, 150},
	};
	Resolved wb = resolveDrawn(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	EXPECT_TRUE(wb.junctions.empty());
	expectAllRingsValid(wb);
	expectExactPairwiseDisjoint(wb);
	EXPECT_NEAR(totalAreaDoubled(wb.bands), 2.0 * (1000.0 * 150.0 + 1000.0 * 300.0), 1.0);
	expectOutlineOnBoundary(wb);
	// Perimeter of the stepped shape: faces 4 * 1000, free caps 150 + 300, two
	// steps of 75.
	EXPECT_NEAR(outlineLength(wb), 4000.0 + 450.0 + 150.0, 1.0);
}

TEST(Junction, NearStraightMixedThicknessStaysLocal) {
	// A few degrees off straight, Light into Heavy. The facing faces would only
	// meet metres away behind the Heavy cap; the join must step at the cap
	// instead of trimming both bands back past their length (which rejected the
	// whole wall graph).
	std::vector<WallSegment> segs = {
		{{0, 0}, {1000, 0}, 75},
		{{1000, 0}, {2000, 50}, 150},
	};
	Resolved wb = resolveDrawn(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok) << "status=" << static_cast<int>(wb.status);
	expectAllRingsValid(wb);
	expectExactPairwiseDisjoint(wb);
	expectOutlineOnBoundary(wb);
	// Both bands keep nearly all their length: any cutback is a few mm.
	const double naive = 2.0 * (1000.0 * 150.0 + std::hypot(1000.0, 50.0) * 300.0);
	const double total = totalAreaDoubled(wb.bands) + totalAreaDoubled(wb.junctions);
	EXPECT_NEAR(total, naive, 0.01 * naive);
	EXPECT_NEAR(totalAreaDoubled(wb.bands), naive, 0.01 * naive);

	// The other way round (bending the other side) resolves the same.
	std::vector<WallSegment> mirrored = {
		{{0, 0}, {1000, 0}, 75},
		{{1000, 0}, {2000, -50}, 150},
	};
	Resolved other = resolveDrawn(mirrored, kDefaultMiterLimit);
	ASSERT_EQ(other.status, OffsetStatus::Ok);
	expectAllRingsValid(other);
	expectExactPairwiseDisjoint(other);
	expectOutlineOnBoundary(other);
}

TEST(Junction, ObtuseMixedThicknessTiles) {
	// 135 degree bend from Light into Heavy: an ordinary mitered corner, far
	// enough off straight that the faces meet near the vertex.
	std::vector<WallSegment> segs = {
		{{-1000, 0}, {0, 0}, 75},
		{{0, 0}, {707, 707}, 150},
	};
	Resolved wb = resolveDrawn(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.junctions.size(), 1u);
	expectAllRingsValid(wb);
	expectExactPairwiseDisjoint(wb);
	expectOutlineOnBoundary(wb);
}

// --- outline of the joined shape ---------------------------------------------

TEST(Outline, RightAngleCornerHasNoSeams) {
	// The L of RightAngleCornerGolden: the outline is the L's own perimeter, with
	// the outer corner stroked and no line across either band's end.
	std::vector<WallSegment> segs = {
		{{1000, 0}, {0, 0}, 100},
		{{0, 0}, {0, 1000}, 100},
	};
	Resolved wb = resolveDrawn(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	expectOutlineOnBoundary(wb);
	// 1100 + 1100 outer, 900 + 900 inner, two 200 free caps.
	EXPECT_NEAR(outlineLength(wb), 4400.0, 1.0);
}

TEST(Outline, TeeCrossAndRoomHaveNoSeams) {
	std::vector<WallSegment> tee = {
		{{0, 0}, {1000, 0}, 100},
		{{0, 0}, {-1000, 0}, 100},
		{{0, 0}, {0, 1000}, 100},
	};
	Resolved teeBands = resolveDrawn(tee, kDefaultMiterLimit);
	ASSERT_EQ(teeBands.status, OffsetStatus::Ok);
	expectOutlineOnBoundary(teeBands);
	// Flat side 2000, two inner runs of 900 each side of the stem, stem faces
	// 2 * 900, three free caps of 200.
	EXPECT_NEAR(outlineLength(teeBands), 2000.0 + 1800.0 + 1800.0 + 600.0, 1.0);

	std::vector<WallSegment> cross = {
		{{0, 0}, {1000, 0}, 120},
		{{0, 0}, {-1000, 0}, 80},
		{{0, 0}, {0, 1000}, 120},
		{{0, 0}, {0, -1000}, 80},
	};
	Resolved crossBands = resolveDrawn(cross, kDefaultMiterLimit);
	ASSERT_EQ(crossBands.status, OffsetStatus::Ok);
	expectOutlineOnBoundary(crossBands);

	std::vector<WallSegment> room = {
		{{0, 0}, {1000, 0}, 100},
		{{1000, 0}, {1000, 1000}, 100},
		{{1000, 1000}, {0, 1000}, 100},
		{{0, 1000}, {0, 0}, 100},
	};
	Resolved roomBands = resolveDrawn(room, kDefaultMiterLimit);
	ASSERT_EQ(roomBands.status, OffsetStatus::Ok);
	expectOutlineOnBoundary(roomBands);
	// Outer square 4 * 1200 plus inner square 4 * 800.
	EXPECT_NEAR(outlineLength(roomBands), 4800.0 + 3200.0, 1.0);
}

TEST(Outline, SharpAndMixedCornersHaveNoSeams) {
	std::vector<WallSegment> sharp = {
		{{1000, 0}, {0, 0}, 100},
		{{0, 0}, {819, 574}, 100},
	};
	Resolved sharpBands = resolveDrawn(sharp, kDefaultMiterLimit);
	ASSERT_EQ(sharpBands.status, OffsetStatus::Ok);
	expectOutlineOnBoundary(sharpBands);

	Resolved beveled = resolveDrawn(sharp, 2.0);
	ASSERT_EQ(beveled.status, OffsetStatus::Ok);
	expectOutlineOnBoundary(beveled);

	std::vector<WallSegment> mixedCorner = {
		{{1000, 0}, {0, 0}, 75},
		{{0, 0}, {0, 1000}, 150},
	};
	Resolved mixedBands = resolveDrawn(mixedCorner, kDefaultMiterLimit);
	ASSERT_EQ(mixedBands.status, OffsetStatus::Ok);
	expectAllRingsValid(mixedBands);
	expectExactPairwiseDisjoint(mixedBands);
	expectOutlineOnBoundary(mixedBands);
}

TEST(Outline, FreeSegmentIsItsRectangle) {
	std::vector<WallSegment> segs = {{{0, 0}, {1000, 0}, 100}};
	Resolved				 wb	  = resolveDrawn(segs, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.pieces.size(), 1u);
	ASSERT_EQ(wb.pieces[0].size(), 1u);
	EXPECT_EQ(wb.pieces[0][0], wb.bands[0]);
	EXPECT_EQ(wb.outline.size(), 4u);
	EXPECT_NEAR(outlineLength(wb), 2400.0, 1.0);
}

// --- gaps (doors and windows) ------------------------------------------------

TEST(Gaps, OpeningBesideCornerKeepsTheCornerTrim) {
	// A door gap 300..1200 mm along the first leg of an L, right beside the
	// corner at (1500,0). The piece between the gap and the corner keeps the
	// junction's cutback, so it meets the junction polygon instead of
	// overlapping it, and the gap's edges are outline.
	std::vector<WallSegment> segs = {{{0, 0}, {1500, 0}, 100}, {{1500, 0}, {1500, 1500}, 100}};
	Resolved				 wb	  = resolveDrawn(segs, kDefaultMiterLimit, {{{300, 1200}}, {}});
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.pieces[0].size(), 2u);
	ASSERT_EQ(wb.pieces[1].size(), 1u);
	ASSERT_EQ(wb.junctions.size(), 1u);

	std::vector<Ring> shape = wallShape(wb);
	for (std::size_t i = 0; i < shape.size(); ++i) {
		for (std::size_t k = i + 1; k < shape.size(); ++k) {
			EXPECT_TRUE(exactlyDisjointInteriors(shape[i], shape[k])) << "pieces " << i << " and " << k << " overlap";
		}
	}
	expectOutlineOnBoundary(wb);
	// Solid: 0..300 and 1200..1400 (trimmed 100 at the corner) on the first leg.
	EXPECT_NEAR(totalAreaDoubled(wb.pieces[0]), 2.0 * (300.0 + 200.0) * 200.0, 1.0);
	// Both gap edges are stroked: the L's perimeter less the gap's two faces,
	// plus its two 200 caps.
	EXPECT_NEAR(outlineLength(wb), (1600.0 + 1600.0 + 1400.0 + 1400.0 + 400.0) - 1800.0 + 400.0, 1.0);
}

TEST(Gaps, OverlappingAndOverhangingGapsMerge) {
	std::vector<WallSegment> segs = {{{0, 0}, {2000, 0}, 100}};
	Resolved				 wb	  = resolveDrawn(segs, kDefaultMiterLimit, {{{1500, 2500}, {-100, 200}, {150, 400}}});
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.pieces[0].size(), 1u);
	EXPECT_NEAR(totalAreaDoubled(wb.pieces[0]), 2.0 * 1100.0 * 200.0, 1.0);
	// The band itself stays whole for callers that ignore gaps.
	EXPECT_NEAR(totalAreaDoubled(wb.bands), 2.0 * 2000.0 * 200.0, 1.0);
}

TEST(Junction, NearParallelLargeCoordinateDenominatorExact) {
	// A real corner whose two outgoing directions are nearly collinear at large
	// magnitude with messy low bits. The line-line denominator is the cross
	// product of the two directions; the int64 component products here exceed
	// 2^53, so computing the denominator in double loses integer bits and the
	// near-cancellation leaves it off by ~100+ (verified: exact -5051857754 vs
	// double -5051857920). That corrupts the miter apex and, in the limit, can
	// round a true non-zero determinant to 0.0 and misclassify the corner as a
	// straight continuation. The exact Int128 cross keeps the determinant honest:
	// the corner resolves to a valid, non-rejected junction.
	std::vector<IncidentSegment> incidents = {
		{{3037000123LL, 2014857631LL}, 100, 0},
		{{3037000124LL, 2014857630LL}, 100, 1}, // 1 mm cross-wise over a ~3.6e9 run
	};
	const JunctionResolution jr = resolveJunction({0, 0}, incidents, kDefaultMiterLimit);
	EXPECT_EQ(jr.status, OffsetStatus::Ok);
	ASSERT_EQ(jr.trims.size(), 2u);
}

// --- properties over the legal input space -----------------------------------
//
// Seeded samples of the families a review swept: degree 2 from 30 to 330
// degrees, near-straight joins of mixed thickness, and degrees 3 to 6 with
// every corner at least 30 degrees, over half thicknesses 75, 100 and 150 and
// 2 m legs. Each check returns a description of the first thing wrong, so a
// sweep reports a count and a few examples instead of thousands of failures.

namespace {

	constexpr std::int64_t kHalves[] = {75, 100, 150};

	Vec2i64 polar(double degrees, double radius) {
		const double radians = degrees * 3.14159265358979323846 / 180.0;
		return {std::llround(radius * std::cos(radians)), std::llround(radius * std::sin(radians))};
	}

	struct Arm {
		double		 degrees = 0.0;
		std::int64_t half	 = 0;
		bool		 outward = true; // listed from the origin, else towards it
	};

	std::vector<WallSegment> star(const std::vector<Arm>& arms) {
		std::vector<WallSegment> segments;
		for (const Arm& arm : arms) {
			const Vec2i64 end = polar(arm.degrees, 2000.0);
			segments.push_back({arm.outward ? Vec2i64{0, 0} : end, arm.outward ? end : Vec2i64{0, 0}, arm.half});
		}
		return segments;
	}

	// One list of gaps per segment, or none at all.
	using Gaps = std::vector<std::vector<WallGap>>;

	const std::vector<WallGap>& gapsOf(const Gaps& gaps, std::size_t segment) {
		static const std::vector<WallGap> kNone;
		return gaps.empty() ? kNone : gaps[segment];
	}

	std::string describe(const std::vector<WallSegment>& segments, const Gaps& gaps) {
		std::string text;
		for (std::size_t i = 0; i < segments.size(); ++i) {
			const WallSegment& s = segments[i];
			text += "(" + std::to_string(s.a.x) + "," + std::to_string(s.a.y) + ")->(" + std::to_string(s.b.x) + "," +
					std::to_string(s.b.y) + ") h" + std::to_string(s.halfThicknessMm);
			for (const WallGap& gap : gapsOf(gaps, i)) {
				text += " gap " + std::to_string(gap.fromMm) + ".." + std::to_string(gap.toMm);
			}
			text += "; ";
		}
		return text;
	}

	std::string pointText(const Vec2i64& p) {
		return "(" + std::to_string(p.x) + "," + std::to_string(p.y) + ")";
	}

	double distanceBetween(const Vec2i64& a, const Vec2i64& b) {
		return std::hypot(static_cast<double>(b.x - a.x), static_cast<double>(b.y - a.y));
	}

	double distanceToRing(const Vec2i64& p, const Ring& ring) {
		double best = 1e18;
		for (std::size_t e = 0; e < ring.size(); ++e) {
			best = std::min(best, distanceToSegment(p, ring[e], ring[(e + 1) % ring.size()]));
		}
		return best;
	}

	// A point of one ring inside another by more than rounding. Two band corners
	// meeting at one miter apex each round about a millimetre from it, and where
	// they cross over, the polygon's edge between them can reach a little over 2
	// mm into a band.
	bool overlapsDeeply(const Ring& a, const Ring& b) {
		auto deepInside = [](const Vec2i64& p, const Ring& ring) {
			return pointInPolygon(p, ring) == PointInPolygon::Inside && distanceToRing(p, ring) > 2.5;
		};
		for (const auto& [from, into] : {std::pair{&a, &b}, std::pair{&b, &a}}) {
			for (std::size_t e = 0; e < from->size(); ++e) {
				const Vec2i64& p = (*from)[e];
				const Vec2i64& q = (*from)[(e + 1) % from->size()];
				if (deepInside(p, *into) || deepInside({(p.x + q.x) / 2, (p.y + q.y) / 2}, *into)) {
					return true;
				}
			}
		}
		return false;
	}

	// The shared endpoint of a junction's segments.
	Vec2i64 junctionVertex(const std::vector<WallSegment>& segments, const std::vector<std::size_t>& incident) {
		const WallSegment& first  = segments[incident[0]];
		const WallSegment& second = segments[incident[1]];
		return (first.a == second.a || first.a == second.b) ? first.a : first.b;
	}

	// (b): rings valid; bands and junctions tile with no overlap and no gap at a
	// seam beyond a millimetre; every junction vertex within the miter bound.
	std::string checkTiles(const std::vector<WallSegment>& segments, const WallBands& wb, double miterLimit) {
		if (wb.status != OffsetStatus::Ok) {
			return "rejected, status " + std::to_string(static_cast<int>(wb.status));
		}
		std::vector<Ring> tiles = wb.bands;
		tiles.insert(tiles.end(), wb.junctions.begin(), wb.junctions.end());
		for (const Ring& ring : tiles) {
			if (ring.size() < 3 || windingOrder(ring) != Winding::CounterClockwise || !isSimple(ring).pass) {
				return "invalid ring";
			}
		}
		for (std::size_t i = 0; i < tiles.size(); ++i) {
			for (std::size_t k = i + 1; k < tiles.size(); ++k) {
				if (overlapsDeeply(tiles[i], tiles[k])) {
					return "rings " + std::to_string(i) + " and " + std::to_string(k) + " overlap";
				}
			}
		}

		std::map<Vec2i64, std::vector<std::size_t>> atVertex;
		for (std::size_t i = 0; i < segments.size(); ++i) {
			atVertex[segments[i].a].push_back(i);
			atVertex[segments[i].b].push_back(i);
		}
		std::map<Vec2i64, std::size_t> junctionAt;
		for (std::size_t j = 0; j < wb.junctions.size(); ++j) {
			const Vec2i64 vertex = junctionVertex(segments, wb.junctionSegments[j]);
			junctionAt[vertex]	 = j;
			std::int64_t maxHalf = 0;
			for (const std::size_t i : atVertex[vertex]) {
				maxHalf = std::max(maxHalf, segments[i].halfThicknessMm);
			}
			for (const Vec2i64& p : wb.junctions[j]) {
				if (distanceBetween(p, vertex) > miterLimit * static_cast<double>(maxHalf) + 2.0) {
					return "junction vertex " + pointText(p) + " beyond the miter bound";
				}
			}
		}

		// Each band end at a junction: both cap corners and the cap's middle lie on
		// the junction polygon's boundary, or with no polygon on the other band's.
		// Rounding each ring's corners on its own leaves up to 2 mm. Rarely, at a
		// sharp wedge of a junction of four or more, a band corner and its apex sit
		// on nearly one ray from the vertex and the polygon's angular sort takes
		// them out of order, leaving a crack of about 3 mm; this bound allows it.
		for (const auto& [vertex, incident] : atVertex) {
			if (incident.size() < 2) {
				continue;
			}
			const auto polygon = junctionAt.find(vertex);
			for (const std::size_t i : incident) {
				Ring cap = wb.bands[i];
				std::sort(cap.begin(), cap.end(), [&](const Vec2i64& l, const Vec2i64& r) {
					return distanceBetween(l, vertex) < distanceBetween(r, vertex);
				});
				const Vec2i64 middle{(cap[0].x + cap[1].x) / 2, (cap[0].y + cap[1].y) / 2};
				for (const Vec2i64& p : {cap[0], cap[1], middle}) {
					double gap = 0.0;
					if (polygon != junctionAt.end()) {
						gap = distanceToRing(p, wb.junctions[polygon->second]);
					} else {
						const std::size_t other = incident[0] == i ? incident[1] : incident[0];
						if (segments[other].halfThicknessMm < segments[i].halfThicknessMm) {
							continue; // the thinner cap lies along this one, not the other way
						}
						gap = distanceToRing(p, wb.bands[other]);
					}
					if (gap > 3.5) {
						return "band " + std::to_string(i) + " cap point " + pointText(p) + " is " + std::to_string(gap) +
							   " mm off its junction";
					}
				}
			}
		}
		return "";
	}

	double ringLength(const Ring& ring, std::size_t e) {
		return distanceBetween(ring[e], ring[(e + 1) % ring.size()]);
	}

	// Perimeter of the union of tiles that meet edge to edge: every edge's length
	// less the stretches another tile's edge runs back along within a millimetre
	// or so.
	double unionPerimeter(const std::vector<Ring>& tiles) {
		double perimeter = 0.0;
		for (std::size_t t = 0; t < tiles.size(); ++t) {
			const Ring& ring = tiles[t];
			for (std::size_t e = 0; e < ring.size(); ++e) {
				const Vec2i64& p	  = ring[e];
				const Vec2i64& q	  = ring[(e + 1) % ring.size()];
				const double   length = ringLength(ring, e);
				const double   ux	  = static_cast<double>(q.x - p.x) / length;
				const double   uy	  = static_cast<double>(q.y - p.y) / length;
				double		   shared = 0.0;
				for (std::size_t o = 0; o < tiles.size(); ++o) {
					if (o == t) {
						continue;
					}
					const Ring& other = tiles[o];
					for (std::size_t f = 0; f < other.size(); ++f) {
						const Vec2i64& r = other[f];
						const Vec2i64& s = other[(f + 1) % other.size()];
						const double   fx = static_cast<double>(s.x - r.x) / ringLength(other, f);
						const double   fy = static_cast<double>(s.y - r.y) / ringLength(other, f);
						if (ux * fx + uy * fy > -0.999) {
							continue;
						}
						// The stretch of p->q that r->s runs back along, if r->s stays
						// within rounding of p->q's line over it.
						const double alongR = static_cast<double>(r.x - p.x) * ux + static_cast<double>(r.y - p.y) * uy;
						const double alongS = static_cast<double>(s.x - p.x) * ux + static_cast<double>(s.y - p.y) * uy;
						const double offR	= static_cast<double>(r.y - p.y) * ux - static_cast<double>(r.x - p.x) * uy;
						const double offS	= static_cast<double>(s.y - p.y) * ux - static_cast<double>(s.x - p.x) * uy;
						const double low	= std::max(0.0, std::min(alongR, alongS));
						const double high	= std::min(length, std::max(alongR, alongS));
						if (high <= low) {
							continue;
						}
						auto offsetAt = [&](double along) { return offR + (offS - offR) * (along - alongR) / (alongS - alongR); };
						if (std::abs(offsetAt(low)) <= 3.0 && std::abs(offsetAt(high)) <= 3.0) {
							shared += high - low;
						}
					}
				}
				perimeter += std::max(0.0, length - shared);
			}
		}
		return perimeter;
	}

	// Whether a point between the millimetre grid points lies inside any ring
	// (crossing count, so a point off every edge is never on a boundary).
	bool insideAnyRingAt(const std::vector<Ring>& rings, double x, double y) {
		for (const Ring& ring : rings) {
			bool inside = false;
			for (std::size_t e = 0, prev = ring.size() - 1; e < ring.size(); prev = e++) {
				const double ax = static_cast<double>(ring[prev].x);
				const double ay = static_cast<double>(ring[prev].y);
				const double bx = static_cast<double>(ring[e].x);
				const double by = static_cast<double>(ring[e].y);
				if ((ay > y) != (by > y) && x < ax + (bx - ax) * (y - ay) / (by - ay)) {
					inside = !inside;
				}
			}
			if (inside) {
				return true;
			}
		}
		return false;
	}

	// The outline's loops, each running until an edge returns to the loop's first
	// point; `problem` names the first edge that does not follow on.
	std::vector<std::vector<OutlineEdge>> splitLoops(const std::vector<OutlineEdge>& outline, std::string& problem) {
		std::vector<std::vector<OutlineEdge>> loops;
		for (std::size_t i = 0; i < outline.size(); ++i) {
			if (loops.empty() || loops.back().back().to == loops.back().front().from) {
				loops.emplace_back();
			} else if (outline[i].from != loops.back().back().to) {
				problem = "outline loop breaks at " + pointText(loops.back().back().to);
				return loops;
			}
			loops.back().push_back(outline[i]);
		}
		if (!loops.empty() && loops.back().back().to != loops.back().front().from) {
			problem = "outline ends with an open loop";
		}
		return loops;
	}

	// (c): the outline is closed loops, one after another, each with the wall on
	// its left; the loops' signed areas add up to the union's area and their
	// length to its perimeter.
	std::string checkOutline(const Resolved& wb) {
		const std::vector<Ring> shape = wallShape(wb);
		std::string				problem;
		double					length = 0.0;
		double					area   = 0.0;
		for (const std::vector<OutlineEdge>& loop : splitLoops(wb.outline, problem)) {
			for (const OutlineEdge& e : loop) {
				length += distanceBetween(e.from, e.to);
				area += cross(e.from, e.to).toDouble() * 0.5;
			}
		}
		if (!problem.empty()) {
			return problem;
		}
		for (const OutlineEdge& e : wb.outline) {
			const double dx	  = static_cast<double>(e.to.x - e.from.x);
			const double dy	  = static_cast<double>(e.to.y - e.from.y);
			const double span = std::hypot(dx, dy);
			// Short edges sit where corners rounded apart or in the throat of a sharp
			// inner corner, too narrow to probe; the area and length checks below
			// still count them.
			if (span < 20.0) {
				continue;
			}
			// Probed from half a millimetre to 3 mm out each side: an edge snapped a
			// millimetre off its ring can miss the wall right beside it, and a slot
			// a few mm wide has wall again beyond it. Wall at some distance to the
			// left, and not wall at every distance to the right.
			const double midX	  = (static_cast<double>(e.from.x) + static_cast<double>(e.to.x)) * 0.5;
			const double midY	  = (static_cast<double>(e.from.y) + static_cast<double>(e.to.y)) * 0.5;
			bool		 wallLeft  = false;
			bool		 wallRight = true;
			for (const double reach : {0.5, 1.0, 2.0, 3.0}) {
				wallLeft  = wallLeft || insideAnyRingAt(shape, midX - dy / span * reach, midY + dx / span * reach);
				wallRight = wallRight && insideAnyRingAt(shape, midX + dy / span * reach, midY - dx / span * reach);
			}
			if (!wallLeft || wallRight) {
				return "outline edge " + pointText(e.from) + "->" + pointText(e.to) + " is not boundary with the wall on its left";
			}
		}
		const double tileArea  = totalAreaDoubled(shape) * 0.5;
		const double perimeter = unionPerimeter(shape);
		if (std::abs(area - tileArea) > perimeter * 1.0 + 10.0) {
			return "outline encloses " + std::to_string(area) + " mm2, the rings cover " + std::to_string(tileArea);
		}
		if (std::abs(length - perimeter) > 0.002 * perimeter + 8.0) {
			return "outline is " + std::to_string(length) + " mm long, the union's perimeter " + std::to_string(perimeter);
		}
		return "";
	}

	// (d): each segment's pieces are its trimmed band less its gaps: inside the
	// band, clear of every gap, apart from each other, and covering the rest.
	std::string checkPieces(const std::vector<WallSegment>& segments, const Gaps& gaps, const Resolved& wb) {
		for (std::size_t i = 0; i < segments.size(); ++i) {
			const WallSegment& seg	  = segments[i];
			const double	   length = distanceBetween(seg.a, seg.b);
			const double	   ux	  = static_cast<double>(seg.b.x - seg.a.x) / length;
			const double	   uy	  = static_cast<double>(seg.b.y - seg.a.y) / length;
			auto			   along  = [&](const Vec2i64& p) {
				  return static_cast<double>(p.x - seg.a.x) * ux + static_cast<double>(p.y - seg.a.y) * uy;
			};
			auto across = [&](const Vec2i64& p) {
				return static_cast<double>(p.y - seg.a.y) * ux - static_cast<double>(p.x - seg.a.x) * uy;
			};
			double bandFrom = 1e18;
			double bandTo	= -1e18;
			for (const Vec2i64& p : wb.bands[i]) {
				bandFrom = std::min(bandFrom, along(p));
				bandTo	 = std::max(bandTo, along(p));
			}

			std::vector<std::pair<double, double>> open;
			for (const WallGap& gap : gapsOf(gaps, i)) {
				const double from = std::max(bandFrom, static_cast<double>(gap.fromMm));
				const double to	  = std::min(bandTo, static_cast<double>(gap.toMm));
				if (to - from >= 6.5) { // a smaller opening is ignored, give or take rounding
					open.emplace_back(from, to);
				}
			}
			std::sort(open.begin(), open.end());
			double openLength = 0.0;
			double reached	  = bandFrom;
			for (const auto& [from, to] : open) {
				openLength += std::max(0.0, to - std::max(from, reached));
				reached = std::max(reached, to);
			}

			const double width = 2.0 * static_cast<double>(seg.halfThicknessMm);
			std::vector<std::pair<double, double>> spans;
			for (const Ring& piece : wb.pieces[i]) {
				if (piece.size() < 3 || windingOrder(piece) != Winding::CounterClockwise || !isSimple(piece).pass) {
					return "segment " + std::to_string(i) + " has an invalid piece";
				}
				double from = 1e18;
				double to	= -1e18;
				for (const Vec2i64& p : piece) {
					from = std::min(from, along(p));
					to	 = std::max(to, along(p));
					if (std::abs(across(p)) > static_cast<double>(seg.halfThicknessMm) + 1.5) {
						return "segment " + std::to_string(i) + " piece leaves its band";
					}
				}
				if (from < bandFrom - 1.5 || to > bandTo + 1.5) {
					return "segment " + std::to_string(i) + " piece runs past its trimmed band";
				}
				for (const auto& [gapFrom, gapTo] : open) {
					if (std::min(to, gapTo) - std::max(from, gapFrom) > 2.0) {
						return "segment " + std::to_string(i) + " piece covers a gap";
					}
				}
				spans.emplace_back(from, to);
			}
			std::sort(spans.begin(), spans.end());
			for (std::size_t k = 1; k < spans.size(); ++k) {
				if (spans[k].first < spans[k - 1].second - 2.0) {
					return "segment " + std::to_string(i) + " pieces overlap";
				}
			}
			const double bandArea = std::abs(signedAreaDoubled(wb.bands[i]).toDouble()) * 0.5;
			const double expected = bandArea * (bandTo - bandFrom - openLength) / (bandTo - bandFrom);
			const double actual	  = totalAreaDoubled(wb.pieces[i]) * 0.5;
			// Each end within rounding, and each gap may have swallowed a run under
			// 6 mm on either side or been ignored at the threshold.
			const double slack = 2.0 * static_cast<double>(wb.pieces[i].size() + 1) + 14.0 * static_cast<double>(gapsOf(gaps, i).size());
			if (std::abs(actual - expected) > slack * width) {
				return "segment " + std::to_string(i) + " pieces cover " + std::to_string(actual) + " mm2, expected " +
					   std::to_string(expected);
			}
		}
		return "";
	}

	// Everything resolveWallBands returns, in a form that does not depend on the
	// segments' order (indices mapped back through `original`) or on where a
	// ring's vertex list starts.
	Ring fromLowest(Ring ring) {
		std::rotate(ring.begin(), std::min_element(ring.begin(), ring.end()), ring.end());
		return ring;
	}

	struct Canonical {
		std::vector<Ring>										   bands;
		std::vector<std::vector<Ring>>							   pieces;
		std::vector<std::pair<std::vector<std::size_t>, Ring>>	   junctions;
		std::vector<std::tuple<Vec2i64, Vec2i64, std::size_t>> outline;

		bool operator==(const Canonical&) const = default;
	};

	Canonical canonical(const Resolved& wb, const std::vector<std::size_t>& original) {
		Canonical result;
		result.bands.resize(wb.bands.size());
		result.pieces.resize(wb.bands.size());
		for (std::size_t i = 0; i < wb.bands.size(); ++i) {
			result.bands[original[i]] = fromLowest(wb.bands[i]);
			for (const Ring& piece : wb.pieces[i]) {
				result.pieces[original[i]].push_back(fromLowest(piece));
			}
			std::sort(result.pieces[original[i]].begin(), result.pieces[original[i]].end());
		}
		for (std::size_t j = 0; j < wb.junctions.size(); ++j) {
			std::vector<std::size_t> incident;
			for (const std::size_t i : wb.junctionSegments[j]) {
				incident.push_back(original[i]);
			}
			std::sort(incident.begin(), incident.end());
			result.junctions.emplace_back(incident, fromLowest(wb.junctions[j]));
		}
		std::sort(result.junctions.begin(), result.junctions.end());
		for (const OutlineEdge& e : wb.outline) {
			result.outline.emplace_back(e.from, e.to, original[e.index]);
		}
		std::sort(result.outline.begin(), result.outline.end());
		return result;
	}

	// A segment's gaps measured from its other end.
	std::vector<WallGap> reversedGaps(const WallSegment& seg, const std::vector<WallGap>& gaps) {
		const double		 length = distanceBetween(seg.a, seg.b);
		std::vector<WallGap> flipped;
		for (const WallGap& gap : gaps) {
			const std::int64_t from = std::llround(length - static_cast<double>(gap.toMm));
			const std::int64_t to	= std::llround(length - static_cast<double>(gap.fromMm));
			flipped.push_back({from, to});
		}
		return flipped;
	}

	// (e): reversing every segment and listing them in another order gives the
	// same geometry, exactly.
	std::string checkInvariance(const std::vector<WallSegment>& segments, const Gaps& gaps, const Resolved& wb,
								std::mt19937& rng) {
		std::vector<std::size_t> original(segments.size());
		for (std::size_t i = 0; i < original.size(); ++i) {
			original[i] = i;
		}
		std::shuffle(original.begin(), original.end(), rng);
		std::vector<WallSegment> shuffled;
		Gaps					 shuffledGaps;
		for (std::size_t i = 0; i < original.size(); ++i) {
			const WallSegment&			seg		  = segments[original[i]];
			const std::vector<WallGap>& segGaps = gapsOf(gaps, original[i]);
			shuffled.push_back(i % 2 == 0 ? WallSegment{seg.b, seg.a, seg.halfThicknessMm} : seg);
			shuffledGaps.push_back(i % 2 == 0 ? reversedGaps(seg, segGaps) : segGaps);
		}
		std::vector<std::size_t> identity(segments.size());
		for (std::size_t i = 0; i < identity.size(); ++i) {
			identity[i] = i;
		}
		const Resolved other = resolveDrawn(shuffled, kDefaultMiterLimit, shuffledGaps);
		if (other.status != wb.status) {
			return "reordered input resolves with another status";
		}
		if (!(canonical(other, original) == canonical(wb, identity))) {
			return "reordered input resolves differently";
		}
		return "";
	}

	struct Sweep {
		int						 inputs = 0;
		int						 failed = 0;
		std::vector<std::string> examples;

		void check(const std::vector<WallSegment>& segments, const Gaps& gaps, const std::string& problem) {
			++inputs;
			if (!problem.empty()) {
				++failed;
				if (examples.size() < 5) {
					examples.push_back(problem + " | " + describe(segments, gaps));
				}
			}
		}

		std::string report() const {
			std::string text = std::to_string(failed) + " of " + std::to_string(inputs) + " inputs failed";
			for (const std::string& example : examples) {
				text += "\n  " + example;
			}
			return text;
		}
	};

	std::string checkAll(const std::vector<WallSegment>& segments, const Gaps& gaps, std::mt19937& rng) {
		const Resolved wb = resolveDrawn(segments, kDefaultMiterLimit, gaps);
		std::string		problem = checkTiles(segments, wb, kDefaultMiterLimit);
		if (problem.empty()) {
			problem = checkPieces(segments, gaps, wb);
		}
		if (problem.empty()) {
			problem = checkOutline(wb);
		}
		if (problem.empty()) {
			problem = checkInvariance(segments, gaps, wb, rng);
		}
		return problem;
	}

	// Random arms at least 30 degrees apart, a quarter of them snapped to 15
	// degree steps the way the editor's angle snap places them.
	std::vector<Arm> randomArms(int degree, bool snapped, std::mt19937& rng) {
		std::uniform_real_distribution<double> angle(0.0, 360.0);
		std::vector<Arm>					   arms;
		for (int guard = 0; static_cast<int>(arms.size()) < degree && guard < 2000; ++guard) {
			double candidate = angle(rng);
			if (snapped) {
				candidate = std::round(candidate / 15.0) * 15.0;
			}
			bool clear = true;
			for (const Arm& arm : arms) {
				const double apart = std::fmod(std::abs(arm.degrees - candidate), 360.0);
				clear			   = clear && std::min(apart, 360.0 - apart) >= 30.0;
			}
			if (clear) {
				const std::int64_t half	   = kHalves[rng() % 3];
				const bool		   outward = rng() % 2 == 0;
				arms.push_back({candidate, half, outward});
			}
		}
		return arms;
	}

} // namespace

TEST(WallProperties, DegreeTwoAcrossTheLegalRange) {
	// Every 2.5 degrees from 30 to 330 for each pairing of halves, turned off the
	// axes by a varying amount so the rounding differs from corner to corner.
	std::mt19937 rng(7);
	Sweep		 sweep;
	int			 index = 0;
	for (double corner = 30.0; corner <= 330.0; corner += 0.5) {
		for (const std::int64_t first : kHalves) {
			for (const std::int64_t second : kHalves) {
				const double base = static_cast<double>(index % 12) * 7.5 + static_cast<double>(index % 5) * 0.3;
				const bool	 flip = index % 2 == 1;
				++index;
				const std::vector<WallSegment> segments = star({{base, first, true}, {base + corner, second, !flip}});
				sweep.check(segments, {}, checkAll(segments, {}, rng));
			}
		}
	}
	EXPECT_EQ(sweep.failed, 0) << sweep.report();
}

TEST(WallProperties, NearStraightJoinsResolveLocally) {
	// The join that used to reject a whole wall graph: a few degrees either side
	// of straight, most of all with two thicknesses. Accepted, tiled, and inside
	// the miter bound at every offset down to a tenth of a degree.
	std::mt19937 rng(11);
	Sweep		 sweep;
	for (const double offset : {0.1, 1.0, 3.0, 5.0, 10.0}) {
		for (const double side : {-1.0, 1.0}) {
			for (const double base : {0.0, 17.0, 45.0, 103.0}) {
				for (const std::int64_t first : kHalves) {
					for (const std::int64_t second : kHalves) {
						for (const bool flip : {false, true}) {
							const std::vector<WallSegment> segments =
								star({{base, first, true}, {base + 180.0 + side * offset, second, !flip}});
							sweep.check(segments, {}, checkAll(segments, {}, rng));
						}
					}
				}
			}
		}
	}
	// A T whose through-run bends a few degrees between two thicknesses.
	for (const double offset : {-5.0, -1.0, -0.1, 0.1, 1.0, 5.0}) {
		for (const double stem : {60.0, 90.0, 135.0, 270.0}) {
			const std::vector<WallSegment> segments = star({{0.0, 75, true}, {180.0 + offset, 150, false}, {stem, 100, true}});
			sweep.check(segments, {}, checkAll(segments, {}, rng));
		}
	}
	// Two arms of one thickness 0.03 degrees off straight in a junction of five:
	// their faces nearly coincide, so the rounded apex lands metres off.
	const std::vector<WallSegment> equalPair = {
		{{826, -1821}, {0, 0}, 75},	  {{1954, 425}, {0, 0}, 150},	 {{0, 0}, {-1252, -1560}, 75},
		{{1775, -921}, {0, 0}, 150}, {{1251, 1560}, {0, 0}, 75},
	};
	sweep.check(equalPair, {}, checkAll(equalPair, {}, rng));
	EXPECT_EQ(sweep.failed, 0) << sweep.report();
}

TEST(WallProperties, HigherDegreeJunctions) {
	std::mt19937 rng(42);
	Sweep		 sweep;
	for (int degree = 3; degree <= 6; ++degree) {
		for (int trial = 0; trial < 1000; ++trial) {
			const std::vector<Arm> arms = randomArms(degree, trial % 4 == 0, rng);
			if (static_cast<int>(arms.size()) < degree) {
				continue;
			}
			const std::vector<WallSegment> segments = star(arms);
			sweep.check(segments, {}, checkAll(segments, {}, rng));
			// The same junction with an opening somewhere along each arm, at times
			// reaching into the junction's trim.
			Gaps gaps;
			for (std::size_t i = 0; i < segments.size(); ++i) {
				const std::int64_t from	 = static_cast<std::int64_t>(rng() % 1600);
				const std::int64_t width = 300 + static_cast<std::int64_t>(rng() % 600);
				gaps.push_back({{from, from + width}});
			}
			sweep.check(segments, gaps, checkAll(segments, gaps, rng));
		}
	}
	EXPECT_EQ(sweep.failed, 0) << sweep.report();
}

TEST(WallProperties, GapsCutPiecesAndKeepTheOutlineClosed) {
	// Gaps on the first arm of corners, tees and crosses: inside the band,
	// touching its trimmed end, reaching into the trim (a sharp corner trims
	// further than an opening's margin), past the free end, overlapping each
	// other, and covering the whole segment. The other arms carry a gap too, so
	// a junction can lose more than one band.
	std::mt19937 rng(5);
	Sweep		 sweep;
	const std::vector<std::vector<Arm>> shapes = {
		{{0.0, 150, true}, {30.0, 150, false}},
		{{0.0, 75, false}, {37.5, 150, true}},
		{{0.0, 100, true}, {90.0, 100, true}},
		{{0.0, 75, true}, {135.0, 150, false}},
		{{0.0, 75, true}, {177.0, 150, true}},
		{{0.0, 150, true}, {180.0, 75, true}},
		{{0.0, 100, true}, {90.0, 150, false}, {180.0, 100, true}},
		{{0.0, 75, false}, {120.0, 100, true}, {240.0, 150, true}},
		{{0.0, 100, true}, {90.0, 75, true}, {180.0, 150, false}, {270.0, 100, true}},
	};
	for (const std::vector<Arm>& arms : shapes) {
		const std::vector<WallSegment> plain = star(arms);
		const WallBands				   bands = resolveWallBands(plain, kDefaultMiterLimit);
		ASSERT_EQ(bands.status, OffsetStatus::Ok) << describe(plain, {});
		// Where the first arm's band starts and ends, measured from its `a`.
		const WallSegment& first  = plain[0];
		const double	   length = distanceBetween(first.a, first.b);
		double			   from	  = 1e18;
		double			   to	  = -1e18;
		for (const Vec2i64& p : bands.bands[0]) {
			const double along = (static_cast<double>(p.x - first.a.x) * static_cast<double>(first.b.x - first.a.x) +
								  static_cast<double>(p.y - first.a.y) * static_cast<double>(first.b.y - first.a.y)) /
								 length;
			from = std::min(from, along);
			to	 = std::max(to, along);
		}
		const std::int64_t start = std::llround(from);
		const std::int64_t end	 = std::llround(to);
		const std::int64_t whole = std::llround(length);
		const std::vector<std::vector<WallGap>> families = {
			{{start + 400, start + 1300}},
			{{start, start + 600}},
			{{end - 700, end}},
			{{std::max<std::int64_t>(0, start / 3), start + 500}},
			{{end - 500, whole}},
			{{end - 500, whole + 300}, {-300, start + 100}},
			{{start + 300, start + 900}, {start + 800, start + 1200}, {start + 1100, start + 1150}},
			{{start + 10, end - 10}},
			{{start + 4, end - 4}},
			{{-10, whole + 10}},
		};
		for (const std::vector<WallGap>& family : families) {
			Gaps gaps(plain.size());
			gaps[0] = family;
			sweep.check(plain, gaps, checkAll(plain, gaps, rng));
			for (std::size_t i = 1; i < plain.size(); ++i) {
				gaps[i] = {{900, 1500}};
			}
			sweep.check(plain, gaps, checkAll(plain, gaps, rng));
		}
	}
	EXPECT_EQ(sweep.failed, 0) << sweep.report();
}

TEST(WallProperties, ClosedRoomOutlinesOutsideAndInside) {
	// A closed room is an annulus: one loop counter-clockwise round the outside,
	// one clockwise round the inside, and a door makes it one loop again.
	const std::vector<WallSegment> room = {
		{{0, 0}, {4000, 0}, 100},
		{{4000, 0}, {4000, 3000}, 150},
		{{4000, 3000}, {0, 3000}, 100},
		{{0, 3000}, {0, 0}, 75},
	};
	std::mt19937 rng(3);
	EXPECT_EQ(checkAll(room, {}, rng), "");
	const Resolved closed = resolveDrawn(room, kDefaultMiterLimit);
	ASSERT_EQ(closed.status, OffsetStatus::Ok);
	std::string			problem;
	std::vector<double> loopAreas;
	for (const std::vector<OutlineEdge>& loop : splitLoops(closed.outline, problem)) {
		double area = 0.0;
		for (const OutlineEdge& e : loop) {
			area += cross(e.from, e.to).toDouble() * 0.5;
		}
		loopAreas.push_back(area);
	}
	EXPECT_EQ(problem, "");
	ASSERT_EQ(loopAreas.size(), 2u);
	EXPECT_EQ(std::count_if(loopAreas.begin(), loopAreas.end(), [](double a) { return a > 0.0; }), 1);
	EXPECT_EQ(std::count_if(loopAreas.begin(), loopAreas.end(), [](double a) { return a < 0.0; }), 1);

	EXPECT_EQ(checkAll(room, {{{1500, 2400}}, {}, {}, {}}, rng), "");
}

TEST(WallProperties, NearStraightMixedJoinIsAShortStep) {
	// Three degrees off straight from half 75 into half 150: the thin band is cut
	// back just clear of the thick band's cap (a few mm), the thick band keeps its
	// full length, and the polygon between them is the step.
	const std::vector<WallSegment> segments = star({{0.0, 75, true}, {183.0, 150, true}});
	const WallBands				   wb		= resolveWallBands(segments, kDefaultMiterLimit);
	ASSERT_EQ(wb.status, OffsetStatus::Ok);
	ASSERT_EQ(wb.junctions.size(), 1u);
	for (const Vec2i64& p : wb.junctions[0]) {
		EXPECT_LE(distanceBetween(p, {0, 0}), 152.0);
	}
	double thinStart = 1e18;
	for (const Vec2i64& p : wb.bands[0]) {
		thinStart = std::min(thinStart, static_cast<double>(p.x));
	}
	EXPECT_GT(thinStart, 0.0);
	EXPECT_LT(thinStart, 10.0);
	for (const Vec2i64& p : wb.bands[1]) {
		EXPECT_GE(distanceBetween(p, {0, 0}), 149.0); // the thick band is not cut back
	}
}
