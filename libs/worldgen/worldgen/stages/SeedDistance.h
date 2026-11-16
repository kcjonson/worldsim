#pragma once

#include "worldgen/grid/SphereGrid.h"

#include <threading/TaskPool.h>

#include <atomic>
#include <cstdint>
#include <limits>
#include <vector>

namespace worldgen {

// Mean center spacing of the grid's tiles as a unit-sphere chord: N near-regular hexagons
// of area (sqrt(3)/2) s^2 cover 4 pi. Matches the grid's mean neighbor chord to ~0.1%.
double meanSpacingChord(const SphereGrid& grid);

// A tile centre in floats. The distance passes read every tile's centre several times over,
// and the grid computes one per call, so they work from a copy: 12 bytes a tile.
struct TileCenterF {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};
// Every tile's centre, filled across the pool.
std::vector<TileCenterF> tileCentersF(const SphereGrid& grid, foundation::TaskPool& pool);

struct SeedDistanceArgs {
    const SphereGrid&               grid;
    const std::vector<TileCenterF>& centers; // tileCentersF(grid)
    const std::vector<TileId>&      seeds;   // a tile listed twice keeps its first index
    const std::atomic<bool>&   cancel; // polled; throws CancelledException when set
    // Resolve distances only this far, in tile spacings; unset covers the whole sphere.
    float maxTiles = std::numeric_limits<float>::infinity();
};

inline constexpr uint32_t kNoSeed = 0xFFFFFFFFu;

struct SeedDistanceResult {
    std::vector<float>    distTiles;   // to the nearest seed, in mean tile spacings
    std::vector<uint32_t> nearestSeed; // index into args.seeds, or kNoSeed past maxTiles
};

// Straight-line distance from every tile center to its nearest seed's center, in mean
// tile spacings, and which seed that is. The contours are round, and the nearest-seed
// regions approximate the Voronoi cells of the seeds: where two seeds' waves meet, a tile
// can keep the farther seed, its distance then up to ~0.3 spacings long.
//
// Vector propagation: each tile carries its nearest seed found so far, and a neighbor
// offering a closer seed re-relaxes it (serial FIFO, strict improvement, so the result
// is deterministic at any thread count; on an exact tie the seed whose wave arrives
// first keeps the tile). Only tiles closer than maxTiles to a seed are ever visited,
// so the work scales with the band around the seeds; every tile farther out (every
// tile, given no seeds) reads exactly maxTiles. The nearest seed is the propagation's
// state, so it is always returned.
SeedDistanceResult seedDistance(const SeedDistanceArgs& args);

} // namespace worldgen
