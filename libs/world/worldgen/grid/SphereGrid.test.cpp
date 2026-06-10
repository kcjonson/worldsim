#include "worldgen/grid/SphereGrid.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <random>
#include <unordered_set>

namespace worldgen {

// ============================================================================
// Tile count
// ============================================================================

TEST(SphereGrid, TileCount) {
    SphereGrid g8(8);
    EXPECT_EQ(g8.tileCount(), 10u * 8u * 8u);

    SphereGrid g16(16);
    EXPECT_EQ(g16.tileCount(), 10u * 16u * 16u);

    SphereGrid g64(64);
    EXPECT_EQ(g64.tileCount(), 10u * 64u * 64u);
}

// ============================================================================
// Determinism
// ============================================================================

TEST(SphereGrid, Determinism) {
    SphereGrid a(32), b(32);
    // Sample 200 random tile centers — must be bit-identical
    for (uint32_t t = 0; t < a.tileCount(); t += a.tileCount() / 200) {
        Vec3d ca = a.tileCenter(t);
        Vec3d cb = b.tileCenter(t);
        EXPECT_EQ(ca.x, cb.x) << "tile " << t;
        EXPECT_EQ(ca.y, cb.y) << "tile " << t;
        EXPECT_EQ(ca.z, cb.z) << "tile " << t;
    }
}

// ============================================================================
// fromUnitVector(tileCenter(t)) == t for all tiles at n=16
// ============================================================================

TEST(SphereGrid, InverseConsistency_n16) {
    SphereGrid g(16);
    uint32_t failures = 0;
    for (uint32_t t = 0; t < g.tileCount(); ++t) {
        Vec3d center = g.tileCenter(t);
        TileId result = g.fromUnitVector(center);
        if (result != t) ++failures;
    }
    EXPECT_EQ(failures, 0u) << "fromUnitVector(tileCenter(t)) != t for " << failures << " tiles";
}

// ============================================================================
// fromUnitVector(tileCenter(t)) == t for sampled tiles at n=256
// ============================================================================

TEST(SphereGrid, InverseConsistency_n256) {
    SphereGrid g(256);
    uint32_t total = g.tileCount();
    uint32_t stride = total / 5000 + 1;
    uint32_t failures = 0;
    for (uint32_t t = 0; t < total; t += stride) {
        Vec3d center = g.tileCenter(t);
        TileId result = g.fromUnitVector(center);
        if (result != t) ++failures;
    }
    EXPECT_EQ(failures, 0u) << "fromUnitVector(tileCenter(t)) != t for " << failures << " sampled tiles at n=256";
}

// ============================================================================
// latLonOf -> fromLatLon roundtrip
// ============================================================================

TEST(SphereGrid, LatLonRoundtrip) {
    SphereGrid g(32);
    uint32_t total = g.tileCount();
    uint32_t stride = total / 1000 + 1;
    uint32_t failures = 0;
    for (uint32_t t = 0; t < total; t += stride) {
        double lat{}, lon{};
        g.latLonOf(t, lat, lon);
        TileId result = g.fromLatLon(lat, lon);
        if (result != t) ++failures;
    }
    EXPECT_EQ(failures, 0u) << "latLonOf -> fromLatLon roundtrip failed for " << failures << " tiles";
}

// ============================================================================
// Neighbor symmetry: u in neighbors(v) <=> v in neighbors(u), n=8 exhaustive
// ============================================================================

TEST(SphereGrid, NeighborSymmetry_n8) {
    SphereGrid g(8);
    uint32_t total = g.tileCount();
    uint32_t asymmetric = 0;

    for (uint32_t t = 0; t < total; ++t) {
        std::array<TileId, 8> nbrs{};
        uint32_t cnt = g.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            TileId nb = nbrs[k];
            // Check that t appears in nb's neighbor list
            std::array<TileId, 8> nbNbrs{};
            uint32_t nbCnt = g.neighbors(nb, nbNbrs);
            bool found = false;
            for (uint32_t q = 0; q < nbCnt; ++q) {
                if (nbNbrs[q] == t) { found = true; break; }
            }
            if (!found) ++asymmetric;
        }
    }
    EXPECT_EQ(asymmetric, 0u) << "Neighbor asymmetry: " << asymmetric << " cases at n=8";
}

// ============================================================================
// Neighbor symmetry at n=16
// ============================================================================

TEST(SphereGrid, NeighborSymmetry_n16) {
    SphereGrid g(16);
    uint32_t total = g.tileCount();
    uint32_t asymmetric = 0;

    for (uint32_t t = 0; t < total; ++t) {
        std::array<TileId, 8> nbrs{};
        uint32_t cnt = g.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            TileId nb = nbrs[k];
            std::array<TileId, 8> nbNbrs{};
            uint32_t nbCnt = g.neighbors(nb, nbNbrs);
            bool found = false;
            for (uint32_t q = 0; q < nbCnt; ++q) {
                if (nbNbrs[q] == t) { found = true; break; }
            }
            if (!found) ++asymmetric;
        }
    }
    EXPECT_EQ(asymmetric, 0u) << "Neighbor asymmetry: " << asymmetric << " cases at n=16";
}

// ============================================================================
// Neighbor counts: interior=8, edge corner tiles have fewer
// ============================================================================

TEST(SphereGrid, NeighborCounts) {
    SphereGrid g(8);
    uint32_t total = g.tileCount();
    uint32_t n = g.subdivision();

    uint32_t minCount = 8, maxCount = 0;
    for (uint32_t t = 0; t < total; ++t) {
        std::array<TileId, 8> nbrs{};
        uint32_t cnt = g.neighbors(t, nbrs);
        if (cnt < minCount) minCount = cnt;
        if (cnt > maxCount) maxCount = cnt;
    }
    EXPECT_LE(minCount, 8u);
    EXPECT_EQ(maxCount, 8u) << "max neighbor count should be 8 (interior tiles)";
    EXPECT_GE(minCount, 3u) << "min neighbor count should be at least 3";
    printf("[SphereGrid] Neighbor counts at n=%u: min=%u, max=%u\n", n, minCount, maxCount);
}

// ============================================================================
// Area uniformity at n=64
// ============================================================================

TEST(SphereGrid, AreaUniformity_n64) {
    SphereGrid g(64);
    uint32_t total = g.tileCount();
    constexpr double kEarthRadius = 6.371e6;

    float minArea = 1e30f, maxArea = 0.0f;
    for (uint32_t t = 0; t < total; ++t) {
        float w = g.tileWidthMeters(t, kEarthRadius);
        float area = w * w; // proxy for area
        if (area < minArea) minArea = area;
        if (area > maxArea) maxArea = area;
    }

    float ratio = maxArea / minArea;
    printf("[SphereGrid] Area uniformity at n=64: max/min area ratio = %.4f\n", ratio);
    EXPECT_LT(ratio, 2.0f) << "Area ratio exceeds 2.0 — grid is too non-uniform";
}

// ============================================================================
// locate(): u,v in [0,1)
// ============================================================================

TEST(SphereGrid, LocateUVInRange) {
    SphereGrid g(16);

    // Sample a range of lat/lon values
    for (int lat = -80; lat <= 80; lat += 5) {
        for (int lon = -175; lon <= 175; lon += 5) {
            TileId t = kInvalidTile;
            float u{}, v{};
            g.locate(static_cast<double>(lat), static_cast<double>(lon), t, u, v);
            EXPECT_NE(t, kInvalidTile);
            EXPECT_GE(u, 0.0f) << "u < 0 at lat=" << lat << " lon=" << lon;
            EXPECT_LT(u, 1.0f) << "u >= 1 at lat=" << lat << " lon=" << lon;
            EXPECT_GE(v, 0.0f) << "v < 0 at lat=" << lat << " lon=" << lon;
            EXPECT_LT(v, 1.0f) << "v >= 1 at lat=" << lat << " lon=" << lon;
        }
    }
}

// ============================================================================
// rhombusPointOnSphere: at tile-center params equals tileCenter()
// ============================================================================

TEST(SphereGrid, RhombusPointMatchesTileCenter) {
    SphereGrid g(16);
    uint32_t n = g.subdivision();
    double eps = 1e-10;

    uint32_t failures = 0;
    for (uint32_t r = 0; r < 10u; ++r) {
        for (uint32_t j = 0; j < n; j += 4) {
            for (uint32_t i = 0; i < n; i += 4) {
                double u = (static_cast<double>(i) + 0.5) / static_cast<double>(n);
                double v = (static_cast<double>(j) + 0.5) / static_cast<double>(n);
                Vec3d p = g.rhombusPointOnSphere(r, u, v);
                TileId t = r * n * n + j * n + i;
                Vec3d c = g.tileCenter(t);
                double dx = p.x - c.x, dy = p.y - c.y, dz = p.z - c.z;
                double dist = std::sqrt(dx*dx + dy*dy + dz*dz);
                if (dist > eps) {
                    ++failures;
                    if (failures <= 3)
                        printf("[SphereGrid] rhombusPointOnSphere mismatch r=%u i=%u j=%u dist=%.2e\n",
                               r, i, j, dist);
                }
            }
        }
    }
    EXPECT_EQ(failures, 0u) << "rhombusPointOnSphere != tileCenter for " << failures << " sampled tiles";
}

} // namespace worldgen
