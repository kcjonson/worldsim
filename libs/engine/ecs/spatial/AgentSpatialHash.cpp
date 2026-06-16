#include "AgentSpatialHash.h"

#include <cmath>

namespace ecs {

AgentSpatialHash::AgentSpatialHash(float cellSize)
    : m_cellSize(cellSize) {}

void AgentSpatialHash::clear() {
    // Drop every cell so the map can't accumulate empty entries as agents move
    // across the world over a session. Agent counts are small, so the per-frame
    // rebuild cost is negligible.
    m_cells.clear();
}

void AgentSpatialHash::insert(EntityID e, glm::vec2 pos) {
    auto [cx, cy] = cellCoords(pos);
    m_cells[cellKey(cx, cy)].push_back(e);
}

void AgentSpatialHash::queryNeighbors(glm::vec2 center, float radius, std::vector<EntityID>& out) const {
    out.clear();

    auto [minCx, minCy] = cellCoords({center.x - radius, center.y - radius});
    auto [maxCx, maxCy] = cellCoords({center.x + radius, center.y + radius});

    for (int32_t cy = minCy; cy <= maxCy; ++cy) {
        for (int32_t cx = minCx; cx <= maxCx; ++cx) {
            auto it = m_cells.find(cellKey(cx, cy));
            if (it != m_cells.end()) {
                for (EntityID e : it->second) {
                    out.push_back(e);
                }
            }
        }
    }
}

int64_t AgentSpatialHash::cellKey(int32_t cx, int32_t cy) {
    // Pack the two int32 bit patterns via unsigned arithmetic; shifting a
    // negative signed value is at best implementation-defined.
    const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(cx));
    const uint64_t uy = static_cast<uint64_t>(static_cast<uint32_t>(cy));
    return static_cast<int64_t>((ux << 32) | uy);
}

std::pair<int32_t, int32_t> AgentSpatialHash::cellCoords(glm::vec2 pos) const {
    return {
        static_cast<int32_t>(std::floor(pos.x / m_cellSize)),
        static_cast<int32_t>(std::floor(pos.y / m_cellSize)),
    };
}

} // namespace ecs
