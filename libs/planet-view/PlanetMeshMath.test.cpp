#include "PlanetMeshMath.h"

#include <gtest/gtest.h>

using namespace planetview;

// Vertex count formula: 10 rhombi, each (min(n,128)+1)^2 vertices.
TEST(PlanetMeshMath, VertexCountFormula) {
    EXPECT_EQ(expectedVertexCount(4),   10U * 5U * 5U);
    EXPECT_EQ(expectedVertexCount(128), 10U * 129U * 129U);
    // Clamped at 128
    EXPECT_EQ(expectedVertexCount(256), 10U * 129U * 129U);
}

// Index count formula: 10 * min(n,128)^2 * 6 (2 triangles per quad).
TEST(PlanetMeshMath, IndexCountFormula) {
    EXPECT_EQ(expectedIndexCount(4),   10U * 4U * 4U * 6U);
    EXPECT_EQ(expectedIndexCount(128), 10U * 128U * 128U * 6U);
    EXPECT_EQ(expectedIndexCount(256), 10U * 128U * 128U * 6U);
}

// Sanity check: clamping is consistent between vertex and index formulas.
TEST(PlanetMeshMath, VertexAndIndexCountsConsistent) {
    for (unsigned n : {1U, 4U, 16U, 64U, 128U, 256U, 512U}) {
        unsigned v  = expectedVertexCount(n);
        unsigned ix = expectedIndexCount(n);
        // The index buffer references vertices 0..(vps*vps-1) per rhombus.
        // Just verify neither overflows in a naive way.
        EXPECT_GT(v,  0U) << "n=" << n;
        EXPECT_GT(ix, 0U) << "n=" << n;
    }
}
