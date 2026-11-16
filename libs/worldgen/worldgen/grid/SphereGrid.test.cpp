#include "worldgen/grid/SphereGrid.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <map>
#include <random>
#include <vector>

namespace worldgen {

namespace {

double chordDist(Vec3d a, Vec3d b) {
    double dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

// Quantize a unit direction into an integer key so identical physical vertices
// (shared across rhombus seams) collapse to the same bucket.
uint64_t quantizeDir(Vec3d d) {
    auto q = [](double x) -> int64_t {
        return static_cast<int64_t>(std::llround(x * 1.0e7));
    };
    uint64_t h = 1469598103934665603ull;
    for (int64_t v : {q(d.x), q(d.y), q(d.z)}) {
        h ^= static_cast<uint64_t>(v);
        h *= 1099511628211ull;
    }
    return h;
}

// The 12 icosahedron vertex directions (must match the grid's construction).
std::array<Vec3d, 12> icosaVerts() {
    constexpr double kPiOver180 = 3.14159265358979323846 / 180.0;
    constexpr double kInvSqrt5 = 0.4472135954999579392818347337462;
    constexpr double k2InvSqrt5 = 0.8944271909999158785636694674925;
    std::array<Vec3d, 12> v{};
    v[0] = {0.0, 0.0, 1.0};
    v[11] = {0.0, 0.0, -1.0};
    for (int r = 0; r < 5; ++r) {
        double lon = r * 72.0 * kPiOver180;
        v[1 + r] = {k2InvSqrt5 * std::cos(lon), k2InvSqrt5 * std::sin(lon), kInvSqrt5};
    }
    for (int r = 0; r < 5; ++r) {
        double lon = (36.0 + r * 72.0) * kPiOver180;
        v[6 + r] = {k2InvSqrt5 * std::cos(lon), k2InvSqrt5 * std::sin(lon), -kInvSqrt5};
    }
    return v;
}

} // namespace

// ============================================================================
// Tile count: 10*n*n + 2
// ============================================================================

TEST(SphereGrid, TileCount) {
    EXPECT_EQ(SphereGrid(8).tileCount(), 10u * 8u * 8u + 2u);
    EXPECT_EQ(SphereGrid(16).tileCount(), 10u * 16u * 16u + 2u);
    EXPECT_EQ(SphereGrid(64).tileCount(), 10u * 64u * 64u + 2u);
}

// ============================================================================
// Vertex ownership partition: every physical chart vertex is owned by exactly
// one TileId, and the total of distinct vertices equals 10*n*n + 2.
// ============================================================================

static void checkOwnershipPartition(uint32_t n) {
    SphereGrid g(n);
    int in = static_cast<int>(n);

    // Map each physical vertex (quantized direction) to the set of TileIds that
    // claim it. Walk every (rhombus, i, j) in [0..n]^2, resolve via fromUnitVector
    // of its exact direction, and record.
    std::map<uint64_t, TileId> ownerOf;
    std::map<uint64_t, uint32_t> claimCount;

    for (uint32_t r = 0; r < 10u; ++r) {
        for (int j = 0; j <= in; ++j) {
            for (int i = 0; i <= in; ++i) {
                Vec3d dir = g.rhombusPointOnSphere(r, static_cast<double>(i) / n,
                                                   static_cast<double>(j) / n);
                uint64_t key = quantizeDir(dir);
                TileId t = g.fromUnitVector(dir);
                ASSERT_NE(t, kInvalidTile) << "r=" << r << " i=" << i << " j=" << j;
                ASSERT_LT(t, g.tileCount());
                auto it = ownerOf.find(key);
                if (it == ownerOf.end()) {
                    ownerOf[key] = t;
                    claimCount[key] = 1;
                } else {
                    EXPECT_EQ(it->second, t)
                        << "vertex r=" << r << " i=" << i << " j=" << j
                        << " owned by " << it->second << " but fromUnitVector gave " << t;
                    ++claimCount[key];
                }
            }
        }
    }

    // Distinct physical vertices == tileCount.
    EXPECT_EQ(ownerOf.size(), g.tileCount())
        << "distinct vertices != tileCount at n=" << n;

    // Every TileId in range must be the owner of exactly one physical vertex.
    std::vector<uint32_t> ownedTimes(g.tileCount(), 0);
    for (auto& [key, t] : ownerOf) ownedTimes[t] += 1;
    uint32_t notOwnedOnce = 0;
    for (uint32_t t = 0; t < g.tileCount(); ++t)
        if (ownedTimes[t] != 1) ++notOwnedOnce;
    EXPECT_EQ(notOwnedOnce, 0u)
        << "some TileId is not the unique owner of exactly one vertex at n=" << n;

    // Poles present.
    EXPECT_EQ(g.fromUnitVector({0.0, 0.0, 1.0}), g.northPole());
    EXPECT_EQ(g.fromUnitVector({0.0, 0.0, -1.0}), g.southPole());
}

TEST(SphereGrid, VertexOwnershipPartition_n4) { checkOwnershipPartition(4); }
TEST(SphereGrid, VertexOwnershipPartition_n8) { checkOwnershipPartition(8); }
TEST(SphereGrid, VertexOwnershipPartition_n16) { checkOwnershipPartition(16); }

// ============================================================================
// Determinism
// ============================================================================

TEST(SphereGrid, Determinism) {
    SphereGrid a(32), b(32);
    for (uint32_t t = 0; t < a.tileCount(); t += a.tileCount() / 200 + 1) {
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
        EXPECT_EQ(g.tileCount(), 10u * n * n + 2u);
    }
}

// ============================================================================
// Inverse consistency: fromUnitVector(tileCenter(t)) == t (incl. both poles),
// plus off-center points strictly inside the hex must map back to t.
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
                            << failures << " tiles (incl. poles)";

    // Off-center: axial offsets (da,db) strictly inside the hex. Skip poles
    // (centers at chart corners; offsets would leave the chart).
    static const double kOff[][2] = {
        { 0.18,  0.00}, {-0.18,  0.00}, { 0.00,  0.18}, { 0.00, -0.18},
        { 0.12, -0.12}, {-0.12,  0.12}, { 0.09,  0.09}, {-0.09, -0.09},
    };
    uint32_t offFailures = 0, offTested = 0;
    for (uint32_t t = 0; t < g.tileCount(); ++t) {
        if (t == g.northPole() || t == g.southPole()) continue;
        // Decode owned (i in [1..n], j in [0..n-1]).
        uint32_t rem = t % (n * n);
        uint32_t j = rem / n, i = rem % n + 1;
        uint32_t rh = t / (n * n);
        for (auto& o : kOff) {
            double da = o[0], db = o[1];
            if (da * da + db * db + da * db >= 0.04) continue; // < 0.2^2
            double uu = (static_cast<double>(i) + da) / static_cast<double>(n);
            double vv = (static_cast<double>(j) + db) / static_cast<double>(n);
            if (uu < 0.0 || uu > 1.0 || vv < 0.0 || vv > 1.0) continue;
            ++offTested;
            Vec3d p = g.rhombusPointOnSphere(rh, uu, vv);
            if (g.fromUnitVector(p) != t) ++offFailures;
        }
    }
    EXPECT_EQ(offFailures, 0u) << "off-center points mapped to wrong tile in "
                              << offFailures << "/" << offTested << " cases at n=16";
}

TEST(SphereGrid, InverseConsistency_n256) {
    SphereGrid g(256);
    uint32_t total = g.tileCount();
    uint32_t n = g.subdivision();
    uint32_t stride = total / 5000 + 1;
    uint32_t failures = 0, offFailures = 0;
    static const double kOff[][2] = {
        { 0.18, 0.00}, {0.00, 0.18}, {0.12, -0.12}, {-0.09, -0.09},
    };
    for (uint32_t t = 0; t < total; t += stride) {
        Vec3d center = g.tileCenter(t);
        if (g.fromUnitVector(center) != t) ++failures;

        if (t == g.northPole() || t == g.southPole()) continue;
        uint32_t rem = t % (n * n);
        uint32_t j = rem / n, i = rem % n + 1;
        uint32_t rh = t / (n * n);
        for (auto& o : kOff) {
            double uu = (static_cast<double>(i) + o[0]) / static_cast<double>(n);
            double vv = (static_cast<double>(j) + o[1]) / static_cast<double>(n);
            if (uu < 0.0 || uu > 1.0 || vv < 0.0 || vv > 1.0) continue;
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
// Hex Voronoi property in the lattice metric. The tiles are Voronoi cells in
// the rhombus uv lattice metric (cube rounding), which the fragment shader
// mirrors; that is the assignment the renderer relies on. 3D-chord divergence
// near seams is expected and reported as informational only.
//
// For vertices ON a seam the chart metric is ambiguous across charts; we sample
// each owned, non-pole tile from its home chart and require the lattice-interior
// point to map back to it.
// ============================================================================

TEST(SphereGrid, HexVoronoiProperty) {
    SphereGrid g(64);
    uint32_t n = g.subdivision();
    std::mt19937_64 rng(0xA11CE5EEDULL);
    std::uniform_real_distribution<double> uni(-0.45, 0.45);

    uint32_t tested = 0, latticeViolations = 0;
    double worst3dRel = 0.0; uint32_t v3d = 0;
    for (uint32_t t = 0; t < g.tileCount(); ++t) {
        if (t == g.northPole() || t == g.southPole()) continue;
        uint32_t rem = t % (n * n);
        uint32_t j = rem / n, i = rem % n + 1;
        uint32_t rh = t / (n * n);
        for (int s = 0; s < 4; ++s) {
            double da = uni(rng), db = uni(rng);
            if (da * da + db * db + da * db >= 0.2) continue;
            double uu = (static_cast<double>(i) + da) / static_cast<double>(n);
            double vv = (static_cast<double>(j) + db) / static_cast<double>(n);
            if (uu < 0.0 || uu > 1.0 || vv < 0.0 || vv > 1.0) continue;
            ++tested;
            Vec3d p = g.rhombusPointOnSphere(rh, uu, vv);
            if (g.fromUnitVector(p) != t) ++latticeViolations;

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

static void checkNeighborSymmetry(uint32_t n) {
    SphereGrid g(n);
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
    EXPECT_EQ(asymmetric, 0u) << "Neighbor asymmetry: " << asymmetric
                              << " cases at n=" << n;
}

TEST(SphereGrid, NeighborSymmetry_n8) { checkNeighborSymmetry(8); }
TEST(SphereGrid, NeighborSymmetry_n16) { checkNeighborSymmetry(16); }

// ============================================================================
// Degree histogram: EXACTLY 12 pentagon tiles (count 5), all of which are the
// 12 icosahedron vertices; every other tile has 6 neighbors.
// ============================================================================

static void checkDegreeHistogram(uint32_t n) {
    SphereGrid g(n);
    uint32_t total = g.tileCount();

    auto verts = icosaVerts();
    // Quantized keys of the 12 icosahedron vertex directions.
    std::map<uint64_t, int> vertKey;
    for (int k = 0; k < 12; ++k) vertKey[quantizeDir(verts[k])] = k;

    uint32_t hist[8] = {0};
    uint32_t fiveNotVertex = 0, vertexNotFive = 0, foundVerts = 0;
    for (uint32_t t = 0; t < total; ++t) {
        std::array<TileId, 6> nbrs{};
        uint32_t cnt = g.neighbors(t, nbrs);
        ASSERT_LT(cnt, 8u);
        ++hist[cnt];

        bool isVertex = vertKey.count(quantizeDir(g.tileCenter(t))) != 0;
        if (isVertex) ++foundVerts;
        if (cnt == 5 && !isVertex) ++fiveNotVertex;
        if (isVertex && cnt != 5) ++vertexNotFive;
    }
    printf("[SphereGrid] Degree histogram at n=%u:", n);
    for (int c = 0; c <= 6; ++c) if (hist[c]) printf(" [%d]=%u", c, hist[c]);
    printf("\n");

    EXPECT_EQ(hist[5], 12u) << "must be exactly 12 pentagon tiles";
    EXPECT_EQ(hist[6], total - 12u) << "all non-pentagon tiles must be hexagons";
    EXPECT_EQ(foundVerts, 12u) << "must find all 12 icosahedron-vertex tiles";
    EXPECT_EQ(fiveNotVertex, 0u) << "a degree-5 tile is not an icosahedron vertex";
    EXPECT_EQ(vertexNotFive, 0u) << "an icosahedron-vertex tile is not degree-5";
}

TEST(SphereGrid, DegreeHistogram_n8) { checkDegreeHistogram(8); }
TEST(SphereGrid, DegreeHistogram_n16) { checkDegreeHistogram(16); }

// ============================================================================
// Area uniformity at n=64. Pentagons/poles have ~5/6 the area of a hex, so the
// max/min ratio floor is ~1.2 from those alone; bound generously and print.
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
    // Hex area varies ~1.5x across the grid (rhombus distortion); pentagon tiles
    // measured as 5/6-area quads add ~1.2x on the low end. 2.5 is a safe bound.
    EXPECT_LT(ratio, 2.5f) << "Area ratio too large — grid is too non-uniform";
}

// ============================================================================
// rhombusPointOnSphere at owned-vertex params equals tileCenter()
// ============================================================================

TEST(SphereGrid, RhombusPointMatchesTileCenter) {
    SphereGrid g(16);
    uint32_t n = g.subdivision();
    double eps = 1e-10;
    uint32_t failures = 0;
    // Owned vertices: i in [1..n], j in [0..n-1].
    for (uint32_t r = 0; r < 10u; ++r) {
        for (uint32_t j = 0; j < n; j += 4) {
            for (uint32_t i = 1; i <= n; i += 4) {
                double u = static_cast<double>(i) / static_cast<double>(n);
                double v = static_cast<double>(j) / static_cast<double>(n);
                Vec3d p = g.rhombusPointOnSphere(r, u, v);
                TileId t = r * n * n + j * n + (i - 1);
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
// real neighbor; high edgeDistance near tile centers.
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
        if (t == g.northPole() || t == g.southPole()) continue;
        double lat{}, lon{};
        g.latLonOf(t, lat, lon);
        SphereGrid::HexSample s = g.locateHex(lat, lon);
        ASSERT_EQ(s.tile, t) << "center of tile must locate to itself";
        if (s.edgeDistance <= 0.3f) ++lowAtCenter;

        // The blend neighbor must be a real neighbor and distinct from the tile.
        if (s.neighbor != kInvalidTile) {
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
        << " tiles";
}

// ============================================================================
// locateHex continuity: walk a great circle crossing rhombus edges in small
// steps; edgeDistance must not jump.
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
    EXPECT_LT(maxJump, 0.3) << "edgeDistance jumped discontinuously across a seam";
}

} // namespace worldgen
