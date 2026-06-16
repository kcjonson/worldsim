#pragma once

#include "../EntityID.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include <glm/vec2.hpp>

namespace ecs {

// Dynamic spatial hash for moving agents. Keyed by packed (cellX, cellY) int64
// (same packing as engine::assets::SpatialIndex). Rebuilt each frame: clear()
// drops every cell, then every agent is re-inserted at its new position, so the
// map only ever holds cells occupied this frame (it cannot accumulate empty
// cells as agents roam the world).
class AgentSpatialHash {
  public:
    explicit AgentSpatialHash(float cellSize = 1.0f);

    // Drop all cells (bounded: only currently-occupied cells survive a rebuild).
    void clear();

    // Insert agent at position into the hash.
    void insert(EntityID e, glm::vec2 pos);

    // Append all candidate entity ids from cells that overlap [center-radius,
    // center+radius] into out (cleared first). May include ids slightly outside
    // radius; the caller is responsible for exact distance checks.
    void queryNeighbors(glm::vec2 center, float radius, std::vector<EntityID>& out) const;

  private:
    [[nodiscard]] static int64_t cellKey(int32_t cx, int32_t cy);
    [[nodiscard]] std::pair<int32_t, int32_t> cellCoords(glm::vec2 pos) const;

    float m_cellSize;
    std::unordered_map<int64_t, std::vector<EntityID>> m_cells;
};

} // namespace ecs
