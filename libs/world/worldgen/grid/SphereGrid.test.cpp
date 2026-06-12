#include "worldgen/grid/SphereGrid.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <random>

namespace worldgen {

namespace {

double chordDist(Vec3d a, Vec3d b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Smallest lattice-distance (in cells) from tile t's (i,j) to any of the four
// rhombus corners. Used to flag pinwheel/corner tiles where the chart metric
// and the true spherical metric disagree.
int cellsToNearestCorner(const SphereGrid& g, TileId t) {
    uint32_t n = g.subdivision();
    // decode without exposing private encode: t = rh*n*n + j*n + i
    uint32_t rem = t % (n * n);
    int j = static_cast<int>(rem / n);
    int i = static_cast<int>(rem % n);
    int in = static_cast<int>(n);
    int best = in;
    const int ci[4] = {0, in - 1, 0, in - 1};
    const int cj[4] = {0, 0, in - 1, in - 1};
    for (int k = 0; k < 4; ++k) {
        int di = std::abs(i - ci[k]);
        int dj = std::abs(j - cj[k]);
        // Chebyshev-ish: corner influence is roughly the max of the two axes.
        int d = std::max(di, dj);
        if (d < best) best = d;
    }
    return best;
}

} // namespace

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
    for (uint32_t t = 0; t < a.tileCount(); t += a.tileCount() / 200) {
        Vec3d ca = a.tileCenter(t);
        Vec3d cb = b.tileCenter(t);
        EXPECT_EQ(ca.x, cb.x) << "tile " << t;
        EXPECT_EQ(ca.y, cb.y) << "tile " << t;
        EXPECT_EQ(ca.z, cb.z) << "tile " << t;
    }
}

// ============================================================================
// EdgeAdjNotReversed: buildEdgeAdj asserts all 40 pairings are non-reversed
// on construction (debug builds). Exercise it across small n.
// ============================================================================

TEST(SphereGrid, EdgeAdjNotReversed) {
    for (uint32_t n = 1; n <= 4; ++n) {
        SphereGrid g(n);
        EXPECT_EQ(g.tileCount(), 10u * n * n);
    }
}

// ============================================================================
// Inverse consistency: fromUnitVector(tileCenter(t)) == t, plus off-center
// points strictly inside the hex must map back to t.
// ============================================================================

TEST(SphereGrid, InverseConsistency_n16) {
    SphereGrid g(16);
    uint32_t n = g.subdivision();
    uint32_t failures = 0;
    for (uint32_t t = 0; t < g.tileCount(); ++t) {
        Vec3d center = g.tileCenter(t);
        if (g.fromUnitVector(center) != t) ++failures;
    }
    EXPECT_EQ(failures, 0u) << "center: fromUnitVector(tileCenter(t)) != t for "
                            << failures << " tiles";

    // Off-center: pick axial offsets (da,db) strictly inside the hex. The hex
    // inradius in axial coords is 0.5 (cell spacing 1); the constraint
    // da^2 + db^2 + da*db <= r^2 keeps the point inside a disc of radius r in
    // the unskewed metric. r=0.3 stays clear of the Voronoi edge.
    static const double kOff[][2] = {
        { 0.30,  0.00}, {-0.30,  0.00}, { 0.00,  0.30}, { 0.00, -0.30},
        { 0.20, -0.20}, {-0.20,  0.20}, { 0.15,  0.15}, {-0.15, -0.15},
    };
    uint32_t offFailures = 0;
    for (uint32_t t = 0; t < g.tileCount(); ++t) {
        uint32_t rem = t % (n * n);
        uint32_t j = rem / n, i = rem % n;
        uint32_t rh = t / (n * n);
        for (auto& o : kOff) {
            double uu = (static_cast<double>(i) + 0.5 + o[0]) / static_cast<double>(n);
            double vv = (static_cast<double>(j) + 0.5 + o[1]) / static_cast<double>(n);
            Vec3d p = g.rhombusPointOnSphere(rh, uu, vv);
            if (g.fromUnitVector(p) != t) ++offFailures;
        }
    }
    EXPECT_EQ(offFailures, 0u) << "off-center points mapped to wrong tile in "
                              << offFailures << " cases at n=16";
}

TEST(SphereGrid, InverseConsistency_n256) {
    SphereGrid g(256);
    uint32_t total = g.tileCount();
    uint32_t n = g.subdivision();
    uint32_t stride = total / 5000 + 1;
    uint32_t failures = 0, offFailures = 0;
    static const double kOff[][2] = {
        { 0.30, 0.00}, {0.00, 0.30}, {0.20, -0.20}, {-0.15, -0.15},
    };
    for (uint32_t t = 0; t < total; t += stride) {
        Vec3d center = g.tileCenter(t);
        if (g.fromUnitVector(center) != t) ++failures;

        uint32_t rem = t % (n * n);
        uint32_t j = rem / n, i = rem % n;
        uint32_t rh = t / (n * n);
        for (auto& o : kOff) {
            double uu = (static_cast<double>(i) + 0.5 + o[0]) / static_cast<double>(n);
            double vv = (static_cast<double>(j) + 0.5 + o[1]) / static_cast<double>(n);
            Vec3d p = g.rhombusPointOnSphere(rh, uu, vv);
            if (g.fromUnitVector(p) != t) ++offFailures;
        }
    }
    EXPECT_EQ(failures, 0u) << "center inverse failed for " << failures
                            << " sampled tiles at n=256";
    EXPECT_EQ(offFailures, 0u) << "off-center inverse failed for " << offFailures
                               << " cases at n=256";
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
        if (g.fromLatLon(lat, lon) != t) ++failures;
    }
    EXPECT_EQ(failures, 0u) << "latLonOf -> fromLatLon roundtrip failed for "
                            << failures << " tiles";
}

// ============================================================================
// Hex Voronoi property. The tiles are Voronoi cells in the rhombus uv lattice
// metric (cube rounding), which the fragment shader mirrors exactly; that is
// the assignment the renderer relies on, so the property to verify is
// lattice-metric, not 3D-chord. (Projecting the lattice onto the sphere warps
// distances, so the 3D-nearest center can differ from the assigned one by up
// to ~10% of a cell near cell boundaries — measured below as informational.)
//
// Lattice property: any point strictly inside a hex in the unskewed-Cartesian
// lattice metric must map back to that hex. Build such points by offsetting
// from a tile center by (da,db) with da^2+db^2+da*db < r^2 for r just under
// the hex inradius (0.5), then mapping to the sphere and assigning.
// ============================================================================

TEST(SphereGrid, HexVoronoiProperty) {
    SphereGrid g(64);
    uint32_t n = g.subdivision();
    std::mt19937_64 rng(0xA11CE5EEDULL);
    std::uniform_real_distribution<double> uni(-0.48, 0.48);

    uint32_t tested = 0, latticeViolations = 0;
    double worst3dRel = 0.0; uint32_t v3d = 0;
    for (uint32_t t = 0; t < g.tileCount(); ++t) {
        if (cellsToNearestCorner(g, t) < 2) continue;
        uint32_t rem = t % (n * n);
        uint32_t j = rem / n, i = rem % n;
        uint32_t rh = t / (n * n);
        for (int s = 0; s < 4; ++s) {
            double da = uni(rng), db = uni(rng);
            // Reject points outside the hex inradius in the lattice metric.
            if (da * da + db * db + da * db >= 0.23) continue;
            ++tested;
            double uu = (static_cast<double>(i) + 0.5 + da) / static_cast<double>(n);
            double vv = (static_cast<double>(j) + 0.5 + db) / static_cast<double>(n);
            Vec3d p = g.rhombusPointOnSphere(rh, uu, vv);
            if (g.fromUnitVector(p) != t) ++latticeViolations;

            // Informational: how far the 3D-nearest neighbor diverges.
            double dAssigned = chordDist(p, g.tileCenter(t));
            std::array<TileId, 6> nbrs{};
            uint32_t cnt = g.neighbors(t, nbrs);
            for (uint32_t k = 0; k < cnt; ++k) {
                double dn = chordDist(p, g.tileCenter(nbrs[k]));
                if (dn < dAssigned) {
                    ++v3d;
                    double rel = (dAssigned - dn) / dAssigned;
                    if (rel > worst3dRel) worst3dRel = rel;
                }
            }
        }
    }
    printf("[SphereGrid] HexVoronoi(lattice): tested=%u violations=%u | "
           "3d-divergence cases=%u worstRel=%.3f\n",
           tested, latticeViolations, v3d, worst3dRel);
    EXPECT_EQ(latticeViolations, 0u)
        << "lattice-interior point mapped to the wrong hex";
    EXPECT_GT(tested, 100000u) << "too few points tested";
}

// ============================================================================
// Neighbor symmetry: u in neighbors(v) <=> v in neighbors(u), exhaustive.
// ============================================================================

TEST(SphereGrid, NeighborSymmetry_n8) {
    SphereGrid g(8);
    uint32_t total = g.tileCount();
    uint32_t asymmetric = 0;
    for (uint32_t t = 0; t < total; ++t) {
        std::array<TileId, 6> nbrs{};
        uint32_t cnt = g.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            std::array<TileId, 6> nbNbrs{};
            uint32_t nbCnt = g.neighbors(nbrs[k], nbNbrs);
            bool found = false;
            for (uint32_t q = 0; q < nbCnt; ++q)
                if (nbNbrs[q] == t) { found = true; break; }
            if (!found) ++asymmetric;
        }
    }
    EXPECT_EQ(asymmetric, 0u) << "Neighbor asymmetry: " << asymmetric << " cases at n=8";
}

TEST(SphereGrid, NeighborSymmetry_n16) {
    SphereGrid g(16);
    uint32_t total = g.tileCount();
    uint32_t asymmetric = 0;
    for (uint32_t t = 0; t < total; ++t) {
        std::array<TileId, 6> nbrs{};
        uint32_t cnt = g.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            std::array<TileId, 6> nbNbrs{};
            uint32_t nbCnt = g.neighbors(nbrs[k], nbNbrs);
            bool found = false;
            for (uint32_t q = 0; q < nbCnt; ++q)
                if (nbNbrs[q] == t) { found = true; break; }
            if (!found) ++asymmetric;
        }
    }
    EXPECT_EQ(asymmetric, 0u) << "Neighbor asymmetry: " << asymmetric << " cases at n=16";
}

// ============================================================================
// Neighbor counts. Interior tiles have 6 neighbors. Along a rhombus seam the
// two triangular lattices interlock with a half-cell (brick-wall) offset, so a
// seam cell borders some cells across the seam at a half-cell shift and its
// degree drops to 5. At the 12 icosahedron-vertex pinwheels the cell is pinched
// and degree drops to 4-5. Every tile with fewer than 6 neighbors must lie on a
// rhombus seam (i or j == 0 or n-1). The graph must be symmetric (checked
// separately); here we bound the degree distribution and its support.
// ============================================================================

TEST(SphereGrid, NeighborCounts) {
    SphereGrid g(8);
    uint32_t total = g.tileCount();
    uint32_t n = g.subdivision();
    int in = static_cast<int>(n);

    uint32_t hist[8] = {0};
    uint32_t minCount = 6, maxCount = 0;
    uint32_t lowDegreeOffSeam = 0;
    for (uint32_t t = 0; t < total; ++t) {
        std::array<TileId, 6> nbrs{};
        uint32_t cnt = g.neighbors(t, nbrs);
        if (cnt < 8) ++hist[cnt];
        if (cnt < minCount) minCount = cnt;
        if (cnt > maxCount) maxCount = cnt;
        if (cnt < 6) {
            uint32_t rem = t % (n * n);
            int j = static_cast<int>(rem / n), i = static_cast<int>(rem % n);
            bool onSeam = (i == 0 || i == in - 1 || j == 0 || j == in - 1);
            if (!onSeam) ++lowDegreeOffSeam;
        }
    }
    printf("[SphereGrid] Neighbor count histogram at n=%u:", n);
    for (int c = 0; c <= 6; ++c) if (hist[c]) printf(" [%d]=%u", c, hist[c]);
    printf("  (min=%u max=%u)\n", minCount, maxCount);

    EXPECT_EQ(maxCount, 6u) << "max neighbor count should be 6 (interior tiles)";
    EXPECT_GE(minCount, 4u) << "min neighbor count should be at least 4";
    EXPECT_EQ(lowDegreeOffSeam, 0u)
        << "tiles with <6 neighbors must lie on a rhombus seam";
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
        float area = w * w;
        if (area < minArea) minArea = area;
        if (area > maxArea) maxArea = area;
    }
    float ratio = maxArea / minArea;
    printf("[SphereGrid] Area uniformity at n=64: max/min area ratio = %.4f\n", ratio);
    EXPECT_LT(ratio, 2.0f) << "Area ratio exceeds 2.0 — grid is too non-uniform";
}

// ============================================================================
// rhombusPointOnSphere at tile-center params equals tileCenter()
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
                if (chordDist(p, c) > eps) {
                    ++failures;
                    if (failures <= 3)
                        printf("[SphereGrid] rhombusPointOnSphere mismatch r=%u i=%u j=%u\n",
                               r, i, j);
                }
            }
        }
    }
    EXPECT_EQ(failures, 0u) << "rhombusPointOnSphere != tileCenter for "
                            << failures << " sampled tiles";
}

// ============================================================================
// locateHex: tile agrees with fromLatLon; edgeDistance bounds; neighbor is a
// real neighbor for non-corner tiles; high edgeDistance near tile centers.
// ============================================================================

TEST(SphereGrid, LocateHexMatchesFromLatLon) {
    SphereGrid g(32);
    uint32_t mismatches = 0;
    for (int lat = -85; lat <= 85; lat += 5) {
        for (int lon = -175; lon <= 175; lon += 5) {
            SphereGrid::HexSample s = g.locateHex(lat, lon);
            TileId fromLL = g.fromLatLon(lat, lon);
            if (s.tile != fromLL) ++mismatches;
            EXPECT_GE(s.edgeDistance, 0.0f) << "lat=" << lat << " lon=" << lon;
            EXPECT_LE(s.edgeDistance, 0.9f) << "lat=" << lat << " lon=" << lon;
        }
    }
    EXPECT_EQ(mismatches, 0u) << "locateHex.tile != fromLatLon in "
                              << mismatches << " cases";
}

TEST(SphereGrid, LocateHexEdgeDistanceAtCenters) {
    SphereGrid g(32);
    uint32_t total = g.tileCount();
    uint32_t stride = total / 2000 + 1;
    uint32_t lowAtCenter = 0;
    uint32_t neighborViolations = 0;
    for (uint32_t t = 0; t < total; t += stride) {
        double lat{}, lon{};
        g.latLonOf(t, lat, lon);
        SphereGrid::HexSample s = g.locateHex(lat, lon);
        ASSERT_EQ(s.tile, t) << "center of tile must locate to itself";
        if (s.edgeDistance <= 0.3f) ++lowAtCenter;

        // For non-corner tiles, the blend neighbor must be a real neighbor and
        // distinct from the tile.
        if (cellsToNearestCorner(g, t) > 1 && s.neighbor != kInvalidTile) {
            if (s.neighbor == s.tile) { ++neighborViolations; continue; }
            std::array<TileId, 6> nbrs{};
            uint32_t cnt = g.neighbors(t, nbrs);
            bool found = false;
            for (uint32_t k = 0; k < cnt; ++k)
                if (nbrs[k] == s.neighbor) { found = true; break; }
            if (!found) ++neighborViolations;
        }
    }
    EXPECT_EQ(lowAtCenter, 0u)
        << "edgeDistance should exceed 0.3 at tile centers (" << lowAtCenter << " low)";
    EXPECT_EQ(neighborViolations, 0u)
        << "locateHex.neighbor not in neighbors(tile) for " << neighborViolations
        << " non-corner tiles";
}

// ============================================================================
// locateHex continuity: walk a great circle crossing rhombus edges in small
// steps; edgeDistance must not jump. Walk along the equator (crosses several
// rhombus seams) in fine increments.
// ============================================================================

TEST(SphereGrid, LocateHexContinuity) {
    SphereGrid g(64);
    double prev = -1.0;
    double maxJump = 0.0;
    const double step = 0.05; // degrees of longitude per step
    for (double lon = -180.0; lon <= 180.0; lon += step) {
        SphereGrid::HexSample s = g.locateHex(2.7, lon); // slightly off-equator
        double e = s.edgeDistance;
        if (prev >= 0.0) {
            double jump = std::abs(e - prev);
            if (jump > maxJump) maxJump = jump;
        }
        prev = e;
    }
    printf("[SphereGrid] locateHex continuity max edgeDistance jump = %.4f\n", maxJump);
    // A fine walk (0.05deg ~ much less than one cell at n=64) must not produce
    // discontinuous jumps in the Voronoi edge distance.
    EXPECT_LT(maxJump, 0.3) << "edgeDistance jumped discontinuously across a seam";
}

} // namespace worldgen
