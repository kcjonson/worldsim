#include "worldgen/pipeline/PlanetGenerator.h"

#include "worldgen/stages/AtmosphereStage.h"
#include "worldgen/stages/BiomeStage.h"
#include "worldgen/stages/CrustStage.h"
#include "worldgen/stages/ErosionStage.h"
#include "worldgen/stages/GlacierStage.h"
#include "worldgen/stages/OceanStage.h"
#include "worldgen/stages/PrecipitationStage.h"
#include "worldgen/stages/SnowStage.h"
#include "worldgen/stages/TectonicHistoryStage.h"
#include "worldgen/stages/TerrainStage.h"

#include <random/SplitMix64.h>
#include <utils/WorldHash.h>

#include <bit>
#include <cstring>
#include <numeric>

namespace worldgen {

namespace {

float habitabilityWeight(Biome b) {
    switch (b) {
        case Biome::TropicalRainforest:
        case Biome::TropicalSeasonalForest:
        case Biome::TemperateDeciduousForest:
        case Biome::TemperateRainforest:
        case Biome::BorealForest:
        case Biome::TropicalSavanna:
        case Biome::TemperateGrassland:
        case Biome::TemperateWetland:
        case Biome::TropicalWetland:
        case Biome::Beach:
            return 1.0f;
        case Biome::MontaneForest:
        case Biome::AlpineGrassland:
        case Biome::XericShrubland:
            return 0.5f;
        default:
            return 0.0f;
    }
}

// True if pass 1 produced any land ice (glacier) — the trigger for the optional
// ice->climate feedback re-run of the temperature-dependent tail.
bool worldHasLandIce(const GeneratedWorld& w) {
    for (uint8_t f : w.data.flags) {
        if (f & kFlagGlacier) return true;
    }
    return false;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

PlanetGenerator::PlanetGenerator(unsigned threadCount)
    : pool(threadCount) {
    stages.push_back(std::make_unique<TectonicHistoryStage>());
    stages.push_back(std::make_unique<CrustStage>());
    stages.push_back(std::make_unique<TerrainStage>());
    stages.push_back(std::make_unique<ErosionStage>());
    stages.push_back(std::make_unique<AtmosphereStage>());
    stages.push_back(std::make_unique<PrecipitationStage>());
    stages.push_back(std::make_unique<OceanStage>());
    stages.push_back(std::make_unique<BiomeStage>());
    stages.push_back(std::make_unique<SnowStage>());
    stages.push_back(std::make_unique<GlacierStage>());

    // Compute prefix sums for totalFraction calculation
    totalWeight = 0.0f;
    weightPrefixSum.resize(stages.size() + 1, 0.0f);
    for (size_t i = 0; i < stages.size(); ++i) {
        weightPrefixSum[i + 1] = weightPrefixSum[i] + stages[i]->weight();
        totalWeight += stages[i]->weight();
    }
}

PlanetGenerator::~PlanetGenerator() {
    cancel();
    if (worker.joinable()) worker.join();
}

// ============================================================================
// Control
// ============================================================================

void PlanetGenerator::start(const PlanetParams& params) {
    cancel();
    if (worker.joinable()) worker.join();

    // Clear the snapshot from any previous run so snapshot() returns nullptr
    // until the first stage of the new run completes (per API contract).
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        latestSnapshot.reset();
#ifndef NDEBUG
        lastPublishedChecksum = 0;
#endif
    }

    cancelFlag.store(false, std::memory_order_release);
    atomicState.store(static_cast<int>(GenerationProgress::State::Running),
                      std::memory_order_release);
    atomicStageIndex.store(-1, std::memory_order_release);
    atomicStageFraction.store(0.0f, std::memory_order_release);
    atomicTotalFraction.store(0.0f, std::memory_order_release);

    worker = std::jthread([this, params]() { runPipeline(params); });
}

void PlanetGenerator::cancel() {
    cancelFlag.store(true, std::memory_order_release);
}

// ============================================================================
// Progress
// ============================================================================

GenerationProgress PlanetGenerator::progress() const {
    GenerationProgress p;
    int stateInt = atomicState.load(std::memory_order_acquire);
    p.state = static_cast<GenerationProgress::State>(stateInt);
    int si   = atomicStageIndex.load(std::memory_order_acquire);
    p.stageIndex = si;
    p.stageFraction  = atomicStageFraction.load(std::memory_order_relaxed);
    p.totalFraction  = atomicTotalFraction.load(std::memory_order_relaxed);
    if (si >= 0 && si < static_cast<int>(stages.size())) {
        p.stageName = stages[static_cast<size_t>(si)]->name();
    }
    return p;
}

// ============================================================================
// Snapshot
// ============================================================================

void PlanetGenerator::publishSnapshot(std::shared_ptr<GeneratedWorld> world) {
    std::lock_guard<std::mutex> lock(snapshotMutex);
#ifndef NDEBUG
    // Under the lock so checksum and publication are one event from a
    // reader's perspective. The immutability contract itself is verified
    // by the SnapshotImmutability test.
    lastPublishedChecksum = computeFieldChecksums(*world);
#endif
    latestSnapshot = std::move(world);
}

std::shared_ptr<const GeneratedWorld> PlanetGenerator::snapshot() const {
    std::lock_guard<std::mutex> lock(snapshotMutex);
    return latestSnapshot;
}

std::shared_ptr<const GeneratedWorld> PlanetGenerator::takeResult() {
    auto state = static_cast<GenerationProgress::State>(
        atomicState.load(std::memory_order_acquire));
    if (state != GenerationProgress::State::Complete) return nullptr;
    std::lock_guard<std::mutex> lock(snapshotMutex);
    return std::move(latestSnapshot);
}

// ============================================================================
// Pipeline runner
// ============================================================================

void PlanetGenerator::runPipeline(PlanetParams params) {
    try {
        // Clamp tectonicPlateCount to the documented valid range [2, 30].
        // Plate count of 0 or 1 would produce modulo-by-zero or degenerate results.
        if (params.tectonicPlateCount < 2)  params.tectonicPlateCount = 2;
        if (params.tectonicPlateCount > 30) params.tectonicPlateCount = 30;

        // Build the GeneratedWorld
        auto world = std::make_shared<GeneratedWorld>();
        world->params  = params;
        world->derived = derive(params);
        world->grid    = std::make_shared<SphereGrid>(params.gridSubdivision);
        world->data.allocate(world->grid->tileCount());

        // Run one stage by index, optionally with the ice-feedback flag set (used
        // only on the second pass). Reuses deriveSeed(seed, i) so re-running a stage
        // is bit-identical apart from the feedback it reads.
        auto runStage = [&](size_t i, bool iceFeedback) {
            atomicStageIndex.store(static_cast<int>(i), std::memory_order_release);
            atomicStageFraction.store(0.0f, std::memory_order_release);

            float stageWeightBase = weightPrefixSum[i];

            // reportProgress lambda: maps [0,1] within stage to totalFraction. Both
            // stores are monotonic max via CAS loop so out-of-order slab completions
            // never push progress backwards (and so the second pass re-running the
            // tail never rewinds the bar).
            auto reportProgress = [&, i, stageWeightBase](float frac) {
                float cur = atomicStageFraction.load(std::memory_order_relaxed);
                while (frac > cur &&
                       !atomicStageFraction.compare_exchange_weak(
                           cur, frac, std::memory_order_relaxed)) {}

                float total = (stageWeightBase + stages[i]->weight() * frac) / totalWeight;
                cur = atomicTotalFraction.load(std::memory_order_relaxed);
                while (total > cur &&
                       !atomicTotalFraction.compare_exchange_weak(
                           cur, total, std::memory_order_relaxed)) {}
            };

            uint64_t stageSeed = foundation::deriveSeed(params.seed, static_cast<uint64_t>(i));

            StageContext ctx{
                params,
                world->derived,
                *world->grid,
                world->data,
                *world,
                pool,
                stageSeed,
                reportProgress,
                cancelFlag,
                iceFeedback
            };

            stages[i]->run(ctx);
            publishSnapshot(world);
        };

        // The climate tail (temperature-dependent stages) begins at AtmosphereStage;
        // everything before it (tectonics, terrain, ocean level) is unaffected by ice
        // and is never re-run.
        size_t tailStart = stages.size();
        for (size_t i = 0; i < stages.size(); ++i) {
            if (std::strcmp(stages[i]->name(), "Atmosphere") == 0) {
                tailStart = i;
                break;
            }
        }

        // Pass 1: the full pipeline, no ice feedback. Capture the validFields set just
        // before the climate tail so the feedback pass can invalidate exactly the
        // fields it rewrites.
        uint32_t preTailValid = 0;
        for (size_t i = 0; i < stages.size(); ++i) {
            if (cancelFlag.load(std::memory_order_acquire)) {
                atomicState.store(
                    static_cast<int>(GenerationProgress::State::Cancelled),
                    std::memory_order_release);
                return;
            }
            if (i == tailStart) preTailValid = world->validFields;
            runStage(i, /*iceFeedback=*/false);
        }

        // Ice -> climate feedback. If pass 1 grew land ice, re-run the temperature-
        // dependent tail once (a fixed two passes, not a convergence loop): the ice
        // cools its own surface (elevation lapse + albedo) so temperature, precip,
        // biomes, snow, and ice re-derive in equilibrium with it. A glacier-free world
        // skips this and pays nothing.
        //
        // Snapshot safety: pass 2 rewrites the climate-tail arrays IN PLACE on the
        // shared GeneratedWorld. Clear those fields' validFields bits first (and
        // publish) so concurrent snapshot readers — which treat validFields as the
        // sole authority for which arrays are safe to read at any instant — skip them
        // while they are being rewritten; each pass-2 stage re-validates its fields as
        // it finishes, restoring the full set by the end of the tail.
        if (worldHasLandIce(*world)) {
            world->validFields = preTailValid;
            publishSnapshot(world);
            for (size_t i = tailStart; i < stages.size(); ++i) {
                if (cancelFlag.load(std::memory_order_acquire)) {
                    atomicState.store(
                        static_cast<int>(GenerationProgress::State::Cancelled),
                        std::memory_order_release);
                    return;
                }
                runStage(i, /*iceFeedback=*/true);
            }
        }

        // Compute WorldSummary
        {
            uint32_t totalTiles = world->grid->tileCount();
            uint64_t landCount = 0;
            uint32_t riverTiles = 0;
            double tempSum = 0.0;
            for (uint32_t t = 0; t < totalTiles; ++t) {
                auto idx = static_cast<size_t>(world->data.biome[t]);
                if (idx < static_cast<size_t>(Biome::Count)) {
                    world->summary.biomeHistogram[idx]++;
                }
                if ((world->data.flags[t] & kFlagOcean) == 0) ++landCount;
                if (world->data.flags[t] & kFlagRiver) ++riverTiles;
                tempSum += static_cast<double>(world->data.temperatureMean[t]) * 0.1;
            }
            world->summary.landFraction   = static_cast<float>(landCount) / static_cast<float>(totalTiles);
            world->summary.meanTemperatureC = static_cast<float>(tempSum / totalTiles);
            world->summary.riverTileCount = riverTiles;

            // Habitability: land-area average of per-biome livability.
            // Weights — 1.0: forests, grasslands, savanna, wetlands, beach;
            // 0.5: montane forest, alpine grassland, xeric shrubland
            // (marginal); 0.0: deserts (hot/cold/semi/polar), tundra
            // (arctic/alpine), and water biomes (lakes count toward the land
            // denominator but contribute nothing).
            double habitable = 0.0;
            for (size_t i = 0; i < world->summary.biomeHistogram.size(); ++i) {
                habitable += static_cast<double>(world->summary.biomeHistogram[i]) *
                             habitabilityWeight(static_cast<Biome>(i));
            }
            world->summary.habitability = landCount > 0
                ? static_cast<float>(habitable / static_cast<double>(landCount))
                : 0.0f;
        }

        // Compute worldHash: FNV-1a over all valid field arrays in fixed order
        world->worldHash = computeFieldChecksums(*world);

        // Publish the final snapshot BEFORE marking Complete so a poller that
        // sees Complete and calls takeResult() always finds a ready snapshot.
        publishSnapshot(std::move(world));
        atomicTotalFraction.store(1.0f, std::memory_order_release);
        atomicState.store(static_cast<int>(GenerationProgress::State::Complete),
                          std::memory_order_release);

    } catch (const CancelledException&) {
        atomicState.store(static_cast<int>(GenerationProgress::State::Cancelled),
                          std::memory_order_release);
    } catch (...) {
        atomicState.store(static_cast<int>(GenerationProgress::State::Failed),
                          std::memory_order_release);
    }
}

// ============================================================================
// Checksum
// ============================================================================

uint64_t PlanetGenerator::computeFieldChecksums(const GeneratedWorld& w) {
    return computeWorldDataHash(w.validFields, w.data);
}

} // namespace worldgen
