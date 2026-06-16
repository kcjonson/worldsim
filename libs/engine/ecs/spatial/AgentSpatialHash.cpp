#include "AgentSpatialHash.h"

#include <cmath>

namespace ecs {

AgentSpatialHash::AgentSpatialHash(float cellSize)
    : m_cellSize(cellSize) {}

void AgentSpatialHash::clear() {
    for (auto& [key, vec] : m_cells) {
        vec.clear(); // keep capacity
    }
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
    return (static_cast<int64_t>(cx) << 32) | static_cast<uint32_t>(cy);
}

std::pair<int32_t, int32_t> AgentSpatialHash::cellCoords(glm::vec2 pos) const {
    return {
        static_cast<int32_t>(std::floor(pos.x / m_cellSize)),
        static_cast<int32_t>(std::floor(pos.y / m_cellSize)),
    };
}

} // namespace ecs
