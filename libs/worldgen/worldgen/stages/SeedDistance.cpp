#include "worldgen/stages/SeedDistance.h"

#include "worldgen/pipeline/GenerationStage.h" // CancelledException

#include <math/DeterministicMath.h>

#include <array>
#include <deque>
#include <limits>

namespace worldgen {

namespace {

// Set on a tile's seed word while it waits in the queue, so it is queued once.
constexpr uint32_t kQueuedBit = 0x80000000u;

} // namespace

double meanSpacingChord(const SphereGrid& grid) {
    constexpr double kPi = 3.14159265358979323846;
    return foundation::det_math::sqrt(
        8.0 * kPi / (foundation::det_math::sqrt(3.0) * static_cast<double>(grid.tileCount())));
}

std::vector<TileCenterF> tileCentersF(const SphereGrid& grid, foundation::TaskPool& pool) {
    constexpr size_t         kGrain = 1u << 16;
    std::vector<TileCenterF> centers(grid.tileCount());
    pool.parallelFor(0, centers.size(), kGrain, [&](size_t begin, size_t end) {
        for (size_t t = begin; t < end; ++t) {
            const Vec3d c = grid.tileCenter(static_cast<TileId>(t));
            centers[t]    = {static_cast<float>(c.x), static_cast<float>(c.y), static_cast<float>(c.z)};
        }
    });
    return centers;
}

SeedDistanceResult seedDistance(const SeedDistanceArgs& args) {
    if (args.cancel.load(std::memory_order_relaxed)) throw CancelledException{};

    const SphereGrid&               grid       = args.grid;
    const std::vector<TileCenterF>& centers    = args.centers;
    const TileId                    totalTiles = grid.tileCount();
    const double      spacing    = meanSpacingChord(grid);
    const double      maxChord   = static_cast<double>(args.maxTiles) * spacing;

    // dist holds the squared chord to the nearest seed found so far (tile spacings after
    // the last pass). Starting at the cap means a tile is only reached from a seed closer
    // than maxTiles, which is what bounds the work.
    SeedDistanceResult    result;
    std::vector<float>&    dist    = result.distTiles;
    std::vector<uint32_t>& nearest = result.nearestSeed;
    dist.assign(totalTiles, static_cast<float>(maxChord * maxChord));
    nearest.assign(totalTiles, kNoSeed);

    std::array<TileId, 6> nbrs{};
    size_t processed = 0;
    auto pollCancel = [&] {
        if ((++processed & 0xFFFFFu) == 0u && args.cancel.load(std::memory_order_relaxed)) {
            throw CancelledException{};
        }
    };
    auto chord2 = [](const TileCenterF& a, const TileCenterF& b) {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    };

    // A tile sits in the queue at most once, so it never holds more than the frontier.
    std::deque<TileId> queue;
    for (uint32_t i = 0; i < args.seeds.size(); ++i) {
        const TileId s = args.seeds[i];
        if (nearest[s] != kNoSeed) continue; // listed twice: the first index keeps it
        dist[s]    = 0.0f;
        nearest[s] = i | kQueuedBit;
        queue.push_back(s);
    }
    while (!queue.empty()) {
        pollCancel();
        const TileId t = queue.front();
        queue.pop_front();
        nearest[t] &= ~kQueuedBit;
        const uint32_t seed = nearest[t];
        const TileCenterF& sc = centers[args.seeds[seed]];
        const uint32_t cnt  = grid.neighbors(t, nbrs);
        for (uint32_t k = 0; k < cnt; ++k) {
            const TileId nb = nbrs[k];
            if ((nearest[nb] & ~kQueuedBit) == seed) continue;
            const float d2 = chord2(centers[nb], sc);
            if (d2 >= dist[nb]) continue;
            dist[nb] = d2;
            const bool waiting = nearest[nb] != kNoSeed && (nearest[nb] & kQueuedBit) != 0u;
            nearest[nb] = seed | kQueuedBit;
            if (!waiting) queue.push_back(nb);
        }
    }

    for (TileId t = 0; t < totalTiles; ++t) {
        dist[t] = nearest[t] == kNoSeed
            ? args.maxTiles
            : static_cast<float>(
                  foundation::det_math::sqrt(static_cast<double>(dist[t])) / spacing);
    }
    return result;
}

} // namespace worldgen
