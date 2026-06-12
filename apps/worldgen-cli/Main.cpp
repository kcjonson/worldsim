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
#include "worldgen/debug/ColorMaps.h"
#include "worldgen/debug/DebugImageExporter.h"
#include "worldgen/debug/WorldStats.h"
#include "worldgen/pipeline/PlanetGenerator.h"
#include "worldgen/tectonics/PlateSim.h"
#include "worldgen/tectonics/TectonicHistory.h"

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
    bool     simOnly{false};    // --sim-only: run PlateSim directly, dump frames
    uint32_t nCoarse{128};      // --n-coarse (sim-only)
    int      frameEvery{10};    // --frame-every (sim-only)
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
        "  --sim-only            run the coarse PlateSim directly (no pipeline),\n"
        "                        dump plateId + crustAge frames + final boundaryType\n"
        "  --n-coarse <int>      sim-only coarse subdivision (default 128)\n"
        "  --frame-every <int>   sim-only: dump a frame every N steps (default 10)\n"
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
        } else if (eq("--sim-only")) {
            out.simOnly = true;
        } else if (eq("--n-coarse")) {
            const char* v = next(); if (!v) return false;
            out.nCoarse = static_cast<uint32_t>(std::strtoul(v, nullptr, 10));
        } else if (eq("--frame-every")) {
            const char* v = next(); if (!v) return false;
            out.frameEvery = static_cast<int>(std::strtol(v, nullptr, 10));
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
// --sim-only mode: run PlateSim directly, dump frames + stats
// ============================================================================

static worldgen::ExportRgb crustAgeColor(uint16_t ageMyr, uint8_t crustType) {
    using CT = worldgen::tectonics::CrustType;
    if (crustType == static_cast<uint8_t>(CT::Continental)) {
        return {70, 110, 60}; // continental crust: muted green, age ramp is for ocean
    }
    if (crustType == static_cast<uint8_t>(CT::None)) {
        return {0, 0, 0};
    }
    // Oceanic: young (age 0) bright/white near ridge -> old deep blue.
    // Ramp over 0..200 Myr.
    float t = static_cast<float>(ageMyr) / 200.0f;
    if (t > 1.0f) t = 1.0f;
    auto r = static_cast<uint8_t>(235 - static_cast<int>(t * 235));
    auto g = static_cast<uint8_t>(245 - static_cast<int>(t * 165));
    auto b = static_cast<uint8_t>(255 - static_cast<int>(t * 95));
    return {r, g, b};
}

static worldgen::ExportRgb boundaryTypeColor(uint8_t bt) {
    switch (bt) {
        case 1: return {220, 30,  30};  // ConvergentCC
        case 2: return {220, 140, 30};  // ConvergentCO
        case 3: return {220, 210, 30};  // ConvergentOO
        case 4: return {30,  80,  220}; // Divergent
        case 5: return {30,  180, 80};  // Transform
        default: return {40, 40, 50};
    }
}

// orogenyAge: young orogens (recent) bright red -> old scars dark; never = grey.
static worldgen::ExportRgb orogenyAgeColor(int32_t ageMyr) {
    if (ageMyr == worldgen::tectonics::kOrogenyNever) return {30, 35, 45};
    float t = static_cast<float>(ageMyr) / 800.0f;
    if (t > 1.0f) t = 1.0f;
    // hot (recent) -> cool/dark (old): red->maroon.
    auto r = static_cast<uint8_t>(240 - static_cast<int>(t * 150));
    auto gg = static_cast<uint8_t>(180 - static_cast<int>(t * 150));
    auto b = static_cast<uint8_t>(60  - static_cast<int>(t * 40));
    return {r, gg, b};
}

// 0..1 field (orogeny intensity / volcanism) on a black->yellow->white ramp.
static worldgen::ExportRgb fieldColor01(float v) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    auto r = static_cast<uint8_t>(static_cast<int>(v * 255));
    auto gg = static_cast<uint8_t>(static_cast<int>(v * 220));
    auto b = static_cast<uint8_t>(static_cast<int>(v * v * 120));
    return {r, gg, b};
}

static int runSimOnly(const CliArgs& args) {
    using namespace worldgen;
    using namespace worldgen::tectonics;

    PlanetParams params = PlanetParams::preset(Preset::EarthLike);
    DerivedPlanetValues derived = derive(params);

    PlateSimParams sp;
    sp.coarseN        = args.nCoarse;
    sp.seed           = args.seed;
    sp.plateCount     = args.plates;
    sp.waterAmount    = args.water;
    sp.planetAge      = args.age;
    sp.planetRadiusKm = derived.planetRadiusMeters / 1000.0;

    std::printf("sim-only  n-coarse=%u  tiles=%u  seed=%llu  plates=%d  water=%.2f  age=%.3g\n",
                args.nCoarse,
                (10u * args.nCoarse * args.nCoarse + 2u),
                static_cast<unsigned long long>(args.seed),
                args.plates, args.water, args.age);
    std::fflush(stdout);

    auto t0 = Clock::now();
    PlateSim sim(sp);
    double initSec = std::chrono::duration<double>(Clock::now() - t0).count();
    std::printf("  init: %.3f s  steps=%d  history=%.0f Myr\n",
                initSec, sim.stepCount(), sim.historyMyr());
    std::fflush(stdout);

    const SphereGrid& g = sim.grid();
    const int frameEvery = args.frameEvery < 1 ? 1 : args.frameEvery;

    // Build a TectonicHistory-like view each frame via finalize() is too costly;
    // instead dump directly from owner/resolvedCrust. crustAge = nowMyr - birthMyr.
    auto dumpFrame = [&](int frameIdx) {
        const auto& owner = sim.owner();
        const auto& crust = sim.resolvedCrust();
        int32_t nowI = static_cast<int32_t>(sim.nowMyr() + 0.5);
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s/frame_%03d_plateid.bmp", args.outDir.c_str(), frameIdx);
        exportEquirectangularBmp(g, [&](TileId t) -> ExportRgb {
            uint8_t pid = owner[t];
            if (pid == 255) return {0, 0, 0};
            Rgb c = plateColor(pid);
            return {c.r, c.g, c.b};
        }, buf, 2048);
        std::snprintf(buf, sizeof(buf), "%s/frame_%03d_crustage.bmp", args.outDir.c_str(), frameIdx);
        exportEquirectangularBmp(g, [&](TileId t) -> ExportRgb {
            const CrustCell& cc = crust[t];
            int32_t age = nowI - cc.birthMyr;
            if (age < 0) age = 0;
            if (age > 65534) age = 65534;
            return crustAgeColor(static_cast<uint16_t>(age), static_cast<uint8_t>(cc.type));
        }, buf, 2048);
        std::snprintf(buf, sizeof(buf), "%s/frame_%03d_orogenyage.bmp", args.outDir.c_str(), frameIdx);
        exportEquirectangularBmp(g, [&](TileId t) -> ExportRgb {
            const CrustCell& cc = crust[t];
            if (cc.type != CrustType::Continental)
                return {15, 20, 35}; // ocean: dark
            int32_t oage = cc.orogenyMyr == kOrogenyNever ? kOrogenyNever : (nowI - cc.orogenyMyr);
            return orogenyAgeColor(oage);
        }, buf, 2048);
    };

    // Frame 0: initial state needs a rasterize. Run one finalize-style pass by
    // stepping zero times: rasterize via a private path is internal, so we dump
    // after the first step instead and label frames by step index.
    double stepTotal = 0.0, stepMax = 0.0;
    int frameIdx = 0;
    int total = sim.stepCount();
    uint32_t aliveMin = 0xFFFFFFFFu, aliveMax = 0;
    uint64_t ccFirst = 0, ccLast = 0; bool ccFirstSet = false;
    for (int s = 0; s < total; ++s) {
        auto ts = Clock::now();
        sim.step();
        double dt = std::chrono::duration<double>(Clock::now() - ts).count();
        stepTotal += dt;
        if (dt > stepMax) stepMax = dt;
        uint32_t al = sim.aliveCount();
        if (al < aliveMin) aliveMin = al;
        if (al > aliveMax) aliveMax = al;
        uint64_t cc = sim.continentalContinentalOverlaps();
        if (!ccFirstSet) { ccFirst = cc; ccFirstSet = true; }
        ccLast = cc;
        if ((s % frameEvery) == 0 || s == total - 1) {
            dumpFrame(frameIdx++);
        }
    }

    auto history = sim.finalize();
    double wall = std::chrono::duration<double>(Clock::now() - t0).count();

    // Final boundaryType + orogeny + volcanism frames.
    {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "%s/final_boundarytype.bmp", args.outDir.c_str());
        exportEquirectangularBmp(g, [&](TileId t) -> ExportRgb {
            return boundaryTypeColor(history->boundaryType[t]);
        }, buf, 2048);
        std::snprintf(buf, sizeof(buf), "%s/final_orogenyage.bmp", args.outDir.c_str());
        exportEquirectangularBmp(g, [&](TileId t) -> ExportRgb {
            if (history->crustType[t] != static_cast<uint8_t>(CrustType::Continental))
                return {15, 20, 35};
            return orogenyAgeColor(history->orogenyAge[t]);
        }, buf, 2048);
        std::snprintf(buf, sizeof(buf), "%s/final_orogenyintensity.bmp", args.outDir.c_str());
        exportEquirectangularBmp(g, [&](TileId t) -> ExportRgb {
            return fieldColor01(history->orogenyIntensity[t]);
        }, buf, 2048);
        std::snprintf(buf, sizeof(buf), "%s/final_volcanism.bmp", args.outDir.c_str());
        exportEquirectangularBmp(g, [&](TileId t) -> ExportRgb {
            return fieldColor01(history->volcanism[t]);
        }, buf, 2048);
    }

    // ---- Stats ----
    const uint32_t N = g.tileCount();
    // Plate areas (final, compacted).
    std::vector<uint32_t> area(history->plates.size(), 0u);
    for (TileId t = 0; t < N; ++t) {
        uint8_t pid = history->plateId[t];
        if (pid < area.size()) area[pid]++;
    }
    // Ocean age min/mean/max + how much exceeds the 220 Myr acceptance ceiling.
    uint32_t oceanCount = 0;
    uint64_t ageSum = 0;
    uint16_t ageMin = 65535, ageMax = 0;
    uint32_t contCount = 0;
    uint32_t ageOver220 = 0;
    std::vector<uint16_t> oceanAges;
    oceanAges.reserve(N);
    for (TileId t = 0; t < N; ++t) {
        if (history->crustType[t] == static_cast<uint8_t>(CrustType::Oceanic)) {
            uint16_t a = history->crustAge[t];
            ++oceanCount; ageSum += a;
            if (a < ageMin) ageMin = a;
            if (a > ageMax) ageMax = a;
            if (a > 220) ++ageOver220;
            oceanAges.push_back(a);
        } else if (history->crustType[t] == static_cast<uint8_t>(CrustType::Continental)) {
            ++contCount;
        }
    }
    double ageMean = oceanCount ? static_cast<double>(ageSum) / oceanCount : 0.0;
    uint16_t ageP99 = 0;
    if (!oceanAges.empty()) {
        std::sort(oceanAges.begin(), oceanAges.end());
        ageP99 = oceanAges[static_cast<size_t>(oceanAges.size() * 0.99)];
    }
    double overFrac = oceanCount ? 100.0 * ageOver220 / oceanCount : 0.0;

    // Orogeny coverage: fraction of continental tiles carrying an orogeny stamp.
    uint32_t orogenyTiles = 0;
    for (TileId t = 0; t < N; ++t) {
        if (history->crustType[t] != static_cast<uint8_t>(CrustType::Continental)) continue;
        if (history->orogenyAge[t] != kOrogenyNever) ++orogenyTiles;
    }
    double orogenyCoverage = contCount ? 100.0 * orogenyTiles / contCount : 0.0;

    // Continental drift: count vs. initial continental cell budget.
    uint32_t simContCells = sim.continentalCellCount();
    double contTarget = (1.0 - args.water) * 1.12 * static_cast<double>(N);
    double driftPct = contTarget > 0.0
        ? 100.0 * (static_cast<double>(simContCells) - contTarget) / contTarget : 0.0;

    std::printf("\n=== sim-only stats ===\n");
    std::printf("  wall            : %.3f s  (step total %.3f s, max step %.4f s)\n",
                wall, stepTotal, stepMax);
    std::printf("  plates (final)  : %u\n", static_cast<unsigned>(history->plates.size()));
    std::printf("  plate areas     :");
    {
        std::vector<uint32_t> sorted = area;
        std::sort(sorted.rbegin(), sorted.rend());
        for (uint32_t a : sorted) std::printf(" %u", a);
        std::printf("\n");
        if (!sorted.empty() && sorted.back() > 0)
            std::printf("  largest/smallest: %.1f\n",
                        static_cast<double>(sorted.front()) / static_cast<double>(sorted.back()));
    }
    std::printf("  continental cells (in-raster) : %u  (target %.0f, drift %+.2f%%)\n",
                simContCells, contTarget, driftPct);
    std::printf("  continental cells (resolved)  : %u\n", contCount);
    std::printf("  events          : merges %u  rifts %u  accretions %u\n",
                sim.mergeCount(), sim.riftCount(), sim.accretionCount());
    std::printf("  alive plates    : start %d  min %u  max %u  final %u\n",
                args.plates, aliveMin == 0xFFFFFFFFu ? 0u : aliveMin, aliveMax,
                static_cast<unsigned>(history->plates.size()));
    std::printf("  CC overlap trend: first %llu -> last %llu\n",
                static_cast<unsigned long long>(ccFirst),
                static_cast<unsigned long long>(ccLast));
    std::printf("  orogeny coverage: %.1f%% of continental tiles (%u stamped)\n",
                orogenyCoverage, orogenyTiles);
    std::printf("  ocean age (Myr) : min %u  mean %.1f  p99 %u  max %u  (>220: %.2f%%, n=%u)\n",
                ageMin == 65535 ? 0u : ageMin, ageMean, ageP99, ageMax, overFrac, oceanCount);
    std::printf("  product hash    : 0x%016llx\n",
                static_cast<unsigned long long>(computeTectonicHistoryHash(*history)));
    std::printf("  frames written  : %d (plateid+crustage+orogenyage) + 4 final maps\n", frameIdx);
    std::fflush(stdout);
    return 0;
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

    if (args.simOnly) {
        return runSimOnly(args);
    }

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
