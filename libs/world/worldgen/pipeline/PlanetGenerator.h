#pragma once

// PlanetGenerator: runs the 8-stage world generation pipeline on a background
// jthread, publishing per-stage snapshots via a shared_ptr.
//
// Snapshot immutability contract (IMPORTANT for downstream consumers):
//   Once a snapshot is published, its arrays for valid fields are READ-ONLY.
//   A stage only writes arrays it is introducing (bits not yet set in validFields).
//   Stages NEVER modify already-valid arrays.
//   In debug builds the generator records per-field FNV checksums at publication
//   and asserts they are unchanged before the next publication, catching violations.
//
// Threading:
//   - start() launches a std::jthread; progress/state are atomic.
//   - snapshot() and takeResult() are mutex-guarded pointer copies — O(1).
//   - cancel() sets a flag; the running stage must call throwIfCancelled() in
//     its slab loop to observe the flag within ~100ms wall time.

#include "worldgen/data/GeneratedWorld.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/pipeline/GenerationStage.h"

#include <threading/TaskPool.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace worldgen {

struct GenerationProgress {
    enum class State { Idle, Running, Complete, Cancelled, Failed };

    int         stageIndex{-1};  // index of current stage (-1 = not started)
    const char* stageName{nullptr};
    float       stageFraction{}; // 0..1 within current stage
    float       totalFraction{}; // 0..1 across all stages
    State       state{State::Idle};
};

class PlanetGenerator {
  public:
    // threadCount: 0 = hardware_concurrency - 1
    explicit PlanetGenerator(unsigned threadCount = 0);
    ~PlanetGenerator();

    PlanetGenerator(const PlanetGenerator&) = delete;
    PlanetGenerator& operator=(const PlanetGenerator&) = delete;

    // Start asynchronous generation. Cancels any in-progress run first.
    void start(const PlanetParams& params);

    // Request cancellation (non-blocking). Destructor also cancels+joins.
    void cancel();

    // Current progress snapshot (all atomics, no lock).
    GenerationProgress progress() const;

    // Latest published world snapshot (nullptr until first stage completes).
    // The returned shared_ptr is immutable per the contract above.
    std::shared_ptr<const GeneratedWorld> snapshot() const;

    // Take the final result (state must be Complete).
    // Returns nullptr if not complete. Clears the internal reference.
    std::shared_ptr<const GeneratedWorld> takeResult();

  private:
    void runPipeline(PlanetParams params);
    void publishSnapshot(std::shared_ptr<GeneratedWorld> world);

    // Debug: compute a simple FNV checksum over all valid arrays
    static uint64_t computeFieldChecksums(const GeneratedWorld& w);

    foundation::TaskPool pool;

    std::vector<std::unique_ptr<IGenerationStage>> stages;

    std::jthread                  worker;
    std::atomic<bool>             cancelFlag{false};

    // Progress state — all atomic so progress() needs no lock
    std::atomic<int>              atomicStageIndex{-1};
    std::atomic<float>            atomicStageFraction{0.0f};
    std::atomic<float>            atomicTotalFraction{0.0f};
    std::atomic<int>              atomicState{0}; // GenerationProgress::State

    // Published snapshot — mutex-guarded pointer copy
    mutable std::mutex            snapshotMutex;
    std::shared_ptr<GeneratedWorld> latestSnapshot;

    // Total stage weight sum (computed at construction)
    float totalWeight{};

    // Per-stage weight prefix sums for totalFraction calculation
    std::vector<float> weightPrefixSum;

#ifndef NDEBUG
    uint64_t lastPublishedChecksum{};
#endif
};

} // namespace worldgen
