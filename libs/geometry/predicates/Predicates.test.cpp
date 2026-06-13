#include "Predicates.h"
#include "../core/Vec2i64.h"

#include <cstdint>
#include <random>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Exact-in-double orientation oracle: for |coords| <= ~50 the determinant fits
	// far inside the 53-bit mantissa, so double is exact and trivially correct.
	int orientationOracle(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c) {
		const double det = (static_cast<double>(b.x - a.x) * static_cast<double>(c.y - a.y)) -
						   (static_cast<double>(b.y - a.y) * static_cast<double>(c.x - a.x));
		return det > 0 ? 1 : (det < 0 ? -1 : 0);
	}

	// Exact-in-double squared distance from p to the closed segment [a,b].
	double segDistSqOracle(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
		const double abx = static_cast<double>(b.x - a.x);
		const double aby = static_cast<double>(b.y - a.y);
		const double apx = static_cast<double>(p.x - a.x);
		const double apy = static_cast<double>(p.y - a.y);
		const double len2 = abx * abx + aby * aby;
		double		 t	  = 0.0;
		if (len2 > 0.0) {
			t = (apx * abx + apy * aby) / len2;
			if (t < 0.0) {
				t = 0.0;
			}
			if (t > 1.0) {
				t = 1.0;
			}
		}
		const double dx = apx - t * abx;
		const double dy = apy - t * aby;
		return dx * dx + dy * dy;
	}

	// Crossing-number point-in-polygon oracle in double (exact at this scale),
	// with an explicit on-boundary pass first.
	int pipOracle(const Vec2i64& p, const std::vector<Vec2i64>& ring) {
		const std::size_t n = ring.size();
		for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
			if (segDistSqOracle(p, ring[j], ring[i]) == 0.0) {
				return 0; // on boundary
			}
		}
		bool inside = false;
		for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
			const Vec2i64& vi = ring[i];
			const Vec2i64& vj = ring[j];
			if ((vi.y > p.y) != (vj.y > p.y)) {
				const double xCross = static_cast<double>(vj.x - vi.x) * static_cast<double>(p.y - vi.y) /
										  static_cast<double>(vj.y - vi.y) +
									  static_cast<double>(vi.x);
				if (static_cast<double>(p.x) < xCross) {
					inside = !inside;
				}
			}
		}
		return inside ? 1 : -1; // 1 inside, -1 outside
	}

} // namespace

TEST(Orientation, BasicTriangle) {
	Vec2i64 a{0, 0};
	Vec2i64 b{1000, 0};
	Vec2i64 c{0, 1000};
	EXPECT_EQ(orientation(a, b, c), Orientation::CounterClockwise);
	EXPECT_EQ(orientation(a, c, b), Orientation::Clockwise);
}

TEST(Orientation, Collinear) {
	EXPECT_EQ(orientation({0, 0}, {500, 500}, {1000, 1000}), Orientation::Collinear);
	EXPECT_EQ(orientation({0, 0}, {0, 0}, {0, 0}), Orientation::Collinear);
}

TEST(Orientation, HugeCoordinatesExactWhereDoubleFails) {
	// Coordinates around 4e18/3 ~ 1.33e18: products reach ~1.8e36, far past the
	// int64 range (~9.2e18) yet inside int128. Construct a triple whose true
	// determinant is small and negative; the naive double cross product rounds
	// it to zero and reports Collinear, the wrong answer.
	const std::int64_t n = 1333333333333333333LL; // ~1.33e18
	Vec2i64			   a{0, 0};
	Vec2i64			   b{n, n};
	Vec2i64			   c{2 * n + 1, 2 * n};
	// Exact determinant = n*(2n) - n*(2n+1) = -n  ->  Clockwise.
	EXPECT_EQ(orientation(a, b, c), Orientation::Clockwise);

	// Demonstrate the double path is wrong (regression guard for the exactness).
	const double dcross = (static_cast<double>(b.x - a.x) * static_cast<double>(c.y - a.y)) -
						  (static_cast<double>(b.y - a.y) * static_cast<double>(c.x - a.x));
	EXPECT_EQ(dcross, 0.0); // double loses the -n term entirely
}

TEST(Orientation, NearCollinearHugeStaysExact) {
	const std::int64_t n = 1000000000000000000LL; // 1e18
	// True determinant = +1 (CCW) at enormous scale.
	Vec2i64 a{0, 0};
	Vec2i64 b{n, n};
	Vec2i64 c{n, n + 1};
	EXPECT_EQ(orientation(a, b, c), Orientation::CounterClockwise);
}

TEST(SegmentIntersect, ProperCrossing) {
	auto r = intersectSegments({0, 0}, {1000, 1000}, {0, 1000}, {1000, 0});
	EXPECT_EQ(r.relation, SegmentRelation::ProperCrossing);
	EXPECT_EQ(r.point, (Vec2i64{500, 500}));
}

TEST(SegmentIntersect, ProperCrossingRoundsToNearestMm) {
	// Diagonal y=x meets the line from (0,1000) to (1000,850): cross at
	// (869.565.., 869.565..), which rounds to nearest mm = 870.
	auto r = intersectSegments({0, 0}, {1000, 1000}, {0, 1000}, {1000, 850});
	EXPECT_EQ(r.relation, SegmentRelation::ProperCrossing);
	// y = 1000 - 0.15x and y = x  =>  x = 1000/1.15 = 869.565..  rounds to 870.
	EXPECT_EQ(r.point.x, 870);
	EXPECT_EQ(r.point.y, 870);
}

TEST(SegmentIntersect, Disjoint) {
	auto r = intersectSegments({0, 0}, {100, 0}, {0, 100}, {100, 100});
	EXPECT_EQ(r.relation, SegmentRelation::Disjoint);
}

TEST(SegmentIntersect, DisjointParallelCollinearGap) {
	auto r = intersectSegments({0, 0}, {100, 0}, {200, 0}, {300, 0});
	EXPECT_EQ(r.relation, SegmentRelation::Disjoint);
}

TEST(SegmentIntersect, SharedEndpoint) {
	auto r = intersectSegments({0, 0}, {100, 0}, {100, 0}, {100, 100});
	EXPECT_EQ(r.relation, SegmentRelation::EndpointTouch);
	EXPECT_EQ(r.point, (Vec2i64{100, 0}));
}

TEST(SegmentIntersect, TTouch) {
	// Endpoint of one segment lies in the interior of the other.
	auto r = intersectSegments({0, 0}, {100, 0}, {50, 0}, {50, 100});
	EXPECT_EQ(r.relation, SegmentRelation::EndpointTouch);
	EXPECT_EQ(r.point, (Vec2i64{50, 0}));
}

TEST(SegmentIntersect, CollinearPartialOverlap) {
	auto r = intersectSegments({0, 0}, {100, 0}, {50, 0}, {150, 0});
	EXPECT_EQ(r.relation, SegmentRelation::CollinearOverlap);
	EXPECT_EQ(r.overlapStart, (Vec2i64{50, 0}));
	EXPECT_EQ(r.overlapEnd, (Vec2i64{100, 0}));
}

TEST(SegmentIntersect, CollinearTotalContainment) {
	auto r = intersectSegments({0, 0}, {100, 0}, {25, 0}, {75, 0});
	EXPECT_EQ(r.relation, SegmentRelation::CollinearOverlap);
	EXPECT_EQ(r.overlapStart, (Vec2i64{25, 0}));
	EXPECT_EQ(r.overlapEnd, (Vec2i64{75, 0}));
}

TEST(SegmentIntersect, CollinearTouchAtSinglePoint) {
	// Two collinear segments meeting at exactly one point: an endpoint touch.
	auto r = intersectSegments({0, 0}, {100, 0}, {100, 0}, {200, 0});
	EXPECT_EQ(r.relation, SegmentRelation::EndpointTouch);
	EXPECT_EQ(r.point, (Vec2i64{100, 0}));
}

TEST(SegmentIntersect, CollinearVertical) {
	auto r = intersectSegments({0, 0}, {0, 100}, {0, 50}, {0, 150});
	EXPECT_EQ(r.relation, SegmentRelation::CollinearOverlap);
	EXPECT_EQ(r.overlapStart, (Vec2i64{0, 50}));
	EXPECT_EQ(r.overlapEnd, (Vec2i64{0, 100}));
}

namespace {

	// CCW unit-square ring, side 100.
	std::vector<Vec2i64> square() {
		return {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
	}

	// Concave "arrow" / chevron polygon (CCW).
	std::vector<Vec2i64> concave() {
		return {{0, 0}, {100, 50}, {0, 100}, {30, 50}};
	}

} // namespace

TEST(PointInPolygon, InsideOutside) {
	EXPECT_EQ(pointInPolygon({50, 50}, square()), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon({150, 50}, square()), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon({-1, 50}, square()), PointInPolygon::Outside);
}

TEST(PointInPolygon, OnVertex) {
	EXPECT_EQ(pointInPolygon({0, 0}, square()), PointInPolygon::OnBoundary);
	EXPECT_EQ(pointInPolygon({100, 100}, square()), PointInPolygon::OnBoundary);
}

TEST(PointInPolygon, OnEdge) {
	EXPECT_EQ(pointInPolygon({50, 0}, square()), PointInPolygon::OnBoundary);
	EXPECT_EQ(pointInPolygon({100, 50}, square()), PointInPolygon::OnBoundary);
}

TEST(PointInPolygon, RayThroughVertex) {
	// A horizontal ray from (50,50) passes through vertices; the half-open rule
	// must still classify interior/exterior correctly.
	std::vector<Vec2i64> diamond = {{50, 0}, {100, 50}, {50, 100}, {0, 50}};
	EXPECT_EQ(pointInPolygon({50, 50}, diamond), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon({-10, 50}, diamond), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon({120, 50}, diamond), PointInPolygon::Outside);
}

TEST(PointInPolygon, Concave) {
	// concave() is a right-pointing arrowhead with an inward notch at (30,50).
	EXPECT_EQ(pointInPolygon({60, 50}, concave()), PointInPolygon::Inside);
	EXPECT_EQ(pointInPolygon({50, 40}, concave()), PointInPolygon::Inside);
	// (10,50) sits left of the notch tip, inside the concavity -> outside.
	EXPECT_EQ(pointInPolygon({10, 50}, concave()), PointInPolygon::Outside);
	EXPECT_EQ(pointInPolygon({-5, 50}, concave()), PointInPolygon::Outside);
}

TEST(PointInPolygon, DegenerateRing) {
	std::vector<Vec2i64> tooFew = {{0, 0}, {10, 0}};
	EXPECT_EQ(pointInPolygon({5, 0}, tooFew), PointInPolygon::Outside);
}

TEST(Distance, EndpointRegionsExact) {
	// Point projects beyond endpoint a: nearest is a itself.
	auto d = squaredDistanceToSegment({-30, 40}, {0, 0}, {100, 0});
	ASSERT_TRUE(d.has_value());
	EXPECT_EQ(*d, dot(Vec2i64{-30, 40}, Vec2i64{-30, 40})); // 900 + 1600
}

TEST(Distance, InteriorIsRational) {
	// Foot of perpendicular is interior: exact integer mm^2 unavailable.
	auto d = squaredDistanceToSegment({50, 30}, {0, 0}, {100, 0});
	EXPECT_FALSE(d.has_value());
}

TEST(Distance, WithinThresholdInterior) {
	// Perpendicular distance from (50,30) to the x-axis segment is 30.
	EXPECT_TRUE(withinDistanceOfSegment({50, 30}, {0, 0}, {100, 0}, 30));
	EXPECT_TRUE(withinDistanceOfSegment({50, 30}, {0, 0}, {100, 0}, 31));
	EXPECT_FALSE(withinDistanceOfSegment({50, 30}, {0, 0}, {100, 0}, 29));
}

TEST(Distance, ExactlyAtThreshold) {
	// Endpoint region: distance from (-3,4) to a=(0,0) is exactly 5.
	EXPECT_TRUE(withinDistanceOfSegment({-3, 4}, {0, 0}, {100, 0}, 5));
	EXPECT_FALSE(withinDistanceOfSegment({-3, 4}, {0, 0}, {100, 0}, 4));
}

TEST(Distance, DegenerateSegment) {
	EXPECT_TRUE(withinDistanceOfSegment({3, 4}, {0, 0}, {0, 0}, 5));
	EXPECT_FALSE(withinDistanceOfSegment({3, 4}, {0, 0}, {0, 0}, 4));
}

TEST(Distance, FloatConvenience) {
	EXPECT_NEAR(distanceToSegment({50, 30}, {0, 0}, {100, 0}), 30.0, 1e-9);
	EXPECT_NEAR(distanceToSegment({-3, 4}, {0, 0}, {100, 0}), 5.0, 1e-9);
}

TEST(OracleSweep, OrientationVsDouble) {
	std::mt19937 rng(0xC0FFEE);
	std::uniform_int_distribution<std::int64_t> coord(-50, 50);
	for (int iter = 0; iter < 8000; ++iter) {
		Vec2i64 a{coord(rng), coord(rng)};
		Vec2i64 b{coord(rng), coord(rng)};
		Vec2i64 c{coord(rng), coord(rng)};
		const int got = orientation(a, b, c) == Orientation::CounterClockwise
							? 1
							: (orientation(a, b, c) == Orientation::Clockwise ? -1 : 0);
		ASSERT_EQ(got, orientationOracle(a, b, c)) << a.x << "," << a.y << " " << b.x << "," << b.y << " " << c.x << ","
												   << c.y;
	}
}

TEST(OracleSweep, WithinDistanceVsDouble) {
	std::mt19937 rng(0xBADF00D);
	std::uniform_int_distribution<std::int64_t> coord(-50, 50);
	std::uniform_int_distribution<std::int64_t> thr(0, 70);
	for (int iter = 0; iter < 8000; ++iter) {
		Vec2i64		 p{coord(rng), coord(rng)};
		Vec2i64		 a{coord(rng), coord(rng)};
		Vec2i64		 b{coord(rng), coord(rng)};
		std::int64_t t = thr(rng);
		const bool	 got = withinDistanceOfSegment(p, a, b, t);
		// Exact threshold comparison in double: distSq <= t^2. At this scale both
		// sides are exact integers in double, so the comparison is exact too.
		const double distSq	  = segDistSqOracle(p, a, b);
		const double threshSq = static_cast<double>(t) * static_cast<double>(t);
		const bool	 expected = distSq <= threshSq;
		ASSERT_EQ(got, expected) << "p=" << p.x << "," << p.y << " a=" << a.x << "," << a.y << " b=" << b.x << "," << b.y
								 << " t=" << t << " distSq=" << distSq << " threshSq=" << threshSq;
	}
}

TEST(OracleSweep, PointInPolygonVsDouble) {
	// Fixed adversarial rings plus random query points. Convex and concave, with
	// vertices and edge-grazing points exercised by the dense small grid.
	const std::vector<std::vector<Vec2i64>> rings = {
		{{0, 0}, {10, 0}, {10, 10}, {0, 10}},				   // square
		{{0, 0}, {20, 0}, {20, 6}, {8, 6}, {8, 14}, {0, 14}},  // L-shape (concave)
		{{0, 0}, {10, 4}, {20, 0}, {15, 10}, {5, 10}},		   // pentagon
	};
	for (const auto& ring : rings) {
		for (std::int64_t y = -3; y <= 23; ++y) {
			for (std::int64_t x = -3; x <= 23; ++x) {
				Vec2i64 p{x, y};
				const PointInPolygon got = pointInPolygon(p, ring);
				const int gv = got == PointInPolygon::Inside ? 1 : (got == PointInPolygon::OnBoundary ? 0 : -1);
				ASSERT_EQ(gv, pipOracle(p, ring)) << "p=" << x << "," << y << " ringSize=" << ring.size();
			}
		}
	}
}

TEST(OracleSweep, IntersectClassificationVsBruteForce) {
	// Classify every ordered pair of segments over a tiny grid against an
	// independent brute-force relation built from orientation signs and overlap.
	std::mt19937 rng(0x5EED);
	std::uniform_int_distribution<std::int64_t> coord(-6, 6);
	auto onSeg = [](const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
		return orientationOracle(a, b, p) == 0 && p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) &&
			   p.y >= std::min(a.y, b.y) && p.y <= std::max(a.y, b.y);
	};
	for (int iter = 0; iter < 20000; ++iter) {
		Vec2i64 a0{coord(rng), coord(rng)};
		Vec2i64 a1{coord(rng), coord(rng)};
		Vec2i64 b0{coord(rng), coord(rng)};
		Vec2i64 b1{coord(rng), coord(rng)};
		if (a0 == a1 || b0 == b1) {
			continue; // degenerate input not in the predicate's contract
		}
		const SegmentRelation rel = intersectSegments(a0, a1, b0, b1).relation;

		const int d1 = orientationOracle(b0, b1, a0);
		const int d2 = orientationOracle(b0, b1, a1);
		const int d3 = orientationOracle(a0, a1, b0);
		const int d4 = orientationOracle(a0, a1, b1);
		const bool allCollinear = d1 == 0 && d2 == 0 && d3 == 0 && d4 == 0;

		if (allCollinear) {
			// Count distinct shared points along the line.
			bool touchA0 = onSeg(a0, b0, b1);
			bool touchA1 = onSeg(a1, b0, b1);
			bool touchB0 = onSeg(b0, a0, a1);
			bool touchB1 = onSeg(b1, a0, a1);
			// Collinear overlap iff the 1D intervals overlap in more than a point.
			const bool useX = std::abs(a1.x - a0.x) >= std::abs(a1.y - a0.y);
			auto co = [useX](const Vec2i64& p) { return useX ? p.x : p.y; };
			std::int64_t lo = std::max(std::min(co(a0), co(a1)), std::min(co(b0), co(b1)));
			std::int64_t hi = std::min(std::max(co(a0), co(a1)), std::max(co(b0), co(b1)));
			if (lo > hi) {
				ASSERT_EQ(rel, SegmentRelation::Disjoint);
			} else if (lo == hi) {
				ASSERT_EQ(rel, SegmentRelation::EndpointTouch);
			} else {
				ASSERT_EQ(rel, SegmentRelation::CollinearOverlap);
			}
			(void)touchA0; (void)touchA1; (void)touchB0; (void)touchB1;
			continue;
		}

		const bool properStraddle =
			((d1 > 0) != (d2 > 0)) && d1 != 0 && d2 != 0 && ((d3 > 0) != (d4 > 0)) && d3 != 0 && d4 != 0;
		if (properStraddle) {
			ASSERT_EQ(rel, SegmentRelation::ProperCrossing);
			continue;
		}

		const bool endpointOnOther = (d1 == 0 && onSeg(a0, b0, b1)) || (d2 == 0 && onSeg(a1, b0, b1)) ||
									 (d3 == 0 && onSeg(b0, a0, a1)) || (d4 == 0 && onSeg(b1, a0, a1));
		if (endpointOnOther) {
			ASSERT_EQ(rel, SegmentRelation::EndpointTouch)
				<< "a0=" << a0.x << "," << a0.y << " a1=" << a1.x << "," << a1.y << " b0=" << b0.x << "," << b0.y
				<< " b1=" << b1.x << "," << b1.y;
		} else {
			ASSERT_EQ(rel, SegmentRelation::Disjoint)
				<< "a0=" << a0.x << "," << a0.y << " a1=" << a1.x << "," << a1.y << " b0=" << b0.x << "," << b0.y
				<< " b1=" << b1.x << "," << b1.y;
		}
	}
}
