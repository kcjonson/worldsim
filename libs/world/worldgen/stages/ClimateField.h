#pragma once

// Stage-local climate scratch fields shared by AtmosphereStage and
// PrecipitationStage. Nothing here is persisted to WorldData, so there is no
// PlanetIO format bump.

#include "worldgen/grid/SphereGrid.h"

#include <array>
#include <cstdint>
#include <vector>

namespace worldgen {

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
