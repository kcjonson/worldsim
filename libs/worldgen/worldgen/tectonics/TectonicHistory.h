#pragma once

#include "worldgen/data/WorldData.h" // BoundaryType
#include "worldgen/grid/SphereGrid.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace worldgen::tectonics {

// Per-coarse-tile crust classification in the output product.
enum class CrustType : uint8_t {
    None        = 0, // no crust resolved (transient gap before fill; never in output)
    Oceanic     = 1,
    Continental = 2,
};

// Which side of a boundary a tile sits on (mirrors TerrainStage's side codes).
inline constexpr uint8_t kSideSymmetric   = 0; // divergent / transform / CC
inline constexpr uint8_t kSideOverriding  = 1; // upper plate at a subduction zone
inline constexpr uint8_t kSideSubducting  = 2; // down-going plate

// Final per-plate summary carried out of the sim. Distinct from GeneratedWorld's
// PlateInfo: this is the simulated end-state with the cumulative rotation and the
// derived Euler pole, area-weighted from the crust raster.
struct TectonicPlate {
    int32_t id{};               // compacted plate id (0..count-1)
    bool    isContinental{};    // majority crust type
    Vec3d   eulerPole{};        // unit vector
    double  omegaRadPerMyr{};   // signed angular speed about the pole
    double  rotation[4]{1, 0, 0, 0}; // cumulative rotation quaternion (w,x,y,z)
    uint32_t area{};            // occupied coarse tiles at finalize
};

// The coarse tectonic-history output product (~ a few MB at coarseN=128).
// Held on GeneratedWorld as a non-serialized shared_ptr<const>. M-T1 fills the
// motion/crust/boundary fields; orogeny + volcanism stay at their "never"/0
// defaults until M-T2 events run.
struct TectonicHistory {
    uint32_t coarseN{};
    std::shared_ptr<const SphereGrid> grid; // the coarse grid the sim ran on

    // Per-coarse-tile arrays, length = grid->tileCount().
    std::vector<uint8_t>  plateId;          // 0..count-1, 255 = unassigned (should not occur post-fill)
    std::vector<uint8_t>  crustType;        // CrustType
    std::vector<uint16_t> crustAge;         // Myr since birth, clamped [0, kMaxStoredAgeMyr]
    std::vector<float>    thicknessKm;      // crustal thickness
    std::vector<int32_t>  orogenyAge;       // Myr since last orogeny, kOrogenyNever = none (M-T2)
    std::vector<float>    orogenyIntensity; // 0..1 (M-T2)
    std::vector<float>    volcanism;        // 0..1 (M-T2)
    std::vector<uint8_t>  boundaryType;     // BoundaryType
    std::vector<uint8_t>  boundarySide;     // kSide*
    std::vector<float>    convergence;      // -dot(vrel, outwardNormal), rad/Myr-scaled

    std::vector<TectonicPlate> plates;

    double historyMyr{}; // total simulated time

    void allocate(uint32_t tileCount) {
        plateId.assign(tileCount, 255);
        crustType.assign(tileCount, static_cast<uint8_t>(CrustType::None));
        crustAge.assign(tileCount, 0);
        thicknessKm.assign(tileCount, 0.0f);
        orogenyAge.assign(tileCount, 0);          // overwritten with kOrogenyNever in finalize
        orogenyIntensity.assign(tileCount, 0.0f);
        volcanism.assign(tileCount, 0.0f);
        boundaryType.assign(tileCount, static_cast<uint8_t>(BoundaryType::None));
        boundarySide.assign(tileCount, kSideSymmetric);
        convergence.assign(tileCount, 0.0f);
    }
};

// FNV-1a product hash over all per-tile arrays plus plate state, in a fixed
// order. Used by golden determinism tests. Reuses foundation's FNV idiom.
uint64_t computeTectonicHistoryHash(const TectonicHistory& h);

} // namespace worldgen::tectonics
