#include "worldgen/pipeline/PlanetGenerator.h"

#include "worldgen/stages/AtmosphereStage.h"
#include "worldgen/stages/BiomeStage.h"
#include "worldgen/stages/OceanStage.h"
#include "worldgen/stages/PlateMovementStage.h"
#include "worldgen/stages/PlateStage.h"
#include "worldgen/stages/PrecipitationStage.h"
#include "worldgen/stages/SnowStage.h"
#include "worldgen/stages/TerrainStage.h"

#include <random/SplitMix64.h>
#include <utils/WorldHash.h>

#include <bit>
#include <cstring>
#include <numeric>

namespace worldgen {

// ============================================================================
// Construction
// ============================================================================

PlanetGenerator::PlanetGenerator(unsigned threadCount)
    : pool(threadCount) {
    stages.push_back(std::make_unique<PlateStage>());
    stages.push_back(std::make_unique<PlateMovementStage>());
    stages.push_back(std::make_unique<TerrainStage>());
    stages.push_back(std::make_unique<AtmosphereStage>());
    stages.push_back(std::make_unique<PrecipitationStage>());
    stages.push_back(std::make_unique<OceanStage>());
    stages.push_back(std::make_unique<BiomeStage>());
    stages.push_back(std::make_unique<SnowStage>());

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

        for (size_t i = 0; i < stages.size(); ++i) {
            if (cancelFlag.load(std::memory_order_acquire)) {
                atomicState.store(
                    static_cast<int>(GenerationProgress::State::Cancelled),
                    std::memory_order_release);
                return;
            }

            atomicStageIndex.store(static_cast<int>(i), std::memory_order_release);
            atomicStageFraction.store(0.0f, std::memory_order_release);

            float stageWeightBase = weightPrefixSum[i];

            // reportProgress lambda: maps [0,1] within stage to totalFraction.
            // Both stores are monotonic max via CAS loop so out-of-order slab
            // completions from the thread pool never push progress backwards.
            auto reportProgress = [&, i, stageWeightBase](float frac) {
                // Monotonic-max store for stageFraction
                float cur = atomicStageFraction.load(std::memory_order_relaxed);
                while (frac > cur &&
                       !atomicStageFraction.compare_exchange_weak(
                           cur, frac, std::memory_order_relaxed)) {}

                // Monotonic-max store for totalFraction
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
                cancelFlag
            };

            stages[i]->run(ctx);

            // Publish snapshot after stage completes
            publishSnapshot(world);
        }

        // Compute WorldSummary
        {
            uint32_t totalTiles = world->grid->tileCount();
            uint64_t landCount = 0;
            double tempSum = 0.0;
            for (uint32_t t = 0; t < totalTiles; ++t) {
                if ((world->data.flags[t] & kFlagOcean) == 0) {
                    ++landCount;
                    auto idx = static_cast<size_t>(world->data.biome[t]);
                    if (idx < static_cast<size_t>(Biome::Count)) {
                        world->summary.biomeHistogram[idx]++;
                    }
                }
                tempSum += static_cast<double>(world->data.temperatureMean[t]) * 0.1;
            }
            world->summary.landFraction   = static_cast<float>(landCount) / static_cast<float>(totalTiles);
            world->summary.meanTemperatureC = static_cast<float>(tempSum / totalTiles);
            world->summary.riverTileCount = 0;

            // Habitability: fraction of land in temperate/livable biomes
            uint64_t habitable = 0;
            habitable += world->summary.biomeHistogram[static_cast<size_t>(Biome::TropicalRainforest)];
            habitable += world->summary.biomeHistogram[static_cast<size_t>(Biome::TropicalSeasonalForest)];
            habitable += world->summary.biomeHistogram[static_cast<size_t>(Biome::TemperateDeciduousForest)];
            habitable += world->summary.biomeHistogram[static_cast<size_t>(Biome::TemperateRainforest)];
            habitable += world->summary.biomeHistogram[static_cast<size_t>(Biome::BorealForest)];
            habitable += world->summary.biomeHistogram[static_cast<size_t>(Biome::TropicalSavanna)];
            habitable += world->summary.biomeHistogram[static_cast<size_t>(Biome::TemperateGrassland)];
            world->summary.habitability = landCount > 0
                ? static_cast<float>(habitable) / static_cast<float>(landCount)
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
