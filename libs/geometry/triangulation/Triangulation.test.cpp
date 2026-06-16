#include "Triangulation.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <utility>
#include <vector>
#include <gtest/gtest.h>

using namespace geometry;

namespace {

	using Tri = std::array<std::uint32_t, 3>;

	// Doubled signed area of a triangle (CCW positive), exact in 128-bit.
	Int128 triArea2(const std::vector<Vec2i64>& v, const Tri& t) {
		return cross(v[t[1]] - v[t[0]], v[t[2]] - v[t[0]]);
	}

	// Sum of doubled areas of a triangle list (all CCW => positive total).
	Int128 totalArea2(const std::vector<Vec2i64>& v, const std::vector<Tri>& tris) {
		Int128 acc(0);
		for (const Tri& t : tris) {
			acc = acc + triArea2(v, t);
		}
		return acc;
	}

	// Doubled signed area of a ring of vertex indices.
	Int128 ringArea2(const std::vector<Vec2i64>& v, const std::vector<std::uint32_t>& ring) {
		Int128			  acc(0);
		const std::size_t n = ring.size();
		for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
			acc = acc + cross(v[ring[j]], v[ring[i]]);
		}
		return acc;
	}

	bool allCcw(const std::vector<Vec2i64>& v, const std::vector<Tri>& tris) {
		for (const Tri& t : tris) {
			if (orientation(v[t[0]], v[t[1]], v[t[2]]) != Orientation::CounterClockwise) {
				return false;
			}
		}
		return true;
	}

	// Integer centroid of a triangle (exact mean is rational; the floored mean is
	// strictly interior for the well-shaped triangles these tests produce).
	Vec2i64 centroid(const std::vector<Vec2i64>& v, const Tri& t) {
		return {(v[t[0]].x + v[t[1]].x + v[t[2]].x) / 3, (v[t[0]].y + v[t[1]].y + v[t[2]].y) / 3};
	}

	// Collect undirected triangle edges as a multiset of ordered (min,max) pairs.
	std::set<std::pair<std::uint32_t, std::uint32_t>> triangleEdgeSet(const std::vector<Tri>& tris) {
		std::set<std::pair<std::uint32_t, std::uint32_t>> edges;
		for (const Tri& t : tris) {
			for (int e = 0; e < 3; ++e) {
				std::uint32_t a = t[e];
				std::uint32_t b = t[(e + 1) % 3];
				if (a > b) {
					std::swap(a, b);
				}
				edges.insert({a, b});
			}
		}
		return edges;
	}

	bool hasEdge(const std::set<std::pair<std::uint32_t, std::uint32_t>>& edges, std::uint32_t a, std::uint32_t b) {
		if (a > b) {
			std::swap(a, b);
		}
		return edges.count({a, b}) > 0;
	}

	// For each interior edge (shared by exactly two triangles), assert the opposite
	// vertex of the neighbor is not strictly inside the triangle's circumcircle:
	// the local-Delaunay condition. Boundary edges are skipped.
	bool isLocallyDelaunay(const std::vector<Vec2i64>& v, const std::vector<Tri>& tris) {
		// Map undirected edge -> list of (triIndex, oppositeVertex).
		std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<std::pair<std::size_t, std::uint32_t>>> edgeMap;
		for (std::size_t ti = 0; ti < tris.size(); ++ti) {
			const Tri& t = tris[ti];
			for (int e = 0; e < 3; ++e) {
				std::uint32_t a	  = t[e];
				std::uint32_t b	  = t[(e + 1) % 3];
				std::uint32_t opp = t[(e + 2) % 3];
				if (a > b) {
					std::swap(a, b);
				}
				edgeMap[{a, b}].push_back({ti, opp});
			}
		}
		for (const auto& kv : edgeMap) {
			if (kv.second.size() != 2) {
				continue; // boundary edge
			}
			const Tri&	  t	  = tris[kv.second[0].first];
			std::uint32_t d	  = kv.second[1].second;
			Vec2i64		  A	  = v[t[0]];
			Vec2i64		  B	  = v[t[1]];
			Vec2i64		  C	  = v[t[2]];
			if (orientation(A, B, C) == Orientation::Clockwise) {
				std::swap(A, B);
			}
			if (inCircle(A, B, C, v[d]) == InCircle::Inside) {
				return false;
			}
		}
		return true;
	}

	std::vector<std::uint32_t> iota(std::uint32_t n) {
		std::vector<std::uint32_t> r(n);
		for (std::uint32_t i = 0; i < n; ++i) {
			r[i] = i;
		}
		return r;
	}

	// CCW convex hull of a point set (Andrew's monotone chain, exact integer).
	// Returns fewer than 3 points only for degenerate/collinear input.
	std::vector<Vec2i64> convexHullCcw(std::vector<Vec2i64> pts) {
		std::sort(pts.begin(), pts.end());
		pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
		if (pts.size() < 3) {
			return pts;
		}
		auto cross2 = [](const Vec2i64& o, const Vec2i64& a, const Vec2i64& b) {
			return cross(a - o, b - o).sign();
		};
		std::vector<Vec2i64> hull;
		for (const Vec2i64& p : pts) { // lower
			while (hull.size() >= 2 && cross2(hull[hull.size() - 2], hull.back(), p) <= 0) {
				hull.pop_back();
			}
			hull.push_back(p);
		}
		const std::size_t lower = hull.size() + 1;
		for (std::size_t i = pts.size(); i-- > 0;) { // upper
			const Vec2i64& p = pts[i];
			while (hull.size() >= lower && cross2(hull[hull.size() - 2], hull.back(), p) <= 0) {
				hull.pop_back();
			}
			hull.push_back(p);
		}
		hull.pop_back(); // last point repeats the first
		return hull;
	}

} // namespace

TEST(Triangulation, SquareTwoTriangles) {
	std::vector<Vec2i64> v = {{0, 0}, {100, 0}, {100, 100}, {0, 100}};
	auto tris = triangulateSimple(v, iota(4));
	ASSERT_EQ(tris.size(), 2u);
	EXPECT_TRUE(allCcw(v, tris));
	// Union covers the square: total doubled area == 2 * 100 * 100 = 20000.
	EXPECT_EQ(totalArea2(v, tris), Int128(20000));
	EXPECT_EQ(totalArea2(v, tris), ringArea2(v, iota(4)));
}

TEST(Triangulation, LShapeConcave) {
	// Concave L (CCW): 6 vertices -> 4 triangles, none outside the polygon.
	std::vector<Vec2i64> v = {{0, 0}, {200, 0}, {200, 60}, {80, 60}, {80, 140}, {0, 140}};
	auto				 ring = iota(6);
	auto				 tris = triangulateSimple(v, ring);
	ASSERT_EQ(tris.size(), 4u);
	EXPECT_TRUE(allCcw(v, tris));
	std::vector<Vec2i64> poly = {v[0], v[1], v[2], v[3], v[4], v[5]};
	for (const Tri& t : tris) {
		EXPECT_EQ(pointInPolygon(centroid(v, t), poly), PointInPolygon::Inside);
	}
	// Area conservation.
	EXPECT_EQ(totalArea2(v, tris), ringArea2(v, ring));
}

TEST(Triangulation, SimplePolygonTriangleCount) {
	// A simple n-gon yields n-2 triangles. Pentagon (convex).
	std::vector<Vec2i64> v = {{0, 0}, {100, 0}, {140, 80}, {50, 140}, {-30, 70}};
	auto				 tris = triangulateSimple(v, iota(5));
	EXPECT_EQ(tris.size(), 3u);
	EXPECT_TRUE(allCcw(v, tris));
	EXPECT_EQ(totalArea2(v, tris), ringArea2(v, iota(5)));
}

TEST(Triangulation, AnnulusOneHole) {
	// Outer square (CCW) + inner square hole (CW). Ring count: n=4, holeVerts=4,
	// h=1 -> (4 + 4 + 2*1 - 2) = 8 triangles.
	std::vector<Vec2i64> v = {
		{0, 0}, {300, 0}, {300, 300}, {0, 300},		   // outer (CCW), indices 0..3
		{100, 100}, {100, 200}, {200, 200}, {200, 100} // inner hole, CW, indices 4..7
	};
	std::vector<std::uint32_t>				   outer = {0, 1, 2, 3};
	std::vector<std::vector<std::uint32_t>>	   holes = {{4, 5, 6, 7}};
	auto									   tris	 = triangulateWithHoles(v, outer, holes);
	ASSERT_EQ(tris.size(), 8u);
	EXPECT_TRUE(allCcw(v, tris));

	// Area covers the ring: outer 300*300 minus hole 100*100 = 90000 - 10000.
	EXPECT_EQ(totalArea2(v, tris), Int128(2 * 80000));

	// No triangle centroid lands inside the hole.
	std::vector<Vec2i64> holeRing = {v[4], v[5], v[6], v[7]};
	for (const Tri& t : tris) {
		EXPECT_NE(pointInPolygon(centroid(v, t), holeRing), PointInPolygon::Inside);
	}

	// All four hole edges are preserved as triangle edges (constraint kept).
	auto edges = triangleEdgeSet(tris);
	EXPECT_TRUE(hasEdge(edges, 4, 5));
	EXPECT_TRUE(hasEdge(edges, 5, 6));
	EXPECT_TRUE(hasEdge(edges, 6, 7));
	EXPECT_TRUE(hasEdge(edges, 7, 4));
}

TEST(Triangulation, TwoHolesInSquare) {
	std::vector<Vec2i64> v = {
		{0, 0}, {500, 0}, {500, 500}, {0, 500},		   // outer 0..3
		{60, 60}, {60, 160}, {160, 160}, {160, 60},	   // hole A (CW) 4..7
		{300, 300}, {300, 420}, {420, 420}, {420, 300} // hole B (CW) 8..11
	};
	std::vector<std::uint32_t>				outer = {0, 1, 2, 3};
	std::vector<std::vector<std::uint32_t>> holes = {{4, 5, 6, 7}, {8, 9, 10, 11}};
	auto									tris  = triangulateWithHoles(v, outer, holes);
	ASSERT_FALSE(tris.empty());
	EXPECT_TRUE(allCcw(v, tris));

	// Triangle count invariant: n=4, holeVerts=8, h=2 -> 4+8+2*2-2 = 14.
	EXPECT_EQ(tris.size(), 14u);

	// Area = 500*500 - 100*100 - 120*120 = 250000 - 10000 - 14400 = 225600.
	EXPECT_EQ(totalArea2(v, tris), Int128(2 * 225600));

	std::vector<Vec2i64> holeA = {v[4], v[5], v[6], v[7]};
	std::vector<Vec2i64> holeB = {v[8], v[9], v[10], v[11]};
	for (const Tri& t : tris) {
		Vec2i64 c = centroid(v, t);
		EXPECT_NE(pointInPolygon(c, holeA), PointInPolygon::Inside);
		EXPECT_NE(pointInPolygon(c, holeB), PointInPolygon::Inside);
	}

	auto edges = triangleEdgeSet(tris);
	EXPECT_TRUE(hasEdge(edges, 4, 5) && hasEdge(edges, 6, 7));
	EXPECT_TRUE(hasEdge(edges, 8, 9) && hasEdge(edges, 10, 11));
}

TEST(Triangulation, PlusRoomOutline) {
	// A plus / cross shape (concave, 12 vertices), CCW. Valid covering.
	std::vector<Vec2i64> v = {
		{100, 0},	{200, 0},	{200, 100}, {300, 100}, {300, 200}, {200, 200},
		{200, 300}, {100, 300}, {100, 200}, {0, 200},	{0, 100},	{100, 100}};
	auto ring = iota(12);
	auto tris = triangulateSimple(v, ring);
	ASSERT_EQ(tris.size(), 10u); // n-2
	EXPECT_TRUE(allCcw(v, tris));
	std::vector<Vec2i64> poly;
	for (std::uint32_t i : ring) {
		poly.push_back(v[i]);
	}
	for (const Tri& t : tris) {
		EXPECT_EQ(pointInPolygon(centroid(v, t), poly), PointInPolygon::Inside);
	}
	EXPECT_EQ(totalArea2(v, tris), ringArea2(v, ring));
}

TEST(Triangulation, DelaunayThinQuad) {
	// A "thin" quad whose naive fan diagonal is the long one. The Delaunay flip
	// must pick the short diagonal so the result is locally Delaunay. Vertices of
	// a kite where (0,0)-(200,0) split would violate in-circle.
	std::vector<Vec2i64> v = {{0, 100}, {100, 0}, {200, 100}, {100, 130}};
	auto				 tris = triangulateSimple(v, iota(4));
	ASSERT_EQ(tris.size(), 2u);
	EXPECT_TRUE(allCcw(v, tris));
	EXPECT_TRUE(isLocallyDelaunay(v, tris));

	// The Delaunay diagonal here is 1-3 (the vertical short axis), not 0-2.
	auto edges = triangleEdgeSet(tris);
	EXPECT_TRUE(hasEdge(edges, 1, 3));
	EXPECT_FALSE(hasEdge(edges, 0, 2));
}

TEST(Triangulation, DegenerateSelfTouchingReturnsEmpty) {
	// A bowtie (self-intersecting) outer ring is not simple -> reject.
	std::vector<Vec2i64> v	  = {{0, 0}, {100, 100}, {100, 0}, {0, 100}};
	auto				 tris = triangulateSimple(v, iota(4));
	EXPECT_TRUE(tris.empty());
}

TEST(Triangulation, WrongWindingOuterReturnsEmpty) {
	// Outer ring given CW (negative area) -> reject (contract requires CCW).
	std::vector<Vec2i64> v	  = {{0, 0}, {0, 100}, {100, 100}, {100, 0}};
	auto				 tris = triangulateSimple(v, iota(4));
	EXPECT_TRUE(tris.empty());
}

TEST(Triangulation, HoleNotInsideReturnsEmpty) {
	// Hole vertices outside the outer ring -> reject.
	std::vector<Vec2i64> v = {
		{0, 0}, {100, 0}, {100, 100}, {0, 100},			   // outer
		{200, 200}, {200, 300}, {300, 300}, {300, 200}};   // hole entirely outside
	std::vector<std::uint32_t>				outer = {0, 1, 2, 3};
	std::vector<std::vector<std::uint32_t>> holes = {{4, 5, 6, 7}};
	auto									tris  = triangulateWithHoles(v, outer, holes);
	EXPECT_TRUE(tris.empty());
}

TEST(Triangulation, DegenerateTooFewVerts) {
	std::vector<Vec2i64> v	  = {{0, 0}, {100, 0}};
	auto				 tris = triangulateSimple(v, iota(2));
	EXPECT_TRUE(tris.empty());
}

TEST(Triangulation, Deterministic) {
	std::vector<Vec2i64> v = {
		{0, 0}, {300, 0}, {300, 300}, {0, 300}, {100, 100}, {100, 200}, {200, 200}, {200, 100}};
	std::vector<std::uint32_t>				outer = {0, 1, 2, 3};
	std::vector<std::vector<std::uint32_t>> holes = {{4, 5, 6, 7}};
	auto									a	  = triangulateWithHoles(v, outer, holes);
	auto									b	  = triangulateWithHoles(v, outer, holes);
	ASSERT_EQ(a.size(), b.size());
	EXPECT_EQ(a, b);
}

TEST(OracleSweep, RandomConvexPolygons) {
	// Random convex polygons: build by sorting points by angle around the centroid
	// of a random point set, taking the convex hull. Assert n-2 CCW triangles whose
	// area sums to the polygon area and which are locally Delaunay.
	std::mt19937								rng(0x7A11);
	std::uniform_int_distribution<std::int64_t> coord(-400, 400);

	int built = 0;
	for (int iter = 0; iter < 400 && built < 120; ++iter) {
		const int k = 5 + (iter % 6); // 5..10 raw points
		std::vector<Vec2i64> pts;
		for (int i = 0; i < k; ++i) {
			pts.push_back({coord(rng), coord(rng)});
		}
		std::vector<Vec2i64> hull = convexHullCcw(std::move(pts));
		if (hull.size() < 3) {
			continue;
		}
		// monotone chain yields CCW.
		std::vector<std::uint32_t> ring = iota(static_cast<std::uint32_t>(hull.size()));
		ASSERT_GT(ringArea2(hull, ring).sign(), 0);

		auto tris = triangulateSimple(hull, ring);
		ASSERT_EQ(tris.size(), hull.size() - 2)
			<< "hull size " << hull.size() << " at iter " << iter;
		EXPECT_TRUE(allCcw(hull, tris)) << "iter " << iter;
		EXPECT_EQ(totalArea2(hull, tris), ringArea2(hull, ring)) << "iter " << iter;
		EXPECT_TRUE(isLocallyDelaunay(hull, tris)) << "iter " << iter;
		++built;
	}
	EXPECT_GT(built, 50); // sanity: we actually exercised a decent number
}

TEST(Triangulation, DiamondHoleDiagonalEdges) {
	// A 45-degree-rotated square hole (CW) in an axis-aligned outer square. Every
	// hole edge is diagonal, so the +x bridge ray from the hole's rightmost vertex
	// strikes outer/loop geometry at a non-integer x: the exact-wedge blocker path.
	std::vector<Vec2i64> v = {
		{0, 0}, {300, 0}, {300, 300}, {0, 300},				 // outer (CCW) 0..3
		{150, 80}, {80, 150}, {150, 220}, {220, 150}};		 // diamond hole (CW) 4..7
	std::vector<std::uint32_t>				outer = {0, 1, 2, 3};
	std::vector<std::vector<std::uint32_t>> holes = {{4, 5, 6, 7}};

	// Sanity on the fixture's windings before exercising the triangulator.
	ASSERT_GT(ringArea2(v, outer).sign(), 0);		   // outer CCW
	ASSERT_LT(ringArea2(v, holes[0]).sign(), 0);	   // hole CW

	auto tris = triangulateWithHoles(v, outer, holes);
	ASSERT_FALSE(tris.empty());
	EXPECT_TRUE(allCcw(v, tris));

	// Area conservation: covered area == outer minus hole (the CW hole ring's
	// doubled area is negative, so adding it subtracts the hole).
	EXPECT_EQ(totalArea2(v, tris), ringArea2(v, outer) + ringArea2(v, holes[0]));

	// No triangle centroid lands inside the diamond.
	std::vector<Vec2i64> holeRing = {v[4], v[5], v[6], v[7]};
	for (const Tri& t : tris) {
		EXPECT_NE(pointInPolygon(centroid(v, t), holeRing), PointInPolygon::Inside);
	}

	// All four diagonal hole edges are preserved as triangle edges (constraints kept).
	auto edges = triangleEdgeSet(tris);
	EXPECT_TRUE(hasEdge(edges, 4, 5));
	EXPECT_TRUE(hasEdge(edges, 5, 6));
	EXPECT_TRUE(hasEdge(edges, 6, 7));
	EXPECT_TRUE(hasEdge(edges, 7, 4));
}

TEST(Triangulation, TriangularHoleDiagonalEdges) {
	// A triangular hole whose three edges are all diagonal (no two vertices share
	// an x or a y), inside an axis-aligned square. Same non-integer-ray path.
	std::vector<Vec2i64> v = {
		{0, 0}, {300, 0}, {300, 300}, {0, 300},		 // outer (CCW) 0..3
		{80, 80}, {140, 230}, {220, 100}};				 // triangular hole (CW) 4..6
	std::vector<std::uint32_t>				outer = {0, 1, 2, 3};
	std::vector<std::vector<std::uint32_t>> holes = {{4, 5, 6}};

	ASSERT_GT(ringArea2(v, outer).sign(), 0);		   // outer CCW
	ASSERT_LT(ringArea2(v, holes[0]).sign(), 0);	   // hole CW

	auto tris = triangulateWithHoles(v, outer, holes);
	ASSERT_FALSE(tris.empty());
	EXPECT_TRUE(allCcw(v, tris));

	EXPECT_EQ(totalArea2(v, tris), ringArea2(v, outer) + ringArea2(v, holes[0]));

	std::vector<Vec2i64> holeRing = {v[4], v[5], v[6]};
	for (const Tri& t : tris) {
		EXPECT_NE(pointInPolygon(centroid(v, t), holeRing), PointInPolygon::Inside);
	}

	auto edges = triangleEdgeSet(tris);
	EXPECT_TRUE(hasEdge(edges, 4, 5));
	EXPECT_TRUE(hasEdge(edges, 5, 6));
	EXPECT_TRUE(hasEdge(edges, 6, 4));
}

TEST(OracleSweep, RotatedConvexHolesInSquare) {
	// Stress the diagonal-hole bridge path across many configurations. Each iter
	// drops two small, arbitrarily oriented convex holes into a large axis-aligned
	// square. Two holes (not one) matter: the second hole's +x bridge ray can
	// strike the first hole's already-spliced diagonal edges at a non-integer x,
	// which is exactly the exact-wedge blocker case. Assert area conservation,
	// no centroid inside either hole, and a locally Delaunay result.
	std::mt19937								rng(0xD1A6);
	std::uniform_int_distribution<std::int64_t> raw(-45, 45); // small hull, pre-translate

	// Outer square, generous so two offset holes stay strictly inside and disjoint.
	const std::int64_t			S		 = 1000;
	const std::vector<Vec2i64>	outerPts = {{0, 0}, {S, 0}, {S, S}, {0, S}};

	// Hole centers: left and right of the square's middle, well separated.
	const std::array<Vec2i64, 2> centers = {Vec2i64{300, 500}, Vec2i64{700, 500}};

	int built = 0;
	for (int iter = 0; iter < 300 && built < 120; ++iter) {
		std::vector<Vec2i64>					vertices = outerPts;
		std::vector<std::uint32_t>				outer	 = {0, 1, 2, 3};
		std::vector<std::vector<std::uint32_t>> holes;
		bool									ok = true;

		for (const Vec2i64& c : centers) {
			const int			 k = 5 + (iter % 5); // 5..9 raw points
			std::vector<Vec2i64> pts;
			for (int i = 0; i < k; ++i) {
				pts.push_back({c.x + raw(rng), c.y + raw(rng)});
			}
			std::vector<Vec2i64> hull = convexHullCcw(std::move(pts));
			if (hull.size() < 3) {
				ok = false;
				break;
			}
			// Holes must be CW: monotone chain gives CCW, so reverse.
			std::reverse(hull.begin(), hull.end());
			std::vector<std::uint32_t> ring;
			ring.reserve(hull.size());
			for (const Vec2i64& p : hull) {
				ring.push_back(static_cast<std::uint32_t>(vertices.size()));
				vertices.push_back(p);
			}
			holes.push_back(std::move(ring));
		}
		if (!ok || holes.size() != centers.size()) {
			continue;
		}

		// Confirm the fixture is valid input (CW holes) before treating an empty
		// result as a triangulator failure.
		bool windingOk = ringArea2(vertices, outer).sign() > 0;
		for (const auto& h : holes) {
			windingOk = windingOk && ringArea2(vertices, h).sign() < 0;
		}
		if (!windingOk) {
			continue;
		}

		auto tris = triangulateWithHoles(vertices, outer, holes);
		ASSERT_FALSE(tris.empty()) << "iter " << iter;
		EXPECT_TRUE(allCcw(vertices, tris)) << "iter " << iter;

		Int128 expected = ringArea2(vertices, outer);
		for (const auto& h : holes) {
			expected = expected + ringArea2(vertices, h);
		}
		EXPECT_EQ(totalArea2(vertices, tris), expected) << "iter " << iter;

		for (const auto& h : holes) {
			std::vector<Vec2i64> holeRing;
			for (std::uint32_t idx : h) {
				holeRing.push_back(vertices[idx]);
			}
			for (const Tri& t : tris) {
				EXPECT_NE(pointInPolygon(centroid(vertices, t), holeRing), PointInPolygon::Inside)
					<< "iter " << iter;
			}
		}

		EXPECT_TRUE(isLocallyDelaunay(vertices, tris)) << "iter " << iter;
		++built;
	}
	EXPECT_GT(built, 50);
}
