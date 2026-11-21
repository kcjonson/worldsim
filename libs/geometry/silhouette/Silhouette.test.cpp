#include "Silhouette.h"

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Coordinates are in millimetres.

	bool insideAny(const std::vector<Ring>& outline, Vec2i64 point) {
		for (const Ring& ring : outline) {
			if (pointInPolygon(point, ring) == PointInPolygon::Inside) {
				return true;
			}
		}
		return false;
	}

	double totalArea(const std::vector<Ring>& outline) {
		double area = 0.0;
		for (const Ring& ring : outline) {
			area += std::abs(signedAreaDoubled(ring).toDouble()) * 0.5;
		}
		return area;
	}

	// Appends the two triangles of an axis-aligned rectangle, the second wound clockwise.
	void pushRect(std::vector<Vec2i64>& vertices, std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		vertices.insert(vertices.end(), {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x0, y1}, {x1, y1}});
	}

	void expectSimpleAndCounterClockwise(const std::vector<Ring>& outline) {
		for (const Ring& ring : outline) {
			EXPECT_TRUE(isSimple(ring).pass);
			EXPECT_GT(signedAreaDoubled(ring).sign(), 0);
		}
	}

} // namespace

TEST(Silhouette, SquareOfTwoTrianglesIsExact) {
	std::vector<Vec2i64> square;
	pushRect(square, 0, 0, 4000, 4000);
	const auto outline = silhouetteOfTriangles(square, 0, 1);
	ASSERT_EQ(outline.size(), 1U);
	EXPECT_EQ(outline.front().size(), 4U);
	EXPECT_DOUBLE_EQ(totalArea(outline), 16'000'000.0);
	expectSimpleAndCounterClockwise(outline);
}

TEST(Silhouette, LShapeKeepsItsNotch) {
	std::vector<Vec2i64> shape;
	pushRect(shape, 0, 0, 3000, 1000);
	pushRect(shape, 0, 1000, 1000, 3000);
	const auto outline = silhouetteOfTriangles(shape, 5, 5);
	ASSERT_EQ(outline.size(), 1U);
	EXPECT_EQ(outline.front().size(), 6U);
	// The close rounds the inner corner and the simplification may lean an edge by up to its tolerance.
	EXPECT_NEAR(totalArea(outline), 5'000'000.0, 5.0 * 3000.0);
	EXPECT_FALSE(insideAny(outline, {2000, 2000}));
}

TEST(Silhouette, SeparateBodiesStaySeparateUntilClosed) {
	std::vector<Vec2i64> two;
	pushRect(two, 0, 0, 4000, 6000);
	pushRect(two, 4006, 0, 8000, 6000); // a 6 mm gap
	const auto open = silhouetteOfTriangles(two, 0, 1);
	ASSERT_EQ(open.size(), 2U);
	EXPECT_FALSE(insideAny(open, {4003, 3000}));

	const auto closed = silhouetteOfTriangles(two, 4, 1);
	ASSERT_EQ(closed.size(), 1U);
	EXPECT_TRUE(insideAny(closed, {4003, 3000}));
	EXPECT_NEAR(totalArea(closed), 48'000'000.0, 48'000'000.0 * 0.001) << "the close keeps the outer boundary";
	expectSimpleAndCounterClockwise(closed);

	const auto wide = silhouetteOfTriangles(two, 2, 1);
	EXPECT_EQ(wide.size(), 2U) << "a gap past twice the radius stays open";
}

TEST(Silhouette, HoleIsFilled) {
	std::vector<Vec2i64> frame;
	pushRect(frame, 0, 0, 6000, 2000);
	pushRect(frame, 0, 4000, 6000, 6000);
	pushRect(frame, 0, 2000, 2000, 4000);
	pushRect(frame, 4000, 2000, 6000, 4000);
	const auto outline = silhouetteOfTriangles(frame, 0, 1);
	ASSERT_EQ(outline.size(), 1U);
	EXPECT_TRUE(insideAny(outline, {3000, 3000}));
	EXPECT_DOUBLE_EQ(totalArea(outline), 36'000'000.0);
}

TEST(Silhouette, IslandInAHoleGoesWithTheFill) {
	std::vector<Vec2i64> frame;
	pushRect(frame, 0, 0, 6000, 2000);
	pushRect(frame, 0, 4000, 6000, 6000);
	pushRect(frame, 0, 2000, 2000, 4000);
	pushRect(frame, 4000, 2000, 6000, 4000);
	pushRect(frame, 2500, 2500, 3500, 3500);
	EXPECT_EQ(silhouetteOfTriangles(frame, 0, 1).size(), 1U);
}

TEST(Silhouette, DegenerateAndDuplicateInputs) {
	EXPECT_TRUE(silhouetteOfTriangles({}, 4, 1).empty());
	EXPECT_TRUE(silhouetteOfTriangles({{0, 0}, {1000, 0}, {2000, 0}}, 4, 1).empty());
	EXPECT_TRUE(silhouetteOfTriangles({{5, 5}, {5, 5}, {5, 5}}, 4, 1).empty());

	std::vector<Vec2i64> once;
	pushRect(once, 0, 0, 3000, 2000);
	std::vector<Vec2i64> repeated = once;
	repeated.insert(repeated.end(), once.begin(), once.end());
	repeated.insert(repeated.end(), {{0, 0}, {3000, 0}, {3000, 0}}); // a triangle with a repeated corner
	repeated.insert(repeated.end(), {{0, 0}, {1000, 0}, {2000, 0}});
	EXPECT_EQ(silhouetteOfTriangles(repeated, 4, 1), silhouetteOfTriangles(once, 4, 1));
}

TEST(Silhouette, ConvexDilationHoldsEveryPointWithinTheRadius) {
	const Ring grown = dilateConvex({{0, 0}, {1000, 0}, {1000, 1000}, {0, 1000}}, 100);
	for (const Vec2i64 point : {Vec2i64{-99, 500}, Vec2i64{500, 1099}, Vec2i64{-70, -70}, Vec2i64{1070, 1070}}) {
		EXPECT_TRUE(insideAny({grown}, point)) << point.x << ", " << point.y;
	}
	EXPECT_FALSE(insideAny({grown}, {-110, 500}));
	EXPECT_FALSE(insideAny({grown}, {-90, -90}));
}

namespace {

	double distanceOutside(const std::vector<Ring>& outline, Vec2i64 point) {
		double nearest = 1e18;
		for (const Ring& ring : outline) {
			if (pointInPolygon(point, ring) != PointInPolygon::Outside) {
				return 0.0;
			}
			for (std::size_t i = 0; i < ring.size(); ++i) {
				const Vec2i64 from = ring[i];
				const Vec2i64 to = ring[(i + 1) % ring.size()];
				const double  alongX = static_cast<double>(to.x - from.x);
				const double  alongY = static_cast<double>(to.y - from.y);
				const double  fraction = std::clamp(
					 (static_cast<double>(point.x - from.x) * alongX + static_cast<double>(point.y - from.y) * alongY) / (alongX * alongX + alongY * alongY), 0.0, 1.0
				 );
				nearest = std::min(nearest, std::hypot(static_cast<double>(point.x - from.x) - fraction * alongX, static_cast<double>(point.y - from.y) - fraction * alongY));
			}
		}
		return nearest;
	}

	void expectEveryVertexWithinBound(const std::vector<Vec2i64>& triangles, const std::vector<Ring>& outline, std::int64_t tolerance) {
		const double bound = static_cast<double>(silhouetteBoundMm(triangles.size() / 3, tolerance));
		for (const Vec2i64& vertex : triangles) {
			EXPECT_LE(distanceOutside(outline, vertex), bound) << "(" << vertex.x << ", " << vertex.y << ")";
		}
	}

} // namespace

// A 6 m body with a thin curved strand (a 10 mm band round a quarter circle) reaching off it and a separate
// 300 by 10 mm stroke beside it, at the tolerance a 6 m object gets (L / 512): nothing drawn is lost.
TEST(Silhouette, ThinStrandsAndThinBodiesStay) {
	constexpr std::int64_t kTolerance = 6000 / 512;
	std::vector<Vec2i64>   triangles;
	pushRect(triangles, 0, 0, 6000, 6000);
	constexpr int kSteps = 48;
	for (int step = 0; step < kSteps; ++step) {
		const auto pointAt = [](double angle, double radius) {
			return Vec2i64{6000 + std::llround(radius * std::cos(angle)), 3000 + std::llround(radius * std::sin(angle))};
		};
		const double from = std::numbers::pi * 0.5 * step / kSteps - std::numbers::pi * 0.25;
		const double to = std::numbers::pi * 0.5 * (step + 1) / kSteps - std::numbers::pi * 0.25;
		const Vec2i64 innerFrom = pointAt(from, 1500);
		const Vec2i64 outerFrom = pointAt(from, 1510);
		const Vec2i64 innerTo = pointAt(to, 1500);
		const Vec2i64 outerTo = pointAt(to, 1510);
		triangles.insert(triangles.end(), {innerFrom, outerFrom, outerTo, innerFrom, outerTo, innerTo});
	}
	pushRect(triangles, 0, 7000, 300, 7010);
	const auto outline = silhouetteOfTriangles(triangles, kTolerance, kTolerance);
	expectSimpleAndCounterClockwise(outline);
	EXPECT_TRUE(insideAny(outline, {150, 7005}) || distanceOutside(outline, {150, 7005}) <= kTolerance) << "the separate stroke is outlined";
	expectEveryVertexWithinBound(triangles, outline, kTolerance);
}