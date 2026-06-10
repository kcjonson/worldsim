#include "PlanetMeshMath.h"

#include <gtest/gtest.h>
#include <cmath>

using namespace planetview;

static constexpr float kUnitTol = 1e-5F;

// All sample points returned by rhombusToSphere should lie on the unit sphere.
TEST(PlanetMeshMath, AllRhombiProduceUnitVectors) {
    const float uvSamples[] = { 0.0F, 0.25F, 0.5F, 0.75F, 1.0F };
    for (unsigned r = 0; r < 10U; ++r) {
        for (float u : uvSamples) {
            for (float v : uvSamples) {
                SpherePt p = rhombusToSphere(r, u, v);
                float mag  = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
                EXPECT_NEAR(mag, 1.0F, kUnitTol)
                    << "r=" << r << " u=" << u << " v=" << v;
            }
        }
    }
}

// Vertex count formula check.
TEST(PlanetMeshMath, VertexCountFormula) {
    EXPECT_EQ(expectedVertexCount(4),   10U * 5U * 5U);
    EXPECT_EQ(expectedVertexCount(128), 10U * 129U * 129U);
    // Clamped at 128
    EXPECT_EQ(expectedVertexCount(256), 10U * 129U * 129U);
}

// Index count formula check.
TEST(PlanetMeshMath, IndexCountFormula) {
    EXPECT_EQ(expectedIndexCount(4),   10U * 4U * 4U * 6U);
    EXPECT_EQ(expectedIndexCount(128), 10U * 128U * 128U * 6U);
    EXPECT_EQ(expectedIndexCount(256), 10U * 128U * 128U * 6U);
}

// Interior midpoints of rhombus 0 should produce distinct unit-sphere directions.
// (Corner samples are excluded because rhombi share edge/corner points with neighbours
// and the north-polar rhombi converge to the pole at v=1.)
TEST(PlanetMeshMath, Rhombus0InteriorMidpointsAreDistinct) {
    // Sample 4 distinct interior points well away from edges/poles.
    SpherePt pts[4] = {
        rhombusToSphere(0, 0.2F, 0.2F),
        rhombusToSphere(0, 0.8F, 0.2F),
        rhombusToSphere(0, 0.2F, 0.8F),
        rhombusToSphere(0, 0.8F, 0.8F),
    };
    for (int a = 0; a < 4; ++a) {
        for (int b = a + 1; b < 4; ++b) {
            float dx = pts[a].x - pts[b].x;
            float dy = pts[a].y - pts[b].y;
            float dz = pts[a].z - pts[b].z;
            float dist = std::sqrt(dx*dx + dy*dy + dz*dz);
            EXPECT_GT(dist, 0.05F) << "points " << a << " and " << b << " are too close";
        }
    }
}

// Sanity: north-polar rhombi (0-3) should produce points with positive z.
TEST(PlanetMeshMath, NorthPolarRhombiHavePositiveZ) {
    for (unsigned r = 0; r < 4U; ++r) {
        SpherePt p = rhombusToSphere(r, 0.5F, 0.5F);
        EXPECT_GT(p.z, 0.0F) << "rhombus " << r << " center has non-positive z";
    }
}

// South-polar rhombi (8-9) should produce points with negative z.
TEST(PlanetMeshMath, SouthPolarRhombiHaveNegativeZ) {
    for (unsigned r = 8U; r < 10U; ++r) {
        SpherePt p = rhombusToSphere(r, 0.5F, 0.5F);
        EXPECT_LT(p.z, 0.0F) << "rhombus " << r << " center has non-negative z";
    }
}
