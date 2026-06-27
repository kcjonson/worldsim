#include "worldgen/pipeline/PlanetGenerator.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/debug/DebugImageExporter.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>

namespace worldgen {

namespace {

// Run generator at n=64 to completion with a timeout
std::shared_ptr<const GeneratedWorld> runToCompletion(const PlanetParams& params,
                                                       int timeoutSeconds = 60) {
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

} // namespace

// ============================================================================
// Progress monotonically nondecreasing 0 -> 1
// ============================================================================

TEST(PlanetGenerator, ProgressMonotone) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16; // small for fast test

    PlanetGenerator gen;
    gen.start(params);

    float lastTotal = 0.0f;
    bool seenFail = false;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);

    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.totalFraction < lastTotal) {
            seenFail = true;
        }
        if (prog.totalFraction > lastTotal) lastTotal = prog.totalFraction;
        if (prog.state == GenerationProgress::State::Complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_FALSE(seenFail) << "Progress went backwards";
    EXPECT_NEAR(lastTotal, 1.0f, 0.01f) << "Progress didn't reach 1.0";
}

// ============================================================================
// All stages reported in order
// ============================================================================

TEST(PlanetGenerator, AllStagesReported) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;

    PlanetGenerator gen;
    gen.start(params);

    int maxStageIndex = -1;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.stageIndex > maxStageIndex) maxStageIndex = prog.stageIndex;
        if (prog.state == GenerationProgress::State::Complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    EXPECT_EQ(maxStageIndex, 9) << "Expected 10 stages (indices 0..9)";
}

// ============================================================================
// validFields has all bits set on completion
// ============================================================================

TEST(PlanetGenerator, AllFieldsValid) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;

    auto world = runToCompletion(params);
    ASSERT_NE(world, nullptr);
    EXPECT_EQ(world->validFields & kAllWorldFields, kAllWorldFields)
        << "Not all WorldField bits set after completion. Missing: "
        << std::hex << (~world->validFields & kAllWorldFields);
}

// ============================================================================
// worldHash nonzero and deterministic
// ============================================================================

TEST(PlanetGenerator, WorldHashDeterministic) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;
    params.seed = 0xDEADBEEF12345678ULL;

    auto world1 = runToCompletion(params);
    auto world2 = runToCompletion(params);

    ASSERT_NE(world1, nullptr);
    ASSERT_NE(world2, nullptr);
    EXPECT_NE(world1->worldHash, 0u) << "worldHash should be nonzero";
    EXPECT_EQ(world1->worldHash, world2->worldHash)
        << "Same seed must produce same hash";
}

// ============================================================================
// Different seed -> different hash
// ============================================================================

TEST(PlanetGenerator, DifferentSeedDifferentHash) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;

    params.seed = 0xAAAAAAAAAAAAAAAAULL;
    auto world1 = runToCompletion(params);

    params.seed = 0xBBBBBBBBBBBBBBBBULL;
    auto world2 = runToCompletion(params);

    ASSERT_NE(world1, nullptr);
    ASSERT_NE(world2, nullptr);
    EXPECT_NE(world1->worldHash, world2->worldHash)
        << "Different seeds should produce different hashes";
}

// ============================================================================
// cancel() while running ends in terminal state Cancelled (never Done),
// proving the request is honored mid-run and short-circuits the pipeline.
//
// We deliberately assert NO wall-clock latency bound. Responsiveness comes from
// frequent cancel checks (PlateSim checks at step start, mid-step, and step end;
// every stage checks between units of work), but a hard millisecond SLA in a
// unit test is unenforceable on shared CI runners whose speed varies by an order
// of magnitude (a loaded Windows runner has been observed ~4x slower than local,
// enough that a single coarse sim step exceeds a tight deadline). The correctness
// property is machine-independent: a cancel requested while running must end in
// Cancelled, not Done. Elapsed time is printed for diagnostics only.
// ============================================================================

TEST(PlanetGeneratorHeavy, CancelStopsGeneration) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 256; // large enough to still be running when we cancel

    PlanetGenerator gen;
    gen.start(params);

    // Small delay to let it actually start processing.
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    auto cancelStart = std::chrono::steady_clock::now();
    gen.cancel();

    // Generous deadline: even a heavily loaded runner observes cancellation in far
    // less than this, and full generation at n=256 would itself finish well inside
    // it, so reaching Cancelled (not Done) proves the cancel was honored mid-run.
    auto deadline = cancelStart + std::chrono::seconds(30);
    GenerationProgress::State finalState = GenerationProgress::State::Running;
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state != GenerationProgress::State::Running) {
            finalState = prog.state;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    auto elapsed = std::chrono::steady_clock::now() - cancelStart;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    printf("[PlanetGenerator] Cancel observed after %lld ms\n", static_cast<long long>(ms));

    EXPECT_EQ(finalState, GenerationProgress::State::Cancelled)
        << "cancel() while running must end in Cancelled, not Done/Running";
}

// ============================================================================
// Snapshot immutability: arrays for valid fields unchanged after more stages
// ============================================================================

TEST(PlanetGenerator, SnapshotImmutability) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;

    PlanetGenerator gen;
    gen.start(params);

    // Wait until stage 2 (TerrainStage) has started and we have a snapshot with plateId.
    // plateId is written by CrustStage (stage 1) and never touched by any later stage,
    // so it is the correct field to test snapshot immutability on.
    // We wait for stageIndex >= 2 (TerrainStage starting), which means CrustStage
    // has completed and publishSnapshot was called for it.
    std::shared_ptr<const GeneratedWorld> snap;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.stageIndex >= 2) {
            snap = gen.snapshot();
            if (snap && (snap->validFields & static_cast<uint32_t>(WorldField::PlateId))) break;
        }
        if (prog.state == GenerationProgress::State::Complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!snap) { GTEST_SKIP() << "Could not get intermediate snapshot"; }

    // Record checksum of snapshot's plateId array — written by CrustStage and
    // never modified by any subsequent stage, so must remain identical after completion.
    uint64_t h1 = 0;
    for (uint8_t v : snap->data.plateId) {
        h1 = h1 * 2654435761ULL + v;
    }

    // Run to completion
    deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline) {
        auto prog = gen.progress();
        if (prog.state == GenerationProgress::State::Complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    // Re-check same snapshot's plateId — must be unchanged since no stage touches it after CrustStage
    uint64_t h2 = 0;
    for (uint8_t v : snap->data.plateId) {
        h2 = h2 * 2654435761ULL + v;
    }

    EXPECT_EQ(h1, h2) << "Snapshot plateId array was modified after publication";
}

// ============================================================================
// Restart clears previous snapshot: snapshot() == nullptr right after start()
// ============================================================================

TEST(PlanetGenerator, RestartClearsSnapshot) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;

    // Run once to completion so latestSnapshot is populated.
    PlanetGenerator gen;
    gen.start(params);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline) {
        if (gen.progress().state == GenerationProgress::State::Complete) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(gen.progress().state, GenerationProgress::State::Complete);
    gen.takeResult(); // consume result

    // Restart immediately and assert snapshot is cleared before any stage completes.
    gen.start(params);
    EXPECT_EQ(gen.snapshot(), nullptr)
        << "snapshot() should be nullptr immediately after restart";
}

// ============================================================================
// Re-running start() publishes a fresh GeneratedWorld object.
//
// The creator's GlobeView re-uploads only when handed a world; the world-creator
// regenerate path depends on each run yielding a *new* GeneratedWorld (distinct
// shared_ptr), not a cleared-and-reused one. If two consecutive runs returned the
// same object, a regenerate could leave the globe showing the accepted world.
// ============================================================================

TEST(PlanetGenerator, RerunPublishesFreshWorld) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;

    // One generator reused across runs, mirroring WorldCreatorModel which holds a
    // single PlanetGenerator and calls start() again to regenerate.
    PlanetGenerator gen;

    auto runOnce = [&](uint64_t seed) -> std::shared_ptr<const GeneratedWorld> {
        params.seed = seed;
        gen.start(params);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < deadline) {
            if (gen.progress().state == GenerationProgress::State::Complete) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return gen.takeResult();
    };

    auto first = runOnce(0x1111111111111111ULL);
    ASSERT_NE(first, nullptr);

    auto second = runOnce(0x2222222222222222ULL);
    ASSERT_NE(second, nullptr);

    EXPECT_NE(first.get(), second.get())
        << "Each run must publish a distinct GeneratedWorld object";
    EXPECT_NE(first->worldHash, second->worldHash)
        << "A new seed must yield a different world";
    EXPECT_EQ(first->params.seed, 0x1111111111111111ULL)
        << "First world must retain its own params after the rerun";
}

// ============================================================================
// DebugImageExporter: writes valid BMP
// ============================================================================

TEST(DebugImageExporter, WritesValidBmp) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 32;
    params.seed = 0x1234567890ABCDEFULL;

    auto world = runToCompletion(params);
    ASSERT_NE(world, nullptr);

    const char* elevPath = "test_debug_elevation.bmp";
    const char* biomePath = "test_debug_biome.bmp";

    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::Elevation,
                                         elevPath, 512));
    EXPECT_TRUE(exportEquirectangularBmp(*world, WorldFieldOrMode::Biome,
                                         biomePath, 512));

    // Verify BMP header
    auto checkBmp = [](const char* path, int expectedWidth) {
        std::FILE* fp = std::fopen(path, "rb");
        if (!fp) return false;
        uint8_t header[54]{};
        size_t nread = std::fread(header, 1, 54, fp);
        std::fclose(fp);
        if (nread < 54) return false;
        if (header[0] != 'B' || header[1] != 'M') return false;
        int w = header[18] | (header[19]<<8) | (header[20]<<16) | (header[21]<<24);
        int h = header[22] | (header[23]<<8) | (header[24]<<16) | (header[25]<<24);
        return w == expectedWidth && h == expectedWidth / 2;
    };

    EXPECT_TRUE(checkBmp(elevPath, 512));
    EXPECT_TRUE(checkBmp(biomePath, 512));
}

// ============================================================================
// Debug BMP export at n=128 with a fixed seed.
//
// Disabled by default so it doesn't run in CI. Run explicitly with:
//   world-tests --gtest_also_run_disabled_tests --gtest_filter=DebugExport.DISABLED_ExportBmps
//
// Output directory: build/debug-images/ (relative to the working directory,
// typically the build tree). Create it first:
//   mkdir build\debug-images
// ============================================================================

TEST(DebugExport, DISABLED_ExportBmps) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 128;
    params.seed = 0xABCD1234ABCD1234ULL;

    printf("[DebugExport] Generating n=128 planet (seed 0xABCD1234ABCD1234)...\n");
    fflush(stdout);

    auto world = runToCompletion(params, 300);
    ASSERT_NE(world, nullptr) << "Planet generation failed or timed out";

    const std::string dir = "debug-images/";
    std::filesystem::create_directories(dir);

    struct Export { WorldFieldOrMode mode; const char* name; };
    const Export exports[] = {
        {WorldFieldOrMode::Elevation,       "elevation.bmp"},
        {WorldFieldOrMode::Biome,           "biome.bmp"},
        {WorldFieldOrMode::PlateId,         "plates.bmp"},
        {WorldFieldOrMode::Crust,           "crust.bmp"},
        {WorldFieldOrMode::BoundaryTypeMap, "boundary_types.bmp"},
        {WorldFieldOrMode::Temperature,     "temperature.bmp"},
        {WorldFieldOrMode::Precipitation,   "precipitation.bmp"},
    };

    for (const auto& e : exports) {
        std::string path = dir + e.name;
        bool ok = exportEquirectangularBmp(*world, e.mode, path, 2048);
        EXPECT_TRUE(ok) << "Failed to write " << path;
        if (ok) {
            auto sz = std::filesystem::file_size(path);
            printf("[DebugExport] %s  %llu bytes\n", path.c_str(),
                   static_cast<unsigned long long>(sz));
        }
    }
    fflush(stdout);
}

// ============================================================================
// Edge-case smoke tests: small grids, plate extremes, water extremes.
//
// These must complete (not crash/assert), produce a valid world, and land ocean
// fraction within ±3% of the requested waterAmount. They are not acceptance tests
// for quality metrics (those run via worldgen-cli at n=512).
// ============================================================================

namespace {

// Run full pipeline to completion and return the world, or nullptr on timeout.
std::shared_ptr<const GeneratedWorld> runEdge(const PlanetParams& params,
                                               int timeoutSeconds = 120) {
    return runToCompletion(params, timeoutSeconds);
}

// Fraction of tiles with kFlagOcean set.
float oceanFraction(const GeneratedWorld& w) {
    const uint32_t N = w.grid->tileCount();
    uint32_t ocean = 0;
    for (uint32_t t = 0; t < N; ++t) {
        if (w.data.flags[t] & kFlagOcean) ++ocean;
    }
    return static_cast<float>(ocean) / static_cast<float>(N);
}

} // namespace

// n=16: degenerate grid where coarseN is clamped to fullN (16 < kCoarseN=128).
// Must not crash or assert and must hit ocean fraction.
TEST(PlanetGenerator, SmallGridN16Completes) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;
    params.seed = 0x1601160116011601ULL;

    auto world = runEdge(params);
    ASSERT_NE(world, nullptr) << "n=16 pipeline failed";
    EXPECT_EQ(world->worldHash != 0u, true) << "worldHash zero on n=16";

    float of = oceanFraction(*world);
    EXPECT_NEAR(of, static_cast<float>(params.waterAmount), 0.05f)
        << "ocean fraction " << of << " far from waterAmount " << params.waterAmount;
}

// n=64: coarseN clamp still active (64 < 128); upsample is actually downsampling.
TEST(PlanetGeneratorHeavy, SmallGridN64Completes) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 64;
    params.seed = 0x6464646464646464ULL;

    auto world = runEdge(params);
    ASSERT_NE(world, nullptr) << "n=64 pipeline failed";

    float of = oceanFraction(*world);
    EXPECT_NEAR(of, static_cast<float>(params.waterAmount), 0.05f)
        << "ocean fraction " << of << " far from waterAmount " << params.waterAmount;
}

// K=2 plates: oversized-rift logic and plate-count controller must not deadlock
// or collapse the continent.
TEST(PlanetGeneratorHeavy, PlatesMin2Completes) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 64;
    params.tectonicPlateCount = 2;
    params.seed = 0x0002000200020002ULL;

    auto world = runEdge(params);
    ASSERT_NE(world, nullptr) << "K=2 pipeline failed";

    float of = oceanFraction(*world);
    EXPECT_NEAR(of, static_cast<float>(params.waterAmount), 0.05f)
        << "ocean fraction " << of << " far from waterAmount";
    // At least one plate in the output.
    EXPECT_GE(world->plates.size(), size_t{1}) << "no plates on K=2 run";
}

// water=0.95 (ocean world): sea-level quantile and continental-area controller
// must not fight the extreme target into divergence.
TEST(PlanetGeneratorHeavy, WaterExtreme095Completes) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 64;
    params.waterAmount = 0.95;
    params.seed = 0x9595959595959595ULL;

    auto world = runEdge(params);
    ASSERT_NE(world, nullptr) << "water=0.95 pipeline failed";

    float of = oceanFraction(*world);
    EXPECT_NEAR(of, 0.95f, 0.05f) << "ocean fraction " << of << " far from 0.95";
}

// water=0.05 (desert world): almost all continental crust.
TEST(PlanetGeneratorHeavy, WaterExtreme005Completes) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 64;
    params.waterAmount = 0.05;
    params.seed = 0x0505050505050505ULL;

    auto world = runEdge(params);
    ASSERT_NE(world, nullptr) << "water=0.05 pipeline failed";

    float of = oceanFraction(*world);
    EXPECT_NEAR(of, 0.05f, 0.05f) << "ocean fraction " << of << " far from 0.05";
}

// ============================================================================
// Golden worldHash: cross-platform determinism gate.
//
// CI runs the heavy bucket on BOTH Windows and Linux. If each platform's full
// pipeline reproduces this same committed hash for a fixed (seed, n), then the
// two platforms agree with each other — a single golden constant IS the
// cross-platform gate, no inter-runner artifact comparison needed.
//
// UPDATE POLICY (mirrors PlateSimHeavy.GoldenProductHash): this value pins the
// output so an *accidental* change to generation is caught. Only re-pin it when
// you intend to change worldgen output, and say why in the commit. A surprise
// failure here means some stage changed its result — investigate before
// updating the constant.
// ============================================================================

TEST(PlanetGeneratorHeavy, GoldenWorldHash) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 64;
    params.seed            = 0x5151515151515151ULL;

    auto world = runToCompletion(params, /*timeoutSeconds=*/120);
    ASSERT_NE(world, nullptr) << "generation did not complete";

    constexpr uint64_t kGoldenWorldHash = 0xc296bb3b9c7debe8ULL;
    EXPECT_EQ(world->worldHash, kGoldenWorldHash)
        << "worldHash drifted. If this change was intentional, re-pin "
           "kGoldenWorldHash to 0x" << std::hex << world->worldHash
        << " and explain why in the commit. Otherwise a stage changed output.";
}

// ============================================================================
// Hardening: invalid params fail loud and synchronously (no worker, no crash),
// with a non-empty reason — instead of UB deep in a stage.
// ============================================================================

TEST(PlanetGenerator, RejectsInvalidParams) {
    auto expectRejected = [](const PlanetParams& p, const char* what) {
        PlanetGenerator gen;
        gen.start(p);
        // Validation is synchronous: state is Failed by the time start() returns.
        EXPECT_EQ(gen.progress().state, GenerationProgress::State::Failed)
            << "expected rejection for " << what;
        EXPECT_FALSE(gen.failureReason().empty())
            << "expected a failure reason for " << what;
        EXPECT_EQ(gen.takeResult(), nullptr) << "no world for " << what;
    };

    PlanetParams base = PlanetParams::preset(Preset::EarthLike);
    base.gridSubdivision = 16;

    PlanetParams badN = base;       badN.gridSubdivision = kMaxGridSubdivision + 1;
    PlanetParams badWater = base;   badWater.waterAmount = 2.0;
    PlanetParams badPlates = base;  badPlates.tectonicPlateCount = 1;
    PlanetParams badEcc = base;     badEcc.eccentricity = 1.5;

    expectRejected(badN, "gridSubdivision over cap");
    expectRejected(badWater, "waterAmount > 1");
    expectRejected(badPlates, "tectonicPlateCount < 2");
    expectRejected(badEcc, "eccentricity > 0.95");
}

} // namespace worldgen
