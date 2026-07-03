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

	Ring squareCcw(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
	}

	Ring squareCw(std::int64_t x0, std::int64_t y0, std::int64_t x1, std::int64_t y1) {
		return {{x0, y0}, {x0, y1}, {x1, y1}, {x1, y0}};
	}

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

} // namespace

TEST(Silhouette, SingleSquareOneRing) {
	const auto s = silhouetteOfRings({squareCcw(0, 0, 4000, 4000)});
	ASSERT_EQ(s.size(), 1u);
	EXPECT_TRUE(insideAny(s, {2000, 2000}));
	EXPECT_FALSE(insideAny(s, {9000, 9000}));
	expectNear(totalArea(s), 16'000'000.0, 0.10);
}

TEST(Silhouette, DonutHoleIsFilled) {
	// Outer CCW square, inner CW square (punches under nonzero) -> hole fills.
	const auto s = silhouetteOfRings({squareCcw(0, 0, 4000, 4000), squareCw(1200, 1200, 2800, 2800)});
	ASSERT_EQ(s.size(), 1u);
	EXPECT_TRUE(insideAny(s, {2000, 2000})); // center of the (filled) hole
	expectNear(totalArea(s), 16'000'000.0, 0.10);
}

TEST(Silhouette, DisjointSquaresTwoRings) {
	const auto s = silhouetteOfRings({squareCcw(0, 0, 2000, 2000), squareCcw(4000, 0, 6000, 2000)});
	ASSERT_EQ(s.size(), 2u);
	EXPECT_TRUE(insideAny(s, {1000, 1000}));
	EXPECT_TRUE(insideAny(s, {5000, 1000}));
	EXPECT_FALSE(insideAny(s, {3000, 1000})); // the gap
}

TEST(Silhouette, ConcaveLShapeNotchIsOutside) {
	const Ring l = {{0, 0}, {3000, 0}, {3000, 1000}, {1000, 1000}, {1000, 3000}, {0, 3000}};
	const auto s = silhouetteOfRings({l});
	ASSERT_EQ(s.size(), 1u);
	EXPECT_FALSE(insideAny(s, {2000, 2000})); // deep in the concave notch
	EXPECT_TRUE(insideAny(s, {500, 2000}));	  // vertical arm
	EXPECT_TRUE(insideAny(s, {2000, 500}));	  // horizontal arm
}

TEST(Silhouette, NestedSolidSameWindingOneRing) {
	// Inner square same winding, fully inside: interior is doubly covered, so no
	// inner boundary is reported.
	const auto s = silhouetteOfRings({squareCcw(0, 0, 4000, 4000), squareCcw(1200, 1200, 2800, 2800)});
	ASSERT_EQ(s.size(), 1u);
	EXPECT_TRUE(insideAny(s, {2000, 2000}));
}

TEST(Silhouette, OverlappingSquaresMerge) {
	const auto s = silhouetteOfRings({squareCcw(0, 0, 3000, 3000), squareCcw(2000, 2000, 5000, 5000)});
	ASSERT_EQ(s.size(), 1u);
	EXPECT_TRUE(insideAny(s, {1000, 1000}));
	EXPECT_TRUE(insideAny(s, {4000, 4000}));
}

TEST(Silhouette, SelfIntersectingFigureEight) {
	// One ring pinched at (0,0) into two CCW lobes; both fill under nonzero.
	const Ring eight = {
		{0, 0}, {1000, -500}, {2000, 0}, {1000, 500}, {0, 0}, {-1000, 500}, {-2000, 0}, {-1000, -500},
	};
	const auto s = silhouetteOfRings({eight});
	ASSERT_GE(s.size(), 1u);
	EXPECT_TRUE(insideAny(s, {1000, 0}));  // right lobe
	EXPECT_TRUE(insideAny(s, {-1000, 0})); // left lobe
}

TEST(Silhouette, EmptyAndDegenerateInputs) {
	EXPECT_TRUE(silhouetteOfRings({}).empty());
	EXPECT_TRUE(silhouetteOfRings({{{0, 0}, {1000, 0}}}).empty()); // 2-vertex, size < 3
	EXPECT_TRUE(silhouetteOfRings({{{5, 5}, {5, 5}, {5, 5}}}).empty()); // zero-extent bbox
}

namespace {

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
