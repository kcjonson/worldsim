#include "Silhouette.h"

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"
#include "../predicates/Predicates.h"

#include <cmath>
#include <cstdint>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	// Coordinates are in millimeters; 1000 mm = 1 m. Raster silhouette output is
	// blocky and approximate, so tests assert topology and area within a few %,
	// never exact vertices. Probe points sit well away from every boundary so
	// raster staircasing cannot flip an inside/outside answer.

	bool insideAny(const std::vector<Ring>& s, Vec2i64 p) {
		for (const Ring& r : s) {
			if (pointInPolygon(p, r) == PointInPolygon::Inside) {
				return true;
			}
		}
		return false;
	}

	double totalArea(const std::vector<Ring>& s) {
		double a = 0.0;
		for (const Ring& r : s) {
			a += std::abs(signedAreaDoubled(r).toDouble()) * 0.5;
		}
		return a;
	}

	void expectNear(double got, double want, double tol) {
		EXPECT_LE(std::abs(got - want), tol * want) << "got " << got << " want " << want;
	}

	// Appends the two triangles of an axis-aligned rectangle.
	void pushRect(std::vector<Vec2i64>& v, std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		v.insert(v.end(), {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y0}, {x1, y1}, {x0, y1}});
	}

} // namespace

TEST(Silhouette, SingleTriangleOneRing) {
	const std::vector<Vec2i64> tri = {{0, 0}, {4000, 0}, {0, 4000}};
	const auto				   s   = silhouetteOfTriangles(tri);
	ASSERT_EQ(s.size(), 1u);
	EXPECT_TRUE(insideAny(s, {1333, 1333})); // centroid
	EXPECT_FALSE(insideAny(s, {3000, 3000})); // outside the hypotenuse
	expectNear(totalArea(s), 8'000'000.0, 0.15);
}

TEST(Silhouette, TwoTrianglesFormSquare) {
	// Shared diagonal (0,0)-(4000,4000); the union is a solid square.
	const std::vector<Vec2i64> tris = {
		{0, 0}, {4000, 0}, {4000, 4000}, // lower-right
		{0, 0}, {4000, 4000}, {0, 4000}, // upper-left
	};
	const auto s = silhouetteOfTriangles(tris);
	ASSERT_EQ(s.size(), 1u);
	EXPECT_TRUE(insideAny(s, {2000, 2000}));
	expectNear(totalArea(s), 16'000'000.0, 0.10);
}

TEST(Silhouette, TriangulatedAnnulusHoleFilled) {
	// Square frame (outer 6000, inner hole 2000..4000) as four strips of triangles;
	// the triangular/quad hole in the middle must fill to a single ring.
	std::vector<Vec2i64> tris;
	pushRect(tris, 0, 0, 6000, 2000);	 // bottom
	pushRect(tris, 0, 4000, 6000, 6000); // top
	pushRect(tris, 0, 2000, 2000, 4000); // left
	pushRect(tris, 4000, 2000, 6000, 4000); // right
	const auto s = silhouetteOfTriangles(tris);
	ASSERT_EQ(s.size(), 1u);
	EXPECT_TRUE(insideAny(s, {3000, 3000})); // filled hole center
}

TEST(Silhouette, CloseBridgesDisjointBlobs) {
	std::vector<Vec2i64> two;
	pushRect(two, 0, 0, 4000, 6000);	  // A
	pushRect(two, 6000, 0, 10000, 6000);  // B, 2000mm gap between facing walls

	// No close: two separate rings, gap is exterior.
	const auto open = silhouetteOfTriangles(two, 128, 0);
	ASSERT_EQ(open.size(), 2u);
	EXPECT_TRUE(insideAny(open, {2000, 3000}));
	EXPECT_TRUE(insideAny(open, {8000, 3000}));
	EXPECT_FALSE(insideAny(open, {5000, 3000})); // the gap

	// Close wide enough to bridge the 2000mm gap: one ring, gap now inside.
	const auto closed = silhouetteOfTriangles(two, 128, 20);
	ASSERT_EQ(closed.size(), 1u);
	EXPECT_TRUE(insideAny(closed, {5000, 3000}));
}

TEST(Silhouette, EmptyAndDegenerateInputs) {
	EXPECT_TRUE(silhouetteOfTriangles({}).empty());
	// A single zero-area (collinear) triangle: no coverage -> no rings.
	EXPECT_TRUE(silhouetteOfTriangles({{0, 0}, {1000, 0}, {2000, 0}}).empty());
	// Zero-extent bbox (all verts coincident).
	EXPECT_TRUE(silhouetteOfTriangles({{5, 5}, {5, 5}, {5, 5}}).empty());
}
