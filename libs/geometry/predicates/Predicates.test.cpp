#include "Predicates.h"
#include "../core/Vec2i64.h"

#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

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
	// Cross at (333.33.., 333.33..): rounds to nearest mm = 333.
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
