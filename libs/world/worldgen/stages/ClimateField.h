#pragma once

// Stage-local climate scratch fields shared by AtmosphereStage and
// PrecipitationStage. Nothing here is persisted to WorldData, so there is no
// PlanetIO format bump.

#include "worldgen/grid/SphereGrid.h"

#include <array>
#include <cstdint>
#include <vector>

namespace worldgen {

// Hadley and Ferrel cell boundary latitudes derived from sqrtRot = sqrt(rotationRate).
// Both AtmosphereStage (wind model) and PrecipitationStage (precip bands) use this
// formula; centralizing it here ensures they can never silently drift apart.
//   hadleyEdge = clamp(30 + 5*(sqrtRot - 1), 25, 35) degrees
//   ferrelEdge = hadleyEdge + 30 degrees
// Earth (rot=1, sqrtRot=1): hadleyEdge=30, ferrelEdge=60. Fast rotation (sqrtRot>1)
// narrows the cells; slow rotation (sqrtRot<1) widens them.
//
// Callers must pass sqrtRot computed from det_math::sqrt(rotationRate) to preserve
// determinism; this function is pure arithmetic with no transcendentals.
struct CirculationCellEdges {
    double hadleyEdge; // subtropical cell boundary (deg)
    double ferrelEdge; // subpolar cell boundary (deg)
};

inline CirculationCellEdges circulationCellEdges(double sqrtRot) {
    double raw = 30.0 + 5.0 * (sqrtRot - 1.0);
    double hadley = raw < 25.0 ? 25.0 : (raw > 35.0 ? 35.0 : raw);
    return {hadley, hadley + 30.0};
}

// Latitude-band base precipitation (mm/yr): simplified Hadley cell pattern.
// Named constants let PrecipitationStage.cpp and PrecipitationStage.test.cpp
// reference the same values — they can't silently desync.
//
// Band shapes (Earth-like, hadleyEdge=30, ferrelEdge=60):
//   ITCZ [0, itczEdge)      : 2000 -> (2000 - kItczDropPerDeg * itczEdge)
//   subtropical [itczEdge, hadleyEdge): piecewise toward ~200 at hadleyEdge
//   midlatitude [hadleyEdge, ferrelEdge): 200 rising toward ~750 at ferrelEdge
//   polar [ferrelEdge, 90]:  750 declining toward 0 at 90 deg
//
// The slope coefficients encode the desired mm/yr at key latitudes and are
// intentionally fixed to these physical anchors; changing them requires re-
// measuring the biome-fraction acceptance gates.
constexpr double kItczPeakMmYr       = 2000.0; // precip at the equator
constexpr double kItczDropPerDeg     = 20.0;   // rate (mm/yr/deg) * kItczHalfWidth / itczEdge
constexpr double kSubdrySlope        = 60.0;   // subtropical drying slope factor
constexpr double kSubdryPeakMmYr     = 200.0;  // minimum at the subtropical peak
constexpr double kMidlatSlope        = 18.3;   // midlatitude recovery slope factor
constexpr double kMidlatPeakMmYr     = 750.0;  // approximate midlatitude peak (at ferrelEdge)
constexpr double kPolarSlope         =   7.5;  // polar decline slope factor

// Base precipitation (mm/yr) for a tile at absLat degrees, given the circulation
// cell boundaries from circulationCellEdges(). The same formula used by
// PrecipitationStage.cpp and validated by PrecipitationStage.test.cpp.
inline double latitudePrecipBase(double absLat, double hadleyEdge, double ferrelEdge) {
    const double itczEdge   = 0.5 * hadleyEdge;   // ITCZ half-width (~15 deg Earth-like)
    const double subDryPeak = hadleyEdge;           // subtropical dry (~30 deg)
    if (absLat < itczEdge)
        return kItczPeakMmYr - absLat * (kItczDropPerDeg * 15.0 / itczEdge);
    if (absLat < subDryPeak)
        return kItczPeakMmYr - absLat * (kSubdrySlope * 30.0 / subDryPeak) + kSubdryPeakMmYr;
    if (absLat < ferrelEdge)
        return kSubdryPeakMmYr + (absLat - subDryPeak) * (kMidlatSlope * 30.0 / (ferrelEdge - subDryPeak));
    return kMidlatPeakMmYr - (absLat - ferrelEdge) * (kPolarSlope * 30.0 / (90.0 - ferrelEdge));
}

// Distance-to-nearest-ocean in tile hops (float). Multi-source BFS seeded from
// every ocean tile (elevation < seaLevel) at distance 0, enqueued in ascending
// TileId order over a FIFO, relaxing each unvisited neighbor to +1. Same
// deterministic pattern as TerrainStage's crustEdgeDist BFS: the seed order plus
// FIFO relaxation makes the result independent of thread count.
//
// OceanStage runs after both climate stages, so kFlagOcean is not yet set when
// this is called; ocean is detected purely by elevation < seaLevel. A world with
// no ocean tiles leaves every distance at 0 (the all-land degenerate case).
inline std::vector<float> computeDistanceToOcean(const SphereGrid& grid,
                                                 const std::vector<float>& elevation,
                                                 float seaLevel) {
    const uint32_t n = grid.tileCount();
    std::vector<float> dist(n, -1.0f);

    std::vector<uint32_t> queue;
    queue.reserve(n);
    for (uint32_t t = 0; t < n; ++t) {
        if (elevation[t] < seaLevel) {
            dist[t] = 0.0f;
            queue.push_back(t);
        }
    }

    std::array<TileId, 6> nbrs{};
    for (size_t qi = 0; qi < queue.size(); ++qi) {
        const uint32_t t  = queue[qi];
        const float    nd = dist[t] + 1.0f;
        const uint32_t cnt = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            const TileId nb = nbrs[k];
            if (dist[nb] < 0.0f) {
                dist[nb] = nd;
                queue.push_back(nb);
            }
        }
    }

    // No ocean at all (degenerate all-land world): leave distances at 0 so
    // continentality terms are a no-op rather than reading -1.
    for (uint32_t t = 0; t < n; ++t) {
        if (dist[t] < 0.0f) dist[t] = 0.0f;
    }
    return dist;
}

} // namespace worldgen
