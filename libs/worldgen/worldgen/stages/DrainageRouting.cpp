#include "worldgen/stages/DrainageRouting.h"

#include "worldgen/pipeline/GenerationStage.h" // CancelledException

#include <array>
#include <cassert>
#include <limits>
#include <queue>

namespace worldgen {

void routeDepressions(const SphereGrid& grid,
                      const std::vector<float>& elevation,
                      float seaLevel,
                      std::vector<float>& filled,
                      std::vector<TileId>& receiver,
                      const std::atomic<bool>& cancel) {
    const TileId totalTiles = grid.tileCount();
    assert(elevation.size() == totalTiles &&
           "routeDepressions: elevation must be sized to grid.tileCount()");
    filled.assign(totalTiles, std::numeric_limits<float>::infinity());
    receiver.assign(totalTiles, kInvalidTile);

    struct HeapItem {
        float  level; // filled water level this cell drains over
        TileId tile;
    };
    // Min-heap: lowest level first; equal levels ordered by ascending TileId.
    auto cmp = [](const HeapItem& a, const HeapItem& b) {
        if (a.level != b.level) return a.level > b.level;
        return a.tile > b.tile;
    };
    std::priority_queue<HeapItem, std::vector<HeapItem>, decltype(cmp)> heap(cmp);

    // Seed: every ocean tile is an outlet at its own terrain elevation. Land tiles
    // enter the heap only when first reached from an already-spilled cell.
    for (TileId t = 0; t < totalTiles; ++t) {
        if (elevation[t] < seaLevel) {
            filled[t] = elevation[t];
            heap.push({elevation[t], t});
        }
    }

    std::array<TileId, 6> nbs{};
    size_t processed = 0;
    while (!heap.empty()) {
        const HeapItem it = heap.top();
        heap.pop();
        // A tile can be pushed more than once (a lower level found later); skip the
        // stale entry whose level no longer matches.
        if (it.level != filled[it.tile]) continue;
        if ((++processed & 0xFFFFFu) == 0u && cancel.load(std::memory_order_relaxed)) {
            throw CancelledException{};
        }

        const TileId t = it.tile;
        const uint32_t cnt = grid.neighbors(t, nbs);
        for (uint32_t k = 0; k < cnt; ++k) {
            const TileId nb = nbs[k];
            if (elevation[nb] < seaLevel) continue; // outlet, fixed
            // The neighbor must rise to at least this cell's level to spill out through
            // it; if its own terrain is higher, that wins.
            float newLevel = it.level;
            if (elevation[nb] > newLevel) newLevel = elevation[nb];
            if (newLevel < filled[nb]) {
                filled[nb]   = newLevel;
                receiver[nb] = t; // one step toward the spill outlet
                heap.push({newLevel, nb});
            }
        }
    }
}

} // namespace worldgen
