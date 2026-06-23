#include "Tessellator.h"
#include "Types.h"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace renderer;

namespace {

	// Sum of unsigned triangle areas. For a valid (non-overlapping) tessellation this equals
	// the filled region's area, which is the strongest simple correctness check.
	double meshArea(const TessellatedMesh& m) {
		double total = 0.0;
		for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
			const auto& a = m.vertices[m.indices[t]];
			const auto& b = m.vertices[m.indices[t + 1]];
			const auto& c = m.vertices[m.indices[t + 2]];
			const double cross =
				(static_cast<double>(b.x - a.x) * (c.y - a.y)) - (static_cast<double>(c.x - a.x) * (b.y - a.y));
			total += std::abs(cross) * 0.5;
		}
		return total;
	}

	VectorPath makePath(std::vector<Foundation::Vec2> verts) {
		VectorPath p;
		p.isClosed = true;
		p.vertices = std::move(verts);
		return p;
	}

} // namespace

// Concave L-shape: the case the old ear-clipper could do, now via the sweep. Area must be exact.
TEST(TessellatorSweep, ConcaveLShape) {
	Tessellator		t;
	VectorPath		path = makePath({{0, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}});
	TessellatedMesh mesh;
	ASSERT_TRUE(t.Tessellate(path, mesh));
	EXPECT_GT(mesh.getTriangleCount(), 0U);
	EXPECT_NEAR(meshArea(mesh), 6400.0, 1.0); // 100*40 + 40*60
}

// Concave 5-point star (non-self-intersecting). Area equals the polygon's shoelace area.
TEST(TessellatorSweep, ConcaveStar) {
	Tessellator			   t;
	std::vector<Foundation::Vec2> verts;
	const float			   pi = 3.14159265358979F;
	for (int i = 0; i < 10; ++i) {
		const float a = ((static_cast<float>(i) / 10.0F) * 2.0F * pi) - (pi / 2.0F);
		const float r = (i % 2 == 0) ? 100.0F : 40.0F;
		verts.push_back({100.0F + (r * std::cos(a)), 100.0F + (r * std::sin(a))});
	}
	// Shoelace area of the star outline.
	double shoelace = 0.0;
	for (size_t i = 0; i < verts.size(); ++i) {
		const auto& p0 = verts[i];
		const auto& p1 = verts[(i + 1) % verts.size()];
		shoelace += (static_cast<double>(p0.x) * p1.y) - (static_cast<double>(p1.x) * p0.y);
	}
	shoelace = std::abs(shoelace) * 0.5;

	TessellatedMesh mesh;
	ASSERT_TRUE(t.Tessellate(makePath(verts), mesh));
	EXPECT_NEAR(meshArea(mesh), shoelace, 1.0);
}

// Square with a square hole (opposite winding), nonzero rule. Area = outer - hole.
TEST(TessellatorSweep, SquareWithHoleNonZero) {
	Tessellator			   t;
	VectorPath			   outer = makePath({{0, 0}, {100, 0}, {100, 100}, {0, 100}});	  // CCW
	VectorPath			   hole = makePath({{25, 25}, {25, 75}, {75, 75}, {75, 25}});	  // CW
	std::vector<VectorPath> contours = {outer, hole};

	TessellatedMesh mesh;
	ASSERT_TRUE(t.Tessellate(contours, mesh));
	EXPECT_NEAR(meshArea(mesh), 7500.0, 1.0); // 10000 - 2500
}

// Self-intersecting bowtie: the old ear-clipper failed outright here. The sweep must insert
// an intersection vertex and still produce a valid triangulation.
TEST(TessellatorSweep, SelfIntersectingBowtie) {
	Tessellator		t;
	VectorPath		path = makePath({{0, 0}, {100, 100}, {100, 0}, {0, 100}});
	TessellatedMesh mesh;
	ASSERT_TRUE(t.Tessellate(path, mesh));
	EXPECT_GT(mesh.getTriangleCount(), 0U);
	EXPECT_GE(mesh.vertices.size(), 5U); // 4 input + the intersection (Steiner) point
	EXPECT_NEAR(meshArea(mesh), 5000.0, 2.0); // two 2500-area lobes
}

// Two nested same-winding squares differentiate the winding rules:
//  - nonzero: inner region winds twice -> still inside -> whole outer fills (10000)
//  - even-odd: inner region is even -> a ring (10000 - 2500 = 7500)
TEST(TessellatorSweep, WindingRuleDifferentiation) {
	Tessellator			   t;
	VectorPath			   outer = makePath({{0, 0}, {100, 0}, {100, 100}, {0, 100}}); // CCW
	VectorPath			   inner = makePath({{25, 25}, {75, 25}, {75, 75}, {25, 75}}); // CCW (same)
	std::vector<VectorPath> contours = {outer, inner};

	TessellatedMesh nonzero;
	TessellatorOptions nz;
	nz.useNonZeroFillRule = true;
	ASSERT_TRUE(t.Tessellate(contours, nonzero, nz));
	EXPECT_NEAR(meshArea(nonzero), 10000.0, 1.0);

	TessellatedMesh odd;
	TessellatorOptions eo;
	eo.useNonZeroFillRule = false;
	ASSERT_TRUE(t.Tessellate(contours, odd, eo));
	EXPECT_NEAR(meshArea(odd), 7500.0, 1.0);
}

// Degenerate zero-area input must not crash or produce overlapping geometry.
TEST(TessellatorSweep, DegenerateCollinearDoesNotCrash) {
	Tessellator		t;
	TessellatedMesh mesh;
	// Collinear points: no interior. Must return without crashing; essentially no area.
	t.Tessellate(makePath({{0, 0}, {50, 0}, {100, 0}, {150, 0}}), mesh);
	EXPECT_LT(meshArea(mesh), 1.0);
}

// A duplicated vertex inside a concave contour (a zero-length edge) must be handled cleanly.
TEST(TessellatorSweep, DuplicateVertexInConcave) {
	Tessellator		t;
	VectorPath		path =
		makePath({{0, 0}, {100, 0}, {100, 0}, {100, 40}, {40, 40}, {40, 100}, {0, 100}});
	TessellatedMesh mesh;
	ASSERT_TRUE(t.Tessellate(path, mesh));
	EXPECT_NEAR(meshArea(mesh), 6400.0, 1.0);
}

// High-detail stress: a 1000-vertex concave organic outline (the flora target). Area must
// match the polygon's shoelace area, confirming a valid, gap-free triangulation at scale.
TEST(TessellatorSweep, LargeConcaveStress) {
	Tessellator					  t;
	std::vector<Foundation::Vec2> verts;
	const int					  n = 1000;
	const float					  pi = 3.14159265358979F;
	for (int i = 0; i < n; ++i) {
		const float a = (static_cast<float>(i) / static_cast<float>(n)) * 2.0F * pi;
		const float r = 200.0F * (1.0F + (0.35F * std::sin(11.0F * a)));
		verts.push_back({300.0F + (r * std::cos(a)), 300.0F + (r * std::sin(a))});
	}
	double shoelace = 0.0;
	for (size_t i = 0; i < verts.size(); ++i) {
		const auto& p0 = verts[i];
		const auto& p1 = verts[(i + 1) % verts.size()];
		shoelace += (static_cast<double>(p0.x) * p1.y) - (static_cast<double>(p1.x) * p0.y);
	}
	shoelace = std::abs(shoelace) * 0.5;

	TessellatedMesh mesh;
	ASSERT_TRUE(t.Tessellate(makePath(verts), mesh));
	EXPECT_GT(mesh.getTriangleCount(), 900U);
	EXPECT_NEAR(meshArea(mesh), shoelace, shoelace * 0.001); // within 0.1%
}
