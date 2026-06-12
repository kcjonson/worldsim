// PlateStage + PlateMovementStage tests — M3a.
//
// Runs the real pipeline at small n and verifies the statistical contracts
// specified in the plan. Visual output test (WORLDGEN_VISUAL=1) writes BMPs
// to build/debug-images/.

#include "worldgen/pipeline/PlanetGenerator.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/debug/DebugImageExporter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace worldgen {

namespace {

// Run generator to completion (or timeout), return the world.
// Only PlateStage and PlateMovementStage are needed; the rest are stubs
// that don't depend on plates. We run the full pipeline since the stubs are cheap.
std::shared_ptr<const GeneratedWorld> runPipeline(const PlanetParams& params,
                                                   int timeoutSeconds = 120) {
    PlanetGenerator gen;
    gen.start(params);

    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state == GenerationProgress::State::Complete ||
            prog.state == GenerationProgress::State::Failed ||
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

// Hash array for determinism tests — hashes ALL bytes of each element.
template<typename T>
uint64_t arrayHash(const std::vector<T>& v) {
    static_assert(sizeof(T) <= 8);
    uint64_t h = 0x517CC1B727220A95ULL;
    for (const T& x : v) {
        uint8_t buf[sizeof(T)];
        std::memcpy(buf, &x, sizeof(T));
        for (size_t b = 0; b < sizeof(T); ++b) {
            h = h * 6364136223846793005ULL + buf[b];
        }
    }
    return h;
}

} // namespace

// ============================================================================
// Exact plate count: every id in [0,K) present, none empty, no id >= K
// ============================================================================

TEST(PlateStage, ExactPlateCount) {
    const int K = 12;
    auto world = runPipeline(makeParams(64, 0.70, K));
    ASSERT_NE(world, nullptr);
    ASSERT_FALSE(world->data.plateId.empty());

    const uint32_t N = static_cast<uint32_t>(world->data.plateId.size());
    std::vector<uint32_t> count(static_cast<size_t>(K), 0u);
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = world->data.plateId[t];
        ASSERT_LT(pid, static_cast<uint8_t>(K)) << "tile " << t << " has plateId >= K";
        count[static_cast<size_t>(pid)]++;
    }
    for (int p = 0; p < K; ++p) {
        EXPECT_GT(count[static_cast<size_t>(p)], 0u) << "plate " << p << " is empty";
    }
}

// ============================================================================
// Crust fraction within ±3% of (1-waterAmount)*1.12 for several waterAmount values
// ============================================================================

TEST(PlateStage, CrustFractionEarthLike) {
    const double waterAmount = 0.70;
    auto world = runPipeline(makeParams(128, waterAmount, 12));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.flags.size());
    uint32_t crusted = 0;
    for (uint32_t t = 0; t < N; ++t) {
        if ((world->data.flags[t] & kFlagContinentalCrust) != 0) ++crusted;
    }
    double actual  = static_cast<double>(crusted) / static_cast<double>(N);
    double target  = (1.0 - waterAmount) * 1.12;
    double absErr  = actual - target;
    if (absErr < 0.0) absErr = -absErr;
    EXPECT_LE(absErr, 0.03) << "crust fraction " << actual << " vs target " << target;
}

TEST(PlateStage, CrustFractionDesert) {
    const double waterAmount = 0.30;
    auto world = runPipeline(makeParams(128, waterAmount, 12, 0xABCD1234ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.flags.size());
    uint32_t crusted = 0;
    for (uint32_t t = 0; t < N; ++t) {
        if ((world->data.flags[t] & kFlagContinentalCrust) != 0) ++crusted;
    }
    double actual = static_cast<double>(crusted) / static_cast<double>(N);
    double target = (1.0 - waterAmount) * 1.12;
    double absErr = actual - target;
    if (absErr < 0.0) absErr = -absErr;
    EXPECT_LE(absErr, 0.03) << "crust fraction " << actual << " vs target " << target;
}

TEST(PlateStage, CrustFractionOcean) {
    const double waterAmount = 0.90;
    auto world = runPipeline(makeParams(128, waterAmount, 12, 0xDEAD4321ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.flags.size());
    uint32_t crusted = 0;
    for (uint32_t t = 0; t < N; ++t) {
        if ((world->data.flags[t] & kFlagContinentalCrust) != 0) ++crusted;
    }
    double actual = static_cast<double>(crusted) / static_cast<double>(N);
    double target = (1.0 - waterAmount) * 1.12;
    double absErr = actual - target;
    if (absErr < 0.0) absErr = -absErr;
    EXPECT_LE(absErr, 0.03) << "crust fraction " << actual << " vs target " << target;
}

TEST(PlateStage, CrustFractionMid) {
    const double waterAmount = 0.50;
    auto world = runPipeline(makeParams(128, waterAmount, 12, 0x11223344ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.flags.size());
    uint32_t crusted = 0;
    for (uint32_t t = 0; t < N; ++t) {
        if ((world->data.flags[t] & kFlagContinentalCrust) != 0) ++crusted;
    }
    double actual = static_cast<double>(crusted) / static_cast<double>(N);
    double target = (1.0 - waterAmount) * 1.12;
    double absErr = actual - target;
    if (absErr < 0.0) absErr = -absErr;
    EXPECT_LE(absErr, 0.03) << "crust fraction " << actual << " vs target " << target;
}

// ============================================================================
// Plate size diversity: with K=12, max/min area ratio >= 3
// Seed 0xFACEFEED42ULL is the baseline seed for this test. With growth-rate range
// [0.6, 2.4] (4:1 ratio), the Voronoi areas follow a lopsided distribution and
// this ratio is reliably >= 3 for K=12 at n=128.
// ============================================================================

TEST(PlateStage, PlateSizeDiversity) {
    const int K = 12;
    // Use a fixed seed known to produce the required ratio (derived empirically).
    auto world = runPipeline(makeParams(128, 0.70, K, 0xFACEFEED42ULL));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.plateId.size());
    std::vector<uint32_t> area(static_cast<size_t>(K), 0u);
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = world->data.plateId[t];
        if (pid < static_cast<uint8_t>(K)) area[static_cast<size_t>(pid)]++;
    }

    uint32_t maxArea = *std::max_element(area.begin(), area.end());
    uint32_t minArea = *std::min_element(area.begin(), area.end());
    ASSERT_GT(minArea, 0u) << "a plate has zero tiles";
    double ratio = static_cast<double>(maxArea) / static_cast<double>(minArea);
    EXPECT_GE(ratio, 3.0) << "max/min plate area ratio " << ratio << " < 3 (too uniform)";
}

// ============================================================================
// Passive margins: crusted tiles whose nearest boundary is > 8 tiles away
// must be > 20% of all crusted tiles.
//
// Method: BFS from plate boundaries inward; count crusted tiles at depth > 8.
// ============================================================================

TEST(PlateStage, PassiveMarginsExist) {
    auto world = runPipeline(makeParams(128, 0.70, 12));
    ASSERT_NE(world, nullptr);

    const uint32_t N = static_cast<uint32_t>(world->data.plateId.size());
    ASSERT_EQ(world->data.flags.size(), N);

    // BFS: seed with all boundary tiles at depth 0, expand inward.
    std::vector<int32_t> depth(N, -1);
    std::vector<uint32_t> queue;
    queue.reserve(N / 4);

    // Seed boundary tiles
    std::array<TileId, 6> nbrs{};
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = world->data.plateId[t];
        uint32_t cnt = world->grid->neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            if (world->data.plateId[nbrs[k]] != pid) {
                depth[t] = 0;
                queue.push_back(t);
                break;
            }
        }
    }

    // BFS expand to depth > 8
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        uint32_t t = queue[qi];
        if (depth[t] >= 8) continue; // don't expand beyond what we need
        uint32_t cnt = world->grid->neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            uint32_t nb = nbrs[k];
            if (depth[nb] < 0) {
                depth[nb] = depth[t] + 1;
                queue.push_back(nb);
            }
        }
    }

    // Count crusted tiles at depth > 8 vs total crusted
    uint32_t totalCrusted = 0, deepCrusted = 0;
    for (uint32_t t = 0; t < N; ++t) {
        if ((world->data.flags[t] & kFlagContinentalCrust) == 0) continue;
        ++totalCrusted;
        // depth < 0 means unreachable from boundaries (interior of large continent) → also deep
        if (depth[t] > 8 || depth[t] < 0) ++deepCrusted;
    }

    if (totalCrusted == 0) { GTEST_SKIP() << "No crusted tiles"; }
    double fraction = static_cast<double>(deepCrusted) / static_cast<double>(totalCrusted);
    EXPECT_GT(fraction, 0.20)
        << "Only " << (fraction * 100.0) << "% of crusted tiles are > 8 tiles from a boundary "
        << "(expected > 20% to confirm passive margins exist)";
}

// ============================================================================
// Determinism: same seed -> identical plateId + flags arrays
// ============================================================================

TEST(PlateStage, Deterministic) {
    auto p = makeParams(64, 0.70, 12, 0xCAFEBABEDEAD0000ULL);
    auto w1 = runPipeline(p);
    auto w2 = runPipeline(p);
    ASSERT_NE(w1, nullptr);
    ASSERT_NE(w2, nullptr);

    uint64_t h1plate = arrayHash(w1->data.plateId);
    uint64_t h2plate = arrayHash(w2->data.plateId);
    EXPECT_EQ(h1plate, h2plate) << "plateId array differs between runs with same seed";

    uint64_t h1flags = arrayHash(w1->data.flags);
    uint64_t h2flags = arrayHash(w2->data.flags);
    EXPECT_EQ(h1flags, h2flags) << "flags array differs between runs with same seed";
}

TEST(PlateStage, DifferentSeedsDiffer) {
    auto p1 = makeParams(64, 0.70, 12, 0xAAAAAAAAAAAAAAAAULL);
    auto p2 = makeParams(64, 0.70, 12, 0xBBBBBBBBBBBBBBBBULL);
    auto w1 = runPipeline(p1);
    auto w2 = runPipeline(p2);
    ASSERT_NE(w1, nullptr);
    ASSERT_NE(w2, nullptr);

    uint64_t h1 = arrayHash(w1->data.plateId);
    uint64_t h2 = arrayHash(w2->data.plateId);
    EXPECT_NE(h1, h2) << "different seeds produced identical plateId arrays";
}

// ============================================================================
// Momentum balance: |Σ area_i * omega_i| < 1e-3 * Σ area_i * |omega_i|
// ============================================================================

TEST(PlateMovementStage, MomentumBalance) {
    auto world = runPipeline(makeParams(64, 0.70, 12));
    ASSERT_NE(world, nullptr);

    const int K = static_cast<int>(world->plates.size());
    const uint32_t N = static_cast<uint32_t>(world->data.plateId.size());

    // Count per-plate area
    std::vector<uint32_t> area(static_cast<size_t>(K), 0u);
    for (uint32_t t = 0; t < N; ++t) {
        uint8_t pid = world->data.plateId[t];
        if (pid < static_cast<uint8_t>(K)) area[static_cast<size_t>(pid)]++;
    }

    // Compute weighted sums
    double netX = 0.0, netY = 0.0, netZ = 0.0;
    double totalOmegaMag = 0.0;
    for (int p = 0; p < K; ++p) {
        const auto& pl = world->plates[static_cast<size_t>(p)];
        double a = static_cast<double>(area[static_cast<size_t>(p)]);
        double omega = static_cast<double>(pl.angularSpeed);
        netX += a * pl.eulerPole.x * omega;
        netY += a * pl.eulerPole.y * omega;
        netZ += a * pl.eulerPole.z * omega;
        totalOmegaMag += a * omega;
    }

    double netMag = std::sqrt(netX*netX + netY*netY + netZ*netZ);
    EXPECT_LT(netMag, 1e-3 * totalOmegaMag)
        << "Angular momentum not balanced: |net| = " << netMag
        << ", 1e-3 * total = " << 1e-3 * totalOmegaMag;
}

// ============================================================================
// Debug image output test (n=256, writes to CWD/build/debug-images/).
// WORLDGEN_VISUAL=1: also runs n=512 images and reports.
// ============================================================================

TEST(PlateStageVisual, WriteDebugImages) {
    // Always run at n=256 to validate the exporter path.
    const uint32_t n = 256;
    auto world = runPipeline(makeParams(n, 0.70, 12, 42ULL), 300);
    ASSERT_NE(world, nullptr) << "Pipeline failed at n=256";

    // Create output directory (best-effort; if it fails the export will fail)
    // We write relative to cwd which is the build dir during ctest.
#ifdef _WIN32
    std::system("mkdir build\\debug-images 2>nul");
#else
    std::system("mkdir -p build/debug-images");
#endif

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::PlateId,
                                         "build/debug-images/plates_id.bmp", 1024))
        << "Failed to write plates_id.bmp";

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::Crust,
                                         "build/debug-images/plates_crust.bmp", 1024))
        << "Failed to write plates_crust.bmp";

    // High-res run only when env var set
    const char* visual = std::getenv("WORLDGEN_VISUAL");
    if (visual && std::string(visual) == "1") {
        printf("[PlateStageVisual] Running n=512 (WORLDGEN_VISUAL=1)...\n");
        auto world512 = runPipeline(makeParams(512, 0.70, 12, 42ULL), 300);
        if (world512) {
            exportEquirectangularBmp(*world512, WorldFieldOrMode::PlateId,
                                     "build/debug-images/plates_id_512.bmp", 2048);
            exportEquirectangularBmp(*world512, WorldFieldOrMode::Crust,
                                     "build/debug-images/plates_crust_512.bmp", 2048);
            printf("[PlateStageVisual] n=512 images written to build/debug-images/\n");
        } else {
            printf("[PlateStageVisual] n=512 pipeline failed or timed out\n");
        }
    }
}

} // namespace worldgen
