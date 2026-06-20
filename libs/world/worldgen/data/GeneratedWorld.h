#pragma once

#include "worldgen/data/Biome.h"
#include "worldgen/data/PlanetParams.h"
#include "worldgen/data/WorldData.h"
#include "worldgen/grid/SphereGrid.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace worldgen::tectonics {
struct TectonicHistory;
}

namespace worldgen {

// Per-plate metadata built by CrustStage from TectonicHistory.
struct PlateInfo {
    Vec3d  eulerPole{};       // unit vector (rotation pole)
    float  angularSpeed{};    // radians per million years
    bool   isContinental{};
};

// High-level statistics computed after all stages complete.
struct WorldSummary {
    float landFraction{};
    std::array<uint32_t, static_cast<size_t>(Biome::Count)> biomeHistogram{};
    float meanTemperatureC{};
    uint32_t riverTileCount{};
    float habitability{}; // heuristic 0..1
};

// The complete output of a generation run.
//
// Snapshot immutability contract:
//   validFields is the sole authority for which arrays a snapshot reader may
//   read. Once a field is marked valid its array MUST NOT be rewritten while
//   that bit is set; stages only write the arrays for fields they introduce.
//
//   Exception — the optional ice->climate feedback re-runs the temperature-
//   dependent tail (Atmosphere..Glacier) a second time to rewrite those
//   climate-tail arrays in place. To stay within the contract the pipeline first
//   CLEARS those fields' validFields bits (and publishes a snapshot) so readers
//   skip the arrays while they are mid-rewrite; each pass-2 stage re-sets its bit
//   as it finishes, so no valid array is ever observed being overwritten.
//   Tectonic/terrain/ocean fields are written exactly once and stay valid and
//   immutable across both passes.
struct GeneratedWorld {
    PlanetParams         params;
    DerivedPlanetValues  derived;
    // Shared so snapshots share the immutable grid object.
    std::shared_ptr<const SphereGrid> grid;
    WorldData            data;
    float                seaLevelMeters{};
    std::vector<PlateInfo> plates;
    WorldSummary         summary;
    uint32_t             validFields{};  // WorldField bits
    uint64_t             worldHash{};

    // Coarse tectonic-history product. Set by TectonicHistoryStage, consumed by
    // CrustStage. Non-serialized: PlanetIO does not persist it; the pipeline
    // regenerates it on each run.
    std::shared_ptr<const tectonics::TectonicHistory> tectonicHistory;
};

} // namespace worldgen
