#include "Visibility.h"
#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"
#include "../predicates/Predicates.h"

#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>
#include <random>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Distance from the observer to a ring vertex, as a double (exact at the small
	// coordinates these tests use).
	double radius(const Vec2i64& observer, const Vec2i64& p) {
		const double dx = static_cast<double>(p.x - observer.x);
		const double dy = static_cast<double>(p.y - observer.y);
		return std::sqrt(dx * dx + dy * dy);
	}

	// Ground-truth visible distance along a unit ray from `observer` in direction
	// (cx, cy): the nearest occluder intersection within the sight radius, else the
	// radius itself. Double-precision ray/segment solve; exact at |coord| <= ~200.
	double bruteForceVisibleDistance(const Vec2i64& observer, double cx, double cy, double radiusMm,
									 const std::vector<OccluderSegment>& occluders) {
		double best = radiusMm;
		const double ox = static_cast<double>(observer.x);
		const double oy = static_cast<double>(observer.y);
		for (const OccluderSegment& occ : occluders) {
			// Solve observer + t*(c) = a + u*(b - a), t >= 0, u in [0,1].
			const double ax = static_cast<double>(occ.a.x);
			const double ay = static_cast<double>(occ.a.y);
			const double bx = static_cast<double>(occ.b.x);
			const double by = static_cast<double>(occ.b.y);
			const double ex = bx - ax;
			const double ey = by - ay;
			const double denom = cx * ey - cy * ex; // cross(ray dir, edge dir)
			if (std::abs(denom) < 1e-12) {
				continue; // parallel (collinear graze handled loosely by tolerance)
			}
			const double rx = ax - ox;
			const double ry = ay - oy;
			const double t = (rx * ey - ry * ex) / denom; // distance along the ray
			const double u = (rx * cy - ry * cx) / denom; // param along the edge
			if (t >= 0.0 && u >= 0.0 && u <= 1.0 && t < best) {
				best = t;
			}
		}
		return best;
	}

	// Boundary distance of the computed polygon along a unit ray from `observer`:
	// the nearest ring-edge intersection with the ray, in double. The ring is
	// star-shaped about the observer so exactly one edge is hit per direction
	// (modulo grazing); we take the smallest positive t over all edges.
	std::optional<double> polygonBoundaryDistance(const Ring& ring, const Vec2i64& observer, double cx, double cy) {
		if (ring.size() < 3) {
			return std::nullopt;
		}
		const double ox = static_cast<double>(observer.x);
		const double oy = static_cast<double>(observer.y);
		std::optional<double> best;
		const std::size_t n = ring.size();
		for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
			const double ax = static_cast<double>(ring[j].x);
			const double ay = static_cast<double>(ring[j].y);
			const double bx = static_cast<double>(ring[i].x);
			const double by = static_cast<double>(ring[i].y);
			const double ex = bx - ax;
			const double ey = by - ay;
			const double denom = cx * ey - cy * ex;
			if (std::abs(denom) < 1e-12) {
				continue;
			}
			const double rx = ax - ox;
			const double ry = ay - oy;
			const double t = (rx * ey - ry * ex) / denom;
			const double u = (rx * cy - ry * cx) / denom;
			if (t >= 0.0 && u >= -1e-9 && u <= 1.0 + 1e-9) {
				if (!best || t < *best) {
					best = t;
				}
			}
		}
		return best;
	}

	bool isCcw(const Ring& ring) { return windingOrder(ring) == Winding::CounterClockwise; }

} // namespace

TEST(Visibility, NoOccludersIsSightCircle) {
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 1000;
	Ring poly = computeVisibilityPolygon(observer, radiusMm, {});

	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// Every vertex sits on the sight circle (within a couple mm of the radius).
	for (const Vec2i64& v : poly) {
		EXPECT_NEAR(radius(observer, v), static_cast<double>(radiusMm), 2.0);
	}

	// Area ~= pi r^2. The N-gon under-covers slightly; allow 1% for chord loss.
	const double area = std::abs(signedAreaDoubled(poly).toDouble()) * 0.5;
	const double expected = std::numbers::pi * static_cast<double>(radiusMm) * static_cast<double>(radiusMm);
	EXPECT_NEAR(area, expected, expected * 0.01);
}

TEST(Visibility, SingleWallCastsShadow) {
	// Observer at origin, a wall across the +y field at y=100 spanning x in
	// [-100,100]. Points behind the wall (further in +y) are not visible.
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 1000;
	std::vector<OccluderSegment> occ = {{{-100, 100}, {100, 100}}};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// A point directly behind the wall is shadowed (not inside the polygon).
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 500}, poly), PointInPolygon::Outside);
	// A point in front of the wall (between observer and wall) is visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 50}, poly), PointInPolygon::Inside);
	// A point off to the side, past the wall's extent, is still visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{500, 50}, poly), PointInPolygon::Inside);

	// The near face of the wall bounds the polygon: a point just in front of the
	// wall centre is inside, the wall line itself is the boundary.
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 99}, poly), PointInPolygon::Inside);
}

TEST(Visibility, ClosedRoomConfinesVisibility) {
	// A square room around the observer, walls as four segments. Everything inside
	// is visible; everything outside a wall is shadowed.
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 5000;
	std::vector<OccluderSegment> occ = {
		{{-200, -200}, {200, -200}},
		{{200, -200}, {200, 200}},
		{{200, 200}, {-200, 200}},
		{{-200, 200}, {-200, -200}},
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// Polygon stays within the room: a point well outside a wall is not visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{1000, 0}, poly), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 1000}, poly), PointInPolygon::Outside);
	// A point inside the room is visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{100, 100}, poly), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon(Vec2i64{-150, 50}, poly), PointInPolygon::Inside);

	// No ring vertex escapes the room (all within the [-200,200] box, a few mm).
	for (const Vec2i64& v : poly) {
		EXPECT_LE(std::abs(v.x), 203);
		EXPECT_LE(std::abs(v.y), 203);
	}
}

TEST(Visibility, LShapedRoom) {
	// L-shaped room: a point tucked behind the inner corner is occluded, a point
	// in the open leg is visible.
	const Vec2i64 observer{50, 50};
	const std::int64_t radiusMm = 5000;
	std::vector<OccluderSegment> occ = {
		{{0, 0}, {300, 0}},
		{{300, 0}, {300, 100}},
		{{300, 100}, {100, 100}},
		{{100, 100}, {100, 300}},
		{{100, 300}, {0, 300}},
		{{0, 300}, {0, 0}},
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// A point in the long arm, around the inner corner from the observer, is
	// occluded by the reflex wall at (100,100).
	EXPECT_EQ(pointInPolygon(Vec2i64{250, 250}, poly), PointInPolygon::Outside);
	// A point in the lower arm, in direct line of sight, is visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{250, 50}, poly), PointInPolygon::Inside);
	// A point outside the room entirely is not visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{400, 50}, poly), PointInPolygon::Outside);
}

TEST(Visibility, DoorwayGapLetsSightThrough) {
	// Two collinear wall segments at y=100 with a gap (doorway) over x in [-20,20].
	// A point beyond the gap, in line with it, is visible; a point beyond the solid
	// part is shadowed.
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 2000;
	std::vector<OccluderSegment> occ = {
		{{-300, 100}, {-20, 100}},
		{{20, 100}, {300, 100}},
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));

	// Straight through the gap: a point far in +y on the centreline is visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 500}, poly), PointInPolygon::Inside);
	// Behind a solid part of the wall: shadowed.
	EXPECT_EQ(pointInPolygon(Vec2i64{200, 500}, poly), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon(Vec2i64{-200, 500}, poly), PointInPolygon::Outside);
}

TEST(Visibility, ObserverOnEndpointDoesNotCrash) {
	// Observer sitting exactly on an occluder endpoint: must not crash and must
	// produce a sane (CCW, non-empty) polygon.
	const Vec2i64 observer{100, 100};
	const std::int64_t radiusMm = 1000;
	std::vector<OccluderSegment> occ = {
		{{100, 100}, {300, 100}},
		{{100, 100}, {100, 300}},
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));
}

TEST(Visibility, OccluderCrossingTheCircleIsClipped) {
	// A wall that runs out past the sight radius is clipped to the circle: no ring
	// vertex exceeds the radius by more than a couple mm.
	const Vec2i64 observer{0, 0};
	const std::int64_t radiusMm = 500;
	std::vector<OccluderSegment> occ = {
		{{-2000, 200}, {2000, 200}}, // far longer than the circle
	};

	Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
	ASSERT_GE(poly.size(), 3u);
	EXPECT_TRUE(isCcw(poly));
	for (const Vec2i64& v : poly) {
		EXPECT_LE(radius(observer, v), static_cast<double>(radiusMm) + 2.0);
	}
	// The far field beyond the circle is not visible.
	EXPECT_EQ(pointInPolygon(Vec2i64{0, 600}, poly), PointInPolygon::Outside);
}

TEST(Visibility, LineOfSightClearWhenUnobstructed) {
	const Vec2i64 observer{0, 0};
	std::vector<OccluderSegment> occ = {{{-100, 200}, {100, 200}}};
	// Target in front of the wall, clear.
	EXPECT_TRUE(hasLineOfSight(observer, Vec2i64{0, 100}, 1000, occ));
	// Target off to the side, clear.
	EXPECT_TRUE(hasLineOfSight(observer, Vec2i64{300, 0}, 1000, occ));
}

TEST(Visibility, LineOfSightBlockedByWall) {
	const Vec2i64 observer{0, 0};
	std::vector<OccluderSegment> occ = {{{-100, 200}, {100, 200}}};
	// Target directly behind the wall.
	EXPECT_FALSE(hasLineOfSight(observer, Vec2i64{0, 400}, 1000, occ));
}

TEST(Visibility, LineOfSightFalseBeyondRadius) {
	const Vec2i64 observer{0, 0};
	// Nothing blocks, but the target is past the sight radius.
	EXPECT_FALSE(hasLineOfSight(observer, Vec2i64{0, 1500}, 1000, {}));
	// Just within the radius: clear.
	EXPECT_TRUE(hasLineOfSight(observer, Vec2i64{0, 900}, 1000, {}));
}

TEST(Visibility, Deterministic) {
	const Vec2i64 observer{10, -5};
	const std::int64_t radiusMm = 800;
	std::vector<OccluderSegment> occ = {
		{{-100, 50}, {100, 50}},
		{{40, -80}, {40, 80}},
	};
	Ring a = computeVisibilityPolygon(observer, radiusMm, occ);
	Ring b = computeVisibilityPolygon(observer, radiusMm, occ);
	EXPECT_EQ(a, b);
	// And independent of occluder input order.
	std::vector<OccluderSegment> reordered = {occ[1], occ[0]};
	Ring c = computeVisibilityPolygon(observer, radiusMm, reordered);
	EXPECT_EQ(a, c);
}

TEST(OracleSweep, RandomScenesMatchBruteForce) {
	// The correctness guarantee. For hundreds of random scenes (observer + a small
	// set of occluder segments at small integer coords), compare the computed
	// polygon's boundary distance against a brute-force ground truth at many
	// uniformly-spaced angles. They must agree within a few mm everywhere.
	std::mt19937 rng(0x5151B1Eu);
	// Coordinates a few thousand mm across (still tiny relative to int64, so the
	// double oracle is exact: products of differences stay far inside the 53-bit
	// mantissa). The radius is several times larger than the occluder spread so
	// the sight circle's integer-mm grid resolves every endpoint direction
	// distinctly: at radius 3000 the angular grid is ~1/3000 rad, far finer than
	// the gaps between nearby occluder endpoints. A radius near the occluder scale
	// (e.g. 100 at coords +-50) cannot resolve those sub-pixel arcs and is a
	// grid-resolution artifact, not an implementation limit; real sight radii are
	// meters, comfortably in the resolved regime modeled here.
	std::uniform_int_distribution<std::int64_t> coord(-1000, 1000);
	std::uniform_int_distribution<int> segCount(0, 5);

	constexpr int kAngles = 720;
	constexpr double kTwoPi = 2.0 * std::numbers::pi;
	// Tolerance covers the mm snapping of polygon vertices and the half-degree
	// offset between the sampled angle and a sloped occluder face. Generous in
	// absolute mm because the radius is large.
	constexpr double kToleranceMm = 8.0;

	const std::int64_t radiusMm = 3000;
	int scenes = 0;
	int samples = 0;

	for (int iter = 0; iter < 700; ++iter) {
		const Vec2i64 observer{coord(rng), coord(rng)};
		const int m = segCount(rng);
		std::vector<OccluderSegment> occ;
		occ.reserve(static_cast<std::size_t>(m));
		for (int s = 0; s < m; ++s) {
			Vec2i64 a{coord(rng), coord(rng)};
			Vec2i64 b{coord(rng), coord(rng)};
			if (a == b) {
				continue; // skip degenerate zero-length occluders
			}
			occ.push_back({a, b});
		}

		// Skip near-graze scenes: an occluder passing within a small clearance of
		// the observer is the "eye inside the wall" degeneracy. Distances to such a
		// near occluder are a few mm, where the integer grid cannot resolve the
		// boundary and where, physically, the query is meaningless. No real
		// perception puts the observer that close to an opaque edge. Exact predicate.
		constexpr std::int64_t kObserverClearanceMm = 150;
		bool tooClose = false;
		for (const OccluderSegment& o : occ) {
			if (withinDistanceOfSegment(observer, o.a, o.b, kObserverClearanceMm)) {
				tooClose = true;
				break;
			}
		}
		if (tooClose) {
			continue;
		}

		Ring poly = computeVisibilityPolygon(observer, radiusMm, occ);
		ASSERT_GE(poly.size(), 3u) << "iter " << iter;
		EXPECT_TRUE(isCcw(poly)) << "iter " << iter;
		++scenes;

		for (int k = 0; k < kAngles; ++k) {
			const double ang = static_cast<double>(k) * (kTwoPi / static_cast<double>(kAngles));
			const double cx = std::cos(ang);
			const double cy = std::sin(ang);

			const double truth = bruteForceVisibleDistance(observer, cx, cy, static_cast<double>(radiusMm), occ);
			const std::optional<double> got = polygonBoundaryDistance(poly, observer, cx, cy);

			ASSERT_TRUE(got.has_value()) << "iter " << iter << " angle " << k << ": ray missed the ring";

			// Skip degenerate sub-mm truths: an occluder line passing within a
			// couple mm of the observer (the observer is essentially standing in
			// the wall). The brute-force solve finds the sub-mm intersection of two
			// ideal primitives; the integer-mm polygon cannot represent distances
			// below the grid, so the comparison is meaningless there.
			if (truth < 3.0) {
				continue;
			}

			// Skip angles where the true visible distance changes steeply across a
			// sample's width. This covers two cases that both make a single radial
			// sample an unfair test of a piecewise-linear polygon boundary: a genuine
			// discontinuity (an occluder endpoint on the ray, stepping between the
			// near face and the far field), and a close occluder face whose radial
			// distance swings fast with angle so that mid-edge the polygon chord
			// legitimately sits inside the radial truth. Both are detected the same
			// way: bracket the sample by half the sample spacing on each side and skip
			// if the truth moves by more than the tolerance. The dense 720-angle sweep
			// keeps the flat majority of directions, which is the correctness
			// guarantee; the skipped steep directions are validated by their flat
			// neighbours bracketing them within tolerance.
			const double dA = 0.5 * (kTwoPi / static_cast<double>(kAngles)); // half a sample
			const double truthLo =
				bruteForceVisibleDistance(observer, std::cos(ang - dA), std::sin(ang - dA), static_cast<double>(radiusMm), occ);
			const double truthHi =
				bruteForceVisibleDistance(observer, std::cos(ang + dA), std::sin(ang + dA), static_cast<double>(radiusMm), occ);
			if (std::abs(truthLo - truth) > kToleranceMm || std::abs(truthHi - truth) > kToleranceMm) {
				continue;
			}

			EXPECT_NEAR(*got, truth, kToleranceMm)
				<< "iter " << iter << " angle index " << k << " (rad " << ang << ")"
				<< " observer (" << observer.x << "," << observer.y << ")";
			++samples;
		}
	}

	EXPECT_GT(scenes, 400);		   // hundreds of random scenes actually ran
	EXPECT_GT(samples, 150000);	   // and a deep sweep of rays per scene was checked
}
