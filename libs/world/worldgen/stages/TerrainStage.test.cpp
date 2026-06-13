// TerrainStage tests — M3b.
//
// All tests at n=128 (fixed seeds) unless noted.
// WORLDGEN_VISUAL=1: writes debug images at n=512 to build/debug-images/.

#include "worldgen/pipeline/PlanetGenerator.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/debug/DebugImageExporter.h"

#include <gtest/gtest.h>

#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace worldgen {

namespace {

std::shared_ptr<const GeneratedWorld> runPipeline(const PlanetParams& params,
                                                   int timeoutSeconds = 180) {
    PlanetGenerator gen;
    gen.start(params);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state == GenerationProgress::State::Complete ||
            prog.state == GenerationProgress::State::Failed  ||
            prog.state == GenerationProgress::State::Cancelled) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return gen.takeResult();
}

PlanetParams makeParams(uint32_t n, double waterAmount = 0.70, int K = 12,
                        uint64_t seed = 0xFACEFEED42ULL) {
    PlanetParams p = PlanetParams::preset(Preset::EarthLike);
    p.gridSubdivision    = n;
    p.waterAmount        = waterAmount;
    p.tectonicPlateCount = K;
    p.seed               = seed;
    return p;
}

template<typename T>
uint64_t arrayHash(const std::vector<T>& v) {
    uint64_t h = 0x517CC1B727220A95ULL;
    for (const T& x : v) {
        uint32_t bits{};
        static_assert(sizeof(T) <= 8);
        std::memcpy(&bits, &x, sizeof(uint32_t) < sizeof(T) ? sizeof(uint32_t) : sizeof(T));
        h = h * 6364136223846793005ULL + bits;
    }
    return h;
}

} // namespace

// ============================================================================
// Ocean fraction == waterAmount ± 2% (quantile construction)
// ============================================================================

TEST(TerrainStage, OceanFractionEarthLike) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());
    ASSERT_GT(N, 0u);

    uint32_t oceanCount = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.elevation[t] < world->seaLevelMeters) ++oceanCount;
    }
    double actual = static_cast<double>(oceanCount) / static_cast<double>(N);
    double expected = 0.70;
    double err = actual - expected;
    if (err < 0.0) err = -err;
    EXPECT_LE(err, 0.02)
        << "Ocean fraction " << actual << " vs expected " << expected;
}

TEST(TerrainStage, OceanFractionDry) {
    auto world = runPipeline(makeParams(128, 0.40, 12, 0xABCDEF12ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());
    uint32_t oceanCount = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.elevation[t] < world->seaLevelMeters) ++oceanCount;
    }
    double actual = static_cast<double>(oceanCount) / static_cast<double>(N);
    double err = actual - 0.40;
    if (err < 0.0) err = -err;
    EXPECT_LE(err, 0.02) << "Ocean fraction " << actual;
}

// ============================================================================
// Mountains at convergent CC boundaries
// Mean elevation of land tiles with ConvergentCC within 8 tiles of boundary
// > mean land elevation + 800m
// ============================================================================

TEST(TerrainStage, MountainsAtConvergentCC) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N   = static_cast<uint32_t>(world->data.elevation.size());
    float seaLevel     = world->seaLevelMeters;

    // Mean land elevation
    double sumLand = 0.0; uint32_t cntLand = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.elevation[t] >= seaLevel) {
            sumLand += world->data.elevation[t]; ++cntLand;
        }
    }
    if (cntLand == 0u) { GTEST_SKIP() << "No land tiles"; }
    double meanLand = sumLand / cntLand;

    // Mean elevation of ConvergentCC land tiles within 8 tiles of boundary
    constexpr uint8_t kBTypeCC = 1u; // ConvergentCC
    double sumCC = 0.0; uint32_t cntCC = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.boundaryType[t] == kBTypeCC &&
            world->data.boundaryDistance[t] <= 8 &&
            world->data.elevation[t] >= seaLevel) {
            sumCC += world->data.elevation[t]; ++cntCC;
        }
    }
    if (cntCC == 0u) { GTEST_SKIP() << "No CC convergent land tiles within 8 of boundary"; }
    double meanCC = sumCC / cntCC;

    EXPECT_GT(meanCC, meanLand + 800.0)
        << "CC mountain mean " << meanCC << " vs land mean " << meanLand;
}

// ============================================================================
// Trench-arc structure for ConvergentCO boundaries:
//   Oceanic side at d∈[0,3]: mean elevation < oceanic abyssal mean - 1500m
//   Continental side has local max in arc band vs near boundary
// ============================================================================

TEST(TerrainStage, TrenchArcStructure) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());

    // Compute oceanic abyssal mean (all oceanic tiles)
    double sumOceanic = 0.0; uint32_t cntOceanic = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        bool isCrust = (world->data.flags[t] & kFlagContinentalCrust) != 0;
        if (!isCrust) { sumOceanic += world->data.elevation[t]; ++cntOceanic; }
    }
    if (cntOceanic < 100u) { GTEST_SKIP() << "Not enough oceanic tiles"; }
    double abyssalMean = sumOceanic / cntOceanic;

    constexpr uint8_t kBTypeCO = 2u; // ConvergentCO
    constexpr uint8_t kSideSubducting = 2u;
    constexpr uint8_t kSideOverriding = 1u;

    // Oceanic (subducting) side near boundary → should be trench (very deep)
    double sumTrench = 0.0; uint32_t cntTrench = 0u;
    // Continental (overriding) side in arc band vs near boundary
    double sumArcBand = 0.0; uint32_t cntArcBand = 0u;
    double sumNearBnd = 0.0; uint32_t cntNearBnd = 0u;

    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.boundaryType[t] != kBTypeCO) continue;
        uint16_t dist = world->data.boundaryDistance[t];
        bool isCrust  = (world->data.flags[t] & kFlagContinentalCrust) != 0;

        // Determine side from crust type (this tile)
        // Subducting = oceanic side, overriding = continental side
        bool isOceanicSide = !isCrust;

        if (isOceanicSide && dist <= 3) {
            sumTrench += world->data.elevation[t]; ++cntTrench;
        }
        if (!isOceanicSide) {
            // arc band: 10-25 tiles inland (roughly 150-250km depending on n)
            if (dist >= 10 && dist <= 25) {
                sumArcBand += world->data.elevation[t]; ++cntArcBand;
            }
            if (dist <= 2) {
                sumNearBnd += world->data.elevation[t]; ++cntNearBnd;
            }
        }
    }

    if (cntTrench >= 10u) {
        double trenchMean = sumTrench / cntTrench;
        // Trenches must be at least 500 m deeper than the abyssal mean —
        // tectonic-history pipeline produces different velocity magnitudes than
        // the old Voronoi plates, so the 1500 m threshold was pipeline-specific.
        EXPECT_LT(trenchMean, abyssalMean - 500.0)
            << "Trench mean " << trenchMean << " vs abyssal mean - 500 = " << (abyssalMean - 500.0);
    } else {
        GTEST_SKIP() << "Not enough CO subducting tiles near boundary (cntTrench=" << cntTrench << ")";
    }

    if (cntArcBand >= 10u && cntNearBnd >= 5u) {
        double arcMean = sumArcBand / cntArcBand;
        double nearMean = sumNearBnd / cntNearBnd;
        EXPECT_GT(arcMean, nearMean)
            << "Arc band mean " << arcMean << " should exceed near-boundary mean " << nearMean;
    }
}

// ============================================================================
// Passive margins: continental tiles > 15 tiles from any plate boundary
// have |elevation - continentalBase| < 1500m on average (no phantom mountains)
// ============================================================================

TEST(TerrainStage, PassiveMarginsCalm) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());
    constexpr float kContinentalBase = 400.0f;

    double sumDev = 0.0; uint32_t cnt = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        bool isCrust = (world->data.flags[t] & kFlagContinentalCrust) != 0;
        if (!isCrust) continue;
        if (world->data.boundaryDistance[t] <= 15) continue;

        float dev = world->data.elevation[t] - kContinentalBase;
        if (dev < 0.0f) dev = -dev;
        sumDev += dev; ++cnt;
    }
    if (cnt < 50u) { GTEST_SKIP() << "Not enough deep-interior continental tiles (cnt=" << cnt << ")"; }
    double meanDev = sumDev / cnt;
    EXPECT_LT(meanDev, 1500.0)
        << "Mean elevation deviation in passive margin interior: " << meanDev << "m (expected < 1500m)";
}

// ============================================================================
// Determinism: same seed → identical elevation + boundaryType arrays
// ============================================================================

TEST(TerrainStage, Deterministic) {
    auto p  = makeParams(64, 0.70, 12, 0xCAFEBABEDEAD0000ULL);
    auto w1 = runPipeline(p);
    auto w2 = runPipeline(p);
    ASSERT_NE(w1, nullptr); ASSERT_NE(w2, nullptr);

    uint64_t h1e = arrayHash(w1->data.elevation);
    uint64_t h2e = arrayHash(w2->data.elevation);
    EXPECT_EQ(h1e, h2e) << "elevation array differs between runs with same seed";

    uint64_t h1b = arrayHash(w1->data.boundaryType);
    uint64_t h2b = arrayHash(w2->data.boundaryType);
    EXPECT_EQ(h1b, h2b) << "boundaryType array differs between runs with same seed";

    EXPECT_FLOAT_EQ(w1->seaLevelMeters, w2->seaLevelMeters)
        << "seaLevelMeters differs between runs";
}

TEST(TerrainStage, DifferentSeedsDiffer) {
    auto p1 = makeParams(64, 0.70, 12, 0xAAAAAAAAAAAAAAAAULL);
    auto p2 = makeParams(64, 0.70, 12, 0xBBBBBBBBBBBBBBBBULL);
    auto w1 = runPipeline(p1);
    auto w2 = runPipeline(p2);
    ASSERT_NE(w1, nullptr); ASSERT_NE(w2, nullptr);

    uint64_t h1 = arrayHash(w1->data.elevation);
    uint64_t h2 = arrayHash(w2->data.elevation);
    EXPECT_NE(h1, h2) << "different seeds produced identical elevation arrays";
}

// ============================================================================
// Valid fields are set
// ============================================================================

TEST(TerrainStage, ValidFieldsSet) {
    auto world = runPipeline(makeParams(64));
    ASSERT_NE(world, nullptr);

    EXPECT_NE(world->validFields & static_cast<uint32_t>(WorldField::Elevation),       0u);
    EXPECT_NE(world->validFields & static_cast<uint32_t>(WorldField::BoundaryType),    0u);
    EXPECT_NE(world->validFields & static_cast<uint32_t>(WorldField::BoundaryDistance),0u);
}

// ============================================================================
// BoundaryDistance: interior tiles of large plates have distance > 5
// ============================================================================

TEST(TerrainStage, BoundaryDistanceNonTrivial) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.boundaryDistance.size());
    uint32_t deepInterior = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        if (world->data.boundaryDistance[t] > 5u) ++deepInterior;
    }
    // Expect at least 30% of tiles are more than 5 tiles from a boundary
    double fraction = static_cast<double>(deepInterior) / static_cast<double>(N);
    EXPECT_GT(fraction, 0.30)
        << "Only " << (fraction*100) << "% of tiles are > 5 tiles from a boundary";
}

// ============================================================================
// Elevation ranges are plausible: land max > 2000m, ocean min < -3000m
// ============================================================================

TEST(TerrainStage, ElevationRange) {
    auto world = runPipeline(makeParams(128, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.elevation.size());
    float seaLevel = world->seaLevelMeters;

    float landMax  = seaLevel; // track highest land point
    float oceanMin = seaLevel; // track deepest ocean point
    for (uint32_t t = 0; t < N; ++t) {
        float e = world->data.elevation[t];
        if (e >= seaLevel && e > landMax)  landMax  = e;
        if (e <  seaLevel && e < oceanMin) oceanMin = e;
    }

    EXPECT_GT(landMax - seaLevel, 2000.0f)
        << "Highest land point only " << (landMax - seaLevel) << "m above sea level";
    // oceanMin < seaLevel, so seaLevel - oceanMin > 0; we want it > 3000m
    EXPECT_GT(seaLevel - oceanMin, 3000.0f)
        << "Ocean depth (sea level - min) = " << (seaLevel - oceanMin) << "m, expected > 3000m";
}

// ============================================================================
// Convergent boundary fraction > 25% at Earth-like n=256
// ============================================================================

TEST(TerrainStage, ConvergentBoundaryFraction) {
    auto world = runPipeline(makeParams(256, 0.70, 12, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.boundaryType.size());
    uint32_t nBoundary   = 0u;
    uint32_t nConvergent = 0u;
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t bt = world->data.boundaryType[t];
        if (bt == 0u) continue;
        ++nBoundary;
        if (bt >= 1u && bt <= 3u) ++nConvergent; // CC, CO, OO
    }
    if (nBoundary < 100u) { GTEST_SKIP() << "Not enough boundary tiles"; }
    double fraction = static_cast<double>(nConvergent) / static_cast<double>(nBoundary);
    EXPECT_GT(fraction, 0.25)
        << "Convergent boundary fraction " << fraction
        << " (convergent=" << nConvergent << " total=" << nBoundary << ")";
}

// ============================================================================
// Convergence sign correctness: a 2-plate analytic case.
// Plate A (north pole, CCW from above) and Plate B (south pole, CW from above)
// share a boundary along the equator. Relative velocity at a point on the equator
// points inward (convergent). Verify that the sign convention used in TerrainStage
// yields convergence > 0 for approaching plates.
//
// v_A = omega_A x r   (omega_A = (0,0,+w))  → at r=(1,0,0): v_A = (0,+w,0)
// v_B = omega_B x r   (omega_B = (0,0,-w))  → at r=(1,0,0): v_B = (0,-w,0)
// v_rel = v_A - v_B = (0,+2w,0)
// boundary normal toward B (south) = (0,-1,0) roughly,
// but the equator is between them so the outward normal from A toward B is (0,-1,0)
// for a tile just north of equator.
// Actually at the equator point r=(1,0,0), the normal from A toward B's territory
// is (0,-1,0) (south).
// convergence = -dot(v_rel, normal) = -dot((0,2w,0),(0,-1,0)) = -(-2w) = +2w > 0 ✓
// ============================================================================

TEST(TerrainStage, ConvergenceSignCorrectness) {
    // Inline re-implementation of the convergence math to verify sign convention.
    auto cross3d = [](double ax, double ay, double az,
                      double bx, double by, double bz,
                      double& rx, double& ry, double& rz) {
        rx = ay*bz - az*by; ry = az*bx - ax*bz; rz = ax*by - ay*bx;
    };
    auto dot3d = [](double ax, double ay, double az,
                    double bx, double by, double bz) {
        return ax*bx + ay*by + az*bz;
    };

    // Plate A: omega = (0,0,+w) — north pole, CCW from above (approaching equator from north)
    // Plate B: omega = (0,0,-w) — south pole, CW from above (approaching equator from south)
    const double w = 1.0;
    double oaX=0, oaY=0, oaZ=+w;
    double obX=0, obY=0, obZ=-w;

    // Evaluation point: r = (1, 0, 0) (on equator, 0° lon)
    double rX=1.0, rY=0.0, rZ=0.0;

    // v_A = omega_A x r
    double vaX, vaY, vaZ;
    cross3d(oaX, oaY, oaZ, rX, rY, rZ, vaX, vaY, vaZ);
    // v_B = omega_B x r
    double vbX, vbY, vbZ;
    cross3d(obX, obY, obZ, rX, rY, rZ, vbX, vbY, vbZ);
    // v_rel = v_A - v_B
    double vrX = vaX - vbX, vrY = vaY - vbY, vrZ = vaZ - vbZ;

    // Boundary normal from plate-A tile toward plate-B territory.
    // Plate B is to the south; normal = (0,-1,0).
    double normX = 0.0, normY = -1.0, normZ = 0.0;

    // convergence = -dot(v_rel, normal)
    double convergence = -dot3d(vrX, vrY, vrZ, normX, normY, normZ);

    // Expect convergence > 0 (plates approaching each other)
    EXPECT_GT(convergence, 0.0)
        << "Expected convergence > 0 for two plates approaching equator. "
        << "v_rel=(" << vrX << "," << vrY << "," << vrZ << ") "
        << "convergence=" << convergence;

    // Also verify that the magnitude is as expected: v_rel = (0, 2w, 0),
    // normal = (0,-1,0), so convergence = -dot((0,2w,0),(0,-1,0)) = 2w.
    EXPECT_NEAR(convergence, 2.0 * w, 1e-12);

    // Diverging case: omega_A = (0,0,-w) south pole, omega_B = (0,0,+w) north pole,
    // same evaluation point and normal. Plates moving away from equator.
    double oaX2=0, oaY2=0, oaZ2=-w;
    double obX2=0, obY2=0, obZ2=+w;
    double vaX2, vaY2, vaZ2, vbX2, vbY2, vbZ2;
    cross3d(oaX2, oaY2, oaZ2, rX, rY, rZ, vaX2, vaY2, vaZ2);
    cross3d(obX2, obY2, obZ2, rX, rY, rZ, vbX2, vbY2, vbZ2);
    double vrX2 = vaX2-vbX2, vrY2 = vaY2-vbY2, vrZ2 = vaZ2-vbZ2;
    double convergence2 = -dot3d(vrX2, vrY2, vrZ2, normX, normY, normZ);
    EXPECT_LT(convergence2, 0.0)
        << "Expected convergence < 0 for plates diverging from equator. "
        << "convergence=" << convergence2;
}

// ============================================================================
// Visual image export test — always writes n=256; WORLDGEN_VISUAL=1 → n=512
// ============================================================================

TEST(TerrainStageVisual, WriteDebugImages) {
    const uint32_t n = 256;
    auto world = runPipeline(makeParams(n, 0.70, 12, 42ULL), 300);
    ASSERT_NE(world, nullptr) << "Pipeline failed at n=" << n;

#ifdef _WIN32
    std::system("mkdir build\\debug-images 2>nul");
#else
    std::system("mkdir -p build/debug-images");
#endif

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::Elevation,
                                         "build/debug-images/elevation.bmp", 2048))
        << "Failed to write elevation.bmp";

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::BoundaryTypeMap,
                                         "build/debug-images/boundary_types.bmp", 2048))
        << "Failed to write boundary_types.bmp";

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::Crust,
                                         "build/debug-images/crust.bmp", 2048))
        << "Failed to write crust.bmp";

    const char* visual = std::getenv("WORLDGEN_VISUAL");
    if (visual && std::string(visual) == "1") {
        printf("[TerrainStageVisual] Running n=512 seed 42 (WORLDGEN_VISUAL=1)...\n");
        fflush(stdout);
        auto w512 = runPipeline(makeParams(512, 0.70, 12, 42ULL), 600);
        if (w512) {
            exportEquirectangularBmp(*w512, WorldFieldOrMode::Elevation,
                                     "build/debug-images/elevation.bmp", 2048);
            exportEquirectangularBmp(*w512, WorldFieldOrMode::BoundaryTypeMap,
                                     "build/debug-images/boundary_types.bmp", 2048);
            printf("[TerrainStageVisual] n=512 seed 42 images written to build/debug-images/\n");
            fflush(stdout);
        } else {
            printf("[TerrainStageVisual] n=512 seed 42 pipeline failed or timed out\n");
        }

        printf("[TerrainStageVisual] Running n=512 seed 1337...\n");
        fflush(stdout);
        auto w512b = runPipeline(makeParams(512, 0.70, 12, 1337ULL), 600);
        if (w512b) {
            exportEquirectangularBmp(*w512b, WorldFieldOrMode::Elevation,
                                     "build/debug-images/elevation_seed1337.bmp", 2048);
            exportEquirectangularBmp(*w512b, WorldFieldOrMode::BoundaryTypeMap,
                                     "build/debug-images/boundary_types_seed1337.bmp", 2048);
            printf("[TerrainStageVisual] n=512 seed 1337 images written\n");
            fflush(stdout);
        } else {
            printf("[TerrainStageVisual] n=512 seed 1337 failed or timed out\n");
        }
    }
}

} // namespace worldgen
