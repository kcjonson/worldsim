#pragma once

// The grid of sub-regions a chunk's short statics are grouped by: what the baked mesh is culled
// and re-baked by, and what PlacementExecutor counts removals by.

#include "world/chunk/ChunkCoordinate.h"

#include <glm/vec2.hpp>

#include <algorithm>

namespace assets {

/// Sub-chunk grid: 8x8 = 64 sub-regions per chunk, 64x64 tiles each.
/// Sub-regions are the granularity of view-frustum culling for baked entities.
inline constexpr int kSubChunkGridSize = 8;
inline constexpr int kSubChunkTileSize = world::kChunkSize / kSubChunkGridSize;
inline constexpr float kSubChunkWorldSize = static_cast<float>(kSubChunkTileSize) * world::kTileSize;
inline constexpr int kSubChunkCount = kSubChunkGridSize * kSubChunkGridSize;

/// The sub-region of chunk `coord` a static at `position` belongs to.
[[nodiscard]] inline int subChunkIndexOf(world::ChunkCoordinate coord, glm::vec2 position) {
	const world::WorldPosition origin = coord.origin();
	const int subX = std::clamp(static_cast<int>((position.x - origin.x) / kSubChunkWorldSize), 0, kSubChunkGridSize - 1);
	const int subY = std::clamp(static_cast<int>((position.y - origin.y) / kSubChunkWorldSize), 0, kSubChunkGridSize - 1);
	return subY * kSubChunkGridSize + subX;
}

} // namespace assets
