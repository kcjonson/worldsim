// worldgen-cli: headless world generation.
//
// Usage:
//   worldgen-cli --out <dir> [--n 256] [--seed 42] [--water 0.7]
//                [--plates 12] [--age 4.5e9] [--threads N]
//                [--verify-threads a,b]
//
// Outputs:
//   <dir>/<mode>.bmp  — equirectangular BMP for every debug mode
//   <dir>/stats.json  — WorldStats as JSON (hand-written, no deps)
//
// Exit codes: 0 ok, 1 generation failed, 2 thread-determinism mismatch,
//             3 bad arguments, 4 output error.

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/debug/DebugImageExporter.h"
#include "worldgen/debug/WorldStats.h"
#include "worldgen/pipeline/PlanetGenerator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MKDIR(p) mkdir((p), 0755)
#endif

// ============================================================================
// Argument parsing
// ============================================================================

struct CliArgs {
    uint32_t n{256};
    uint64_t seed{42};
    double   water{0.70};
    int      plates{12};
    double   age{4.5e9};
    int      threads{0};        // 0 = hardware default
    std::string outDir;
    int      verifyA{0};        // --verify-threads a,b (0 = not set)
    int      verifyB{0};
};

static void printUsage() {
    std::fprintf(stderr,
        "Usage: worldgen-cli --out <dir> [options]\n"
        "  --n       <int>    grid subdivision (default 256, tiles = 10*n*n+2)\n"
        "  --seed    <uint64> RNG seed (default 42)\n"
        "  --water   <float>  water fraction 0..1 (default 0.70)\n"
        "  --plates  <int>    tectonic plate count (default 12)\n"
        "  --age     <float>  planet age years (default 4.5e9)\n"
        "  --threads <int>    thread pool size 0=hw default (default 0)\n"
        "  --verify-threads a,b  run twice with thread counts a and b;\n"
        "                        exit 2 on worldHash mismatch\n"
    );
}

static bool parseArgs(int argc, char** argv, CliArgs& out) {
    for (int i = 1; i < argc; ++i) {
        auto eq = [&](const char* flag) { return std::strcmp(argv[i], flag) == 0; };
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "Missing value for %s\n", argv[i]); return nullptr; }
            return argv[++i];
        };

        if (eq("--out")) {
            const char* v = next(); if (!v) return false;
            out.outDir = v;
        } else if (eq("--n")) {
            const char* v = next(); if (!v) return false;
            out.n = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (eq("--seed")) {
            const char* v = next(); if (!v) return false;
            out.seed = std::strtoull(v, nullptr, 10);
        } else if (eq("--water")) {
            const char* v = next(); if (!v) return false;
            out.water = std::strtod(v, nullptr);
        } else if (eq("--plates")) {
            const char* v = next(); if (!v) return false;
            out.plates = static_cast<int>(std::strtol(v, nullptr, 10));
        } else if (eq("--age")) {
            const char* v = next(); if (!v) return false;
            out.age = std::strtod(v, nullptr);
        } else if (eq("--threads")) {
            const char* v = next(); if (!v) return false;
            out.threads = static_cast<int>(std::strtol(v, nullptr, 10));
        } else if (eq("--verify-threads")) {
            const char* v = next(); if (!v) return false;
            char* comma = const_cast<char*>(std::strchr(v, ','));
            if (!comma) {
                std::fprintf(stderr, "--verify-threads expects a,b\n");
                return false;
            }
            *comma = '\0';
            out.verifyA = static_cast<int>(std::strtol(v,      nullptr, 10));
            out.verifyB = static_cast<int>(std::strtol(comma+1, nullptr, 10));
        } else {
            std::fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }
    if (out.outDir.empty()) {
        std::fprintf(stderr, "--out <dir> is required\n");
        return false;
    }
    return true;
}

// ============================================================================
// Directory creation (best-effort, recursive not needed for single level)
// ============================================================================

static void mkdirP(const std::string& path) {
    MKDIR(path.c_str());
}

// ============================================================================
// Generation
// ============================================================================

using Clock = std::chrono::steady_clock;

struct RunResult {
    std::shared_ptr<const worldgen::GeneratedWorld> world;
    double wallSeconds{};
    std::vector<double> stageSeconds; // one per stage, in order (best-effort)
};

static RunResult runGeneration(const worldgen::PlanetParams& params, int threadCount) {
    RunResult res;
    worldgen::PlanetGenerator gen(static_cast<unsigned>(threadCount));

    auto t0 = Clock::now();
    gen.start(params);

    // Poll ~once/second: print stage progress to stdout.
    int lastStage = -1;
    auto stageStart = t0;
    std::vector<double> stageTimes;

    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        auto prog = gen.progress();

        if (prog.stageIndex != lastStage && prog.stageIndex >= 0) {
            if (lastStage >= 0) {
                double elapsed = std::chrono::duration<double>(Clock::now() - stageStart).count();
                stageTimes.push_back(elapsed);
            }
            stageStart = Clock::now();
            lastStage  = prog.stageIndex;
            std::printf("  stage %d: %s\n",
                        prog.stageIndex + 1,
                        prog.stageName ? prog.stageName : "?");
            std::fflush(stdout);
        }

        using State = worldgen::GenerationProgress::State;
        if (prog.state == State::Complete ||
            prog.state == State::Failed   ||
            prog.state == State::Cancelled) {
            if (lastStage >= 0) {
                double elapsed = std::chrono::duration<double>(Clock::now() - stageStart).count();
                stageTimes.push_back(elapsed);
            }
            break;
        }
    }

    res.world       = gen.takeResult();
    res.wallSeconds = std::chrono::duration<double>(Clock::now() - t0).count();
    res.stageSeconds = std::move(stageTimes);
    return res;
}

// ============================================================================
// JSON output (hand-written; no nlohmann dependency)
// ============================================================================

static void writeStatsJson(const std::string& path,
                           const worldgen::WorldStats& s,
                           double wallSeconds,
                           const std::vector<double>& stageSeconds) {
    std::FILE* fp = std::fopen(path.c_str(), "w");
    if (!fp) {
        std::fprintf(stderr, "Cannot write %s\n", path.c_str());
        return;
    }

    std::fprintf(fp, "{\n");
    std::fprintf(fp, "  \"tileCount\": %u,\n", s.tileCount);
    std::fprintf(fp, "  \"oceanFraction\": %.6f,\n", static_cast<double>(s.oceanFraction));

    // Hypsometry.
    std::fprintf(fp, "  \"hypsometry\": {\n");
    std::fprintf(fp, "    \"binMin\": %.2f,\n", static_cast<double>(s.hypsoBinMin));
    std::fprintf(fp, "    \"binWidth\": %.4f,\n", static_cast<double>(s.hypsoBinWidth));
    std::fprintf(fp, "    \"histogram\": [");
    for (size_t i = 0; i < s.hypsoHist.size(); ++i) {
        if (i > 0) std::fprintf(fp, ",");
        std::fprintf(fp, "%u", s.hypsoHist[i]);
    }
    std::fprintf(fp, "],\n");

    std::fprintf(fp, "    \"modeElevations\": [");
    for (size_t i = 0; i < s.modeElevations.size(); ++i) {
        if (i > 0) std::fprintf(fp, ",");
        std::fprintf(fp, "%.1f", static_cast<double>(s.modeElevations[i]));
    }
    std::fprintf(fp, "],\n");

    std::fprintf(fp, "    \"modeCounts\": [");
    for (size_t i = 0; i < s.modeCounts.size(); ++i) {
        if (i > 0) std::fprintf(fp, ",");
        std::fprintf(fp, "%u", s.modeCounts[i]);
    }
    std::fprintf(fp, "],\n");

    std::fprintf(fp, "    \"troughElevation\": %.1f,\n", static_cast<double>(s.troughElevation));
    std::fprintf(fp, "    \"troughFraction\": %.4f\n", static_cast<double>(s.troughFraction));
    std::fprintf(fp, "  },\n");

    // Plates.
    std::fprintf(fp, "  \"plates\": {\n");
    std::fprintf(fp, "    \"count\": %u,\n",
                 static_cast<unsigned>(s.plates.sortedAreas.size()));
    std::fprintf(fp, "    \"largestToSmallestRatio\": %.3f,\n",
                 static_cast<double>(s.plates.largestToSmallestRatio));
    std::fprintf(fp, "    \"logAreaRankR2\": %.4f,\n",
                 static_cast<double>(s.plates.logAreaRankR2));
    std::fprintf(fp, "    \"sortedAreas\": [");
    for (size_t i = 0; i < s.plates.sortedAreas.size(); ++i) {
        if (i > 0) std::fprintf(fp, ",");
        std::fprintf(fp, "%u", s.plates.sortedAreas[i]);
    }
    std::fprintf(fp, "]\n");
    std::fprintf(fp, "  },\n");

    // Mountain belts.
    std::fprintf(fp, "  \"mountainBelts\": {\n");
    std::fprintf(fp, "    \"beltCount\": %u,\n",
                 static_cast<unsigned>(s.belts.size()));
    std::fprintf(fp, "    \"medianElongation\": %.3f,\n",
                 static_cast<double>(s.medianBeltElongation));
    std::fprintf(fp, "    \"interiorMountainFraction\": %.4f,\n",
                 static_cast<double>(s.interiorMountainFraction));
    std::fprintf(fp, "    \"belts\": [");
    for (size_t i = 0; i < s.belts.size(); ++i) {
        if (i > 0) std::fprintf(fp, ",");
        std::fprintf(fp, "{\"tiles\":%u,\"elongation\":%.3f,\"widthKm\":%.1f}",
                     s.belts[i].tileCount,
                     static_cast<double>(s.belts[i].elongation),
                     static_cast<double>(s.belts[i].widthKm));
    }
    std::fprintf(fp, "]\n");
    std::fprintf(fp, "  },\n");

    // Continents.
    std::fprintf(fp, "  \"continents\": {\n");
    std::fprintf(fp, "    \"count\": %u,\n",
                 static_cast<unsigned>(s.continents.size()));
    std::fprintf(fp, "    \"medianIsoperimetric\": %.3f,\n",
                 static_cast<double>(s.medianIsoperimetric));
    std::fprintf(fp, "    \"continents\": [");
    for (size_t i = 0; i < s.continents.size(); ++i) {
        if (i > 0) std::fprintf(fp, ",");
        std::fprintf(fp, "{\"tiles\":%u,\"isoperimetric\":%.3f}",
                     s.continents[i].tileCount,
                     static_cast<double>(s.continents[i].isoperimetricRatio));
    }
    std::fprintf(fp, "]\n");
    std::fprintf(fp, "  },\n");

    // Timing.
    std::fprintf(fp, "  \"timing\": {\n");
    std::fprintf(fp, "    \"wallSeconds\": %.3f,\n", wallSeconds);
    std::fprintf(fp, "    \"stageSeconds\": [");
    for (size_t i = 0; i < stageSeconds.size(); ++i) {
        if (i > 0) std::fprintf(fp, ",");
        std::fprintf(fp, "%.3f", stageSeconds[i]);
    }
    std::fprintf(fp, "]\n");
    std::fprintf(fp, "  }\n");

    std::fprintf(fp, "}\n");
    std::fclose(fp);
}

// ============================================================================
// Human summary table
// ============================================================================

static void printSummary(const worldgen::WorldStats& s,
                         double wallSeconds,
                         uint64_t worldHash) {
    std::printf("\n=== WorldStats Summary ===\n");
    std::printf("  Tiles              : %u\n", s.tileCount);
    std::printf("  Ocean fraction     : %.4f\n", static_cast<double>(s.oceanFraction));

    if (s.modeElevations.size() >= 1) {
        std::printf("  Hypsometry mode 0  : %.0f m (%u tiles)\n",
                    static_cast<double>(s.modeElevations[0]),
                    s.modeCounts.size() >= 1 ? s.modeCounts[0] : 0u);
    }
    if (s.modeElevations.size() >= 2) {
        std::printf("  Hypsometry mode 1  : %.0f m (%u tiles)\n",
                    static_cast<double>(s.modeElevations[1]),
                    s.modeCounts.size() >= 2 ? s.modeCounts[1] : 0u);
        std::printf("  Hypsometry trough  : %.0f m (trough/lower-peak = %.2f)\n",
                    static_cast<double>(s.troughElevation),
                    static_cast<double>(s.troughFraction));
    }

    std::printf("  Plates             : %u  largest/smallest = %.1f  R^2(log-rank) = %.3f\n",
                static_cast<unsigned>(s.plates.sortedAreas.size()),
                static_cast<double>(s.plates.largestToSmallestRatio),
                static_cast<double>(s.plates.logAreaRankR2));

    std::printf("  Mountain belts     : %u  median elongation = %.2f  "
                "interior fraction = %.3f\n",
                static_cast<unsigned>(s.belts.size()),
                static_cast<double>(s.medianBeltElongation),
                static_cast<double>(s.interiorMountainFraction));

    std::printf("  Continents (>=0.5%): %u  median isoperimetric = %.2f\n",
                static_cast<unsigned>(s.continents.size()),
                static_cast<double>(s.medianIsoperimetric));

    std::printf("  Wall time          : %.2f s\n", wallSeconds);
    std::printf("  worldHash          : 0x%016llx\n",
                static_cast<unsigned long long>(worldHash));
    std::printf("\n");
}

// ============================================================================
// BMP export for every available mode
// ============================================================================

static const struct {
    worldgen::WorldFieldOrMode mode;
    const char* name;
} kModes[] = {
    { worldgen::WorldFieldOrMode::Elevation,       "elevation"       },
    { worldgen::WorldFieldOrMode::Temperature,     "temperature"     },
    { worldgen::WorldFieldOrMode::Precipitation,   "precipitation"   },
    { worldgen::WorldFieldOrMode::Biome,           "biome"           },
    { worldgen::WorldFieldOrMode::PlateId,         "plateid"         },
    { worldgen::WorldFieldOrMode::Ocean,           "ocean"           },
    { worldgen::WorldFieldOrMode::Crust,           "crust"           },
    { worldgen::WorldFieldOrMode::BoundaryTypeMap, "boundarytypemap" },
};

static bool exportAllBmps(const worldgen::GeneratedWorld& world,
                          const std::string& outDir) {
    bool ok = true;
    for (const auto& m : kModes) {
        std::string path = outDir + "/" + m.name + ".bmp";
        bool result = worldgen::exportEquirectangularBmp(world, m.mode, path, 2048);
        if (result) {
            std::printf("  wrote %s.bmp\n", m.name);
        } else {
            std::fprintf(stderr, "  WARN: failed to write %s.bmp\n", m.name);
            ok = false;
        }
    }
    return ok;
}

// ============================================================================
// main
// ============================================================================

int main(int argc, char** argv) {
    CliArgs args;
    if (!parseArgs(argc, argv, args)) {
        printUsage();
        return 3;
    }

    mkdirP(args.outDir);

    worldgen::PlanetParams params = worldgen::PlanetParams::preset(worldgen::Preset::EarthLike);
    params.gridSubdivision    = args.n;
    params.seed               = args.seed;
    params.waterAmount        = args.water;
    params.tectonicPlateCount = args.plates;
    params.planetAge          = args.age;

    std::printf("worldgen-cli  n=%u  seed=%llu  water=%.2f  plates=%d  age=%.3g  threads=%s\n",
                args.n,
                static_cast<unsigned long long>(args.seed),
                args.water,
                args.plates,
                args.age,
                args.threads == 0 ? "hw" : std::to_string(args.threads).c_str());
    std::printf("Generating...\n");
    std::fflush(stdout);

    auto res = runGeneration(params, args.threads);

    if (!res.world) {
        std::fprintf(stderr, "Generation failed or was cancelled.\n");
        return 1;
    }

    std::printf("Generation complete (%.2f s).  Exporting...\n", res.wallSeconds);
    std::fflush(stdout);

    // Export BMPs.
    exportAllBmps(*res.world, args.outDir);

    // Compute stats.
    worldgen::WorldStats stats = worldgen::computeWorldStats(*res.world);

    // Write JSON.
    std::string jsonPath = args.outDir + "/stats.json";
    writeStatsJson(jsonPath, stats, res.wallSeconds, res.stageSeconds);
    std::printf("  wrote stats.json\n");

    // Print human summary.
    printSummary(stats, res.wallSeconds, res.world->worldHash);

    // ---- Optional determinism check ----
    if (args.verifyA > 0 && args.verifyB > 0) {
        std::printf("Determinism check: running with threads=%d and threads=%d...\n",
                    args.verifyA, args.verifyB);
        std::fflush(stdout);

        auto ra = runGeneration(params, args.verifyA);
        auto rb = runGeneration(params, args.verifyB);

        if (!ra.world || !rb.world) {
            std::fprintf(stderr, "One of the verify runs failed.\n");
            return 1;
        }

        uint64_t ha = ra.world->worldHash;
        uint64_t hb = rb.world->worldHash;
        if (ha == hb) {
            std::printf("PASS: worldHash 0x%016llx matches across thread counts %d and %d\n",
                        static_cast<unsigned long long>(ha), args.verifyA, args.verifyB);
        } else {
            std::fprintf(stderr,
                         "FAIL: worldHash mismatch!\n"
                         "  threads=%d -> 0x%016llx\n"
                         "  threads=%d -> 0x%016llx\n",
                         args.verifyA, static_cast<unsigned long long>(ha),
                         args.verifyB, static_cast<unsigned long long>(hb));
            return 2;
        }
    }

    return 0;
}
