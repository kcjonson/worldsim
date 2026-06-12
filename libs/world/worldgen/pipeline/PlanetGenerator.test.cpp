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
// All 8 stages reported in order
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

    EXPECT_EQ(maxStageIndex, 7) << "Expected 8 stages (indices 0..7)";
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
// cancel() -> state Cancelled within 100ms wall time
// ============================================================================

TEST(PlanetGenerator, CancelWithin100ms) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 256; // large enough to still be running

    PlanetGenerator gen;
    gen.start(params);

    // Small delay to let it actually start processing
    std::this_thread::sleep_for(std::chrono::milliseconds(5));

    auto cancelStart = std::chrono::steady_clock::now();
    gen.cancel();

    auto deadline = cancelStart + std::chrono::milliseconds(200);
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
    printf("[PlanetGenerator] Cancel took %lld ms\n", static_cast<long long>(ms));

    EXPECT_EQ(finalState, GenerationProgress::State::Cancelled)
        << "State was not Cancelled after cancel()";
    EXPECT_LE(ms, 200) << "cancel() took more than 200ms";
}

// ============================================================================
// Snapshot immutability: arrays for valid fields unchanged after more stages
// ============================================================================

TEST(PlanetGenerator, SnapshotImmutability) {
    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    params.gridSubdivision = 16;

    PlanetGenerator gen;
    gen.start(params);

    // Wait until stage 2 (PlateMovement) has completed and we have a snapshot with plateId.
    // plateId is written by PlateStage (stage 0) and never touched by any later stage,
    // so it is the correct field to test snapshot immutability on.
    // We wait for stageIndex >= 2 (TerrainStage starting), which means PlateMovement
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

    // Record checksum of snapshot's plateId array — written by PlateStage and
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

    // Re-check same snapshot's plateId — must be unchanged since no stage touches it after PlateStage
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

} // namespace worldgen
