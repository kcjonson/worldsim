#include "VisionSystem.h"

#include "../World.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include "assets/placement/PlacementExecutor.h"

#include <cmath>

namespace ecs {

void VisionSystem::update(float /*deltaTime*/) {
	if (m_placementExecutor == nullptr || m_processedChunks == nullptr) {
		return;
	}

	// Chunk size in world units (meters, since kTileSize = 1.0F)
	constexpr float kChunkWorldSize = static_cast<float>(engine::world::kChunkSize);

	// Iterate all entities with Position and Memory components
	for (auto [entity, pos, memory] : world->view<Position, Memory>()) {
		// Calculate bounding box of vision in world coordinates
		float minX = pos.value.x - memory.sightRadius;
		float maxX = pos.value.x + memory.sightRadius;
		float minY = pos.value.y - memory.sightRadius;
		float maxY = pos.value.y + memory.sightRadius;

		// Convert to chunk coordinate range
		int32_t chunkMinX = static_cast<int32_t>(std::floor(minX / kChunkWorldSize));
		int32_t chunkMaxX = static_cast<int32_t>(std::floor(maxX / kChunkWorldSize));
		int32_t chunkMinY = static_cast<int32_t>(std::floor(minY / kChunkWorldSize));
		int32_t chunkMaxY = static_cast<int32_t>(std::floor(maxY / kChunkWorldSize));

		// Query each potentially visible chunk
		for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
			for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
				engine::world::ChunkCoordinate coord{cx, cy};

				// Only query processed chunks
				if (m_processedChunks->find(coord) == m_processedChunks->end()) {
					continue;
				}

				const auto* chunkIndex = m_placementExecutor->getChunkIndex(coord);
				if (chunkIndex == nullptr) {
					continue;
				}

				// Query entities within sight radius from this chunk
				auto nearbyEntities = chunkIndex->queryRadius(pos.value, memory.sightRadius);

				// Remember each discovered entity
				for (const auto* placedEntity : nearbyEntities) {
					memory.rememberWorldEntity(placedEntity->position, placedEntity->defName);
				}
			}
		}
	}
}

} // namespace ecs
