#include "vector/MeshBounds.h"

#include <gtest/gtest.h>

namespace {

	renderer::TessellatedMesh makeQuad(float x0, float y0, float x1, float y1) {
		renderer::TessellatedMesh m;
		m.vertices = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
		m.indices = {0, 1, 2, 0, 2, 3};
		return m;
	}

} // namespace

TEST(MeshBoundsTest, ComputeBoundsOfEmptyMeshIsZero) {
	renderer::TessellatedMesh empty;
	Foundation::Rect b = renderer::computeBounds(empty);
	EXPECT_FLOAT_EQ(b.x, 0.0F);
	EXPECT_FLOAT_EQ(b.y, 0.0F);
	EXPECT_FLOAT_EQ(b.width, 0.0F);
	EXPECT_FLOAT_EQ(b.height, 0.0F);
}

TEST(MeshBoundsTest, ComputeBounds) {
	auto			 m = makeQuad(-2.0F, -1.0F, 4.0F, 3.0F);
	Foundation::Rect b = renderer::computeBounds(m);
	EXPECT_FLOAT_EQ(b.x, -2.0F);
	EXPECT_FLOAT_EQ(b.y, -1.0F);
	EXPECT_FLOAT_EQ(b.width, 6.0F);
	EXPECT_FLOAT_EQ(b.height, 4.0F);
}

TEST(MeshBoundsTest, FitToRectCentersAndPreservesAspect) {
	// 2x1 mesh into a 100x100 target -> uniform scale 50, centered vertically.
	auto			 m = makeQuad(0.0F, 0.0F, 2.0F, 1.0F);
	Foundation::Rect src = renderer::computeBounds(m);
	renderer::fitToRect(m, src, {0.0F, 0.0F, 100.0F, 100.0F});

	Foundation::Rect fitted = renderer::computeBounds(m);
	EXPECT_FLOAT_EQ(fitted.width, 100.0F);
	EXPECT_FLOAT_EQ(fitted.height, 50.0F);
	EXPECT_FLOAT_EQ(fitted.x, 0.0F);
	EXPECT_FLOAT_EQ(fitted.y, 25.0F);
	EXPECT_FLOAT_EQ(fitted.x + (fitted.width * 0.5F), 50.0F);
	EXPECT_FLOAT_EQ(fitted.y + (fitted.height * 0.5F), 50.0F);
}

TEST(MeshBoundsTest, FitToRectZeroExtentIsNoOp) {
	renderer::TessellatedMesh m;
	m.vertices = {{5.0F, 5.0F}};
	renderer::fitToRect(m, {5.0F, 5.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 100.0F, 100.0F});
	EXPECT_FLOAT_EQ(m.vertices[0].x, 5.0F);
	EXPECT_FLOAT_EQ(m.vertices[0].y, 5.0F);
}
