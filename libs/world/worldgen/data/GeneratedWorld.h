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
//   Within a generation pass, once a field is marked valid in validFields its
//   array MUST NOT be rewritten by a later stage; stages only write the arrays
//   for fields they introduce. Exception: the optional ice->climate feedback
//   re-runs the temperature-dependent tail (Atmosphere..Glacier) a second time,
//   so those climate-tail arrays are written once per pass. Tectonic/terrain/
//   ocean fields are written exactly once and stay immutable across both passes.
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
