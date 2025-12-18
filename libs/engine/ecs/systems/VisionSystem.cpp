#include "VisionSystem.h"

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Knowledge.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/placement/PlacementExecutor.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/TileAdjacency.h"

#include <utils/Log.h>

#include <cmath>

namespace ecs {

	namespace {
		// Synthetic definition name for shore tiles (land adjacent to water)
		// Shore tiles are where colonists stand to drink from water
		constexpr const char* kShoreTileDefName = "Terrain_Shore";
	} // namespace

	void VisionSystem::ensureTerrainDefinitionsRegistered() {
		if (m_terrainDefsRegistered) {
			return;
		}

		auto& registry = engine::assets::AssetRegistry::Get();

		// Create capability mask for shore (Drinkable - colonists drink AT the shore)
		m_shoreTileCapabilityMask = (1 << static_cast<uint8_t>(engine::assets::CapabilityType::Drinkable));

		// Register synthetic shore tile definition
		m_shoreTileDefNameId = registry.registerSyntheticDefinition(kShoreTileDefName, m_shoreTileCapabilityMask);

		m_terrainDefsRegistered = true;
	}

	void VisionSystem::update(float /*deltaTime*/) {
		if (m_placementExecutor == nullptr || m_processedChunks == nullptr) {
			return;
		}

		// Register terrain definitions on first update
		ensureTerrainDefinitionsRegistered();

		// Chunk size in world units (meters, since kTileSize = 1.0F)
		constexpr float kChunkWorldSize = static_cast<float>(engine::world::kChunkSize);

		auto& registry = engine::assets::AssetRegistry::Get();

		// Iterate all entities with Position and Memory components
		for (auto [entity, pos, memory] : world->view<Position, Memory>()) {
			// Get optional Knowledge component for permanent discovery tracking
			auto* knowledge = world->getComponent<Knowledge>(entity);

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

			float sightRadiusSq = memory.sightRadius * memory.sightRadius;

			// Query each potentially visible chunk for placed entities
			for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
				for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
					engine::world::ChunkCoordinate coord{cx, cy};

					// Only query processed chunks for placed entities
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

						// Update permanent knowledge if Knowledge component exists
						if (knowledge != nullptr) {
							uint32_t defNameId = registry.getDefNameId(placedEntity->defName);
							if (defNameId != 0) {
								knowledge->learn(defNameId);
							}
						}
					}
				}
			}

			// Scan for ECS entities with Appearance (e.g., bio piles created by ActionSystem)
			// These are runtime-spawned entities, as opposed to those placed during world generation
			for (auto [otherEntity, otherPos, appearance] : world->view<Position, Appearance>()) {
				// Don't "see" yourself
				if (otherEntity == entity) {
					continue;
				}

				// Check if within sight radius
				float dx = otherPos.value.x - pos.value.x;
				float dy = otherPos.value.y - pos.value.y;
				if (dx * dx + dy * dy <= sightRadiusSq) {
					// Get defNameId and capability mask from registry
					uint32_t defNameId = registry.getDefNameId(appearance.defName);
					if (defNameId != 0) {
						uint8_t capabilityMask = registry.getCapabilityMask(defNameId);
						memory.rememberWorldEntity(otherPos.value, defNameId, capabilityMask);

						// Update permanent knowledge if Knowledge component exists
						if (knowledge != nullptr) {
							knowledge->learn(defNameId);
						}
					}
				}
			}

			// Scan for shore tiles using pre-computed adjacency data
			// Shore = land tile with water in any cardinal direction (N/E/S/W)
			// Much simpler than the old approach of scanning water tiles and checking neighbors
			if (m_chunkManager != nullptr && m_shoreTileDefNameId != 0) {
				constexpr uint8_t kWaterSurfaceId = static_cast<uint8_t>(engine::world::Surface::Water);

				for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
					for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
						engine::world::ChunkCoordinate coord{cx, cy};

						const auto* chunk = m_chunkManager->getChunk(coord);
						if (chunk == nullptr || !chunk->isReady()) {
							continue;
						}

						auto origin = chunk->worldOrigin();

						// Calculate visible tile range within this chunk
						int tileMinX = std::max(0, static_cast<int>(std::floor(minX - origin.x)));
						int tileMaxX =
							std::min(static_cast<int>(engine::world::kChunkSize) - 1, static_cast<int>(std::floor(maxX - origin.x)));
						int tileMinY = std::max(0, static_cast<int>(std::floor(minY - origin.y)));
						int tileMaxY =
							std::min(static_cast<int>(engine::world::kChunkSize) - 1, static_cast<int>(std::floor(maxY - origin.y)));

						// Scan tiles - check adjacency data for water neighbors
						for (int ty = tileMinY; ty <= tileMaxY; ++ty) {
							for (int tx = tileMinX; tx <= tileMaxX; ++tx) {
								auto tile = chunk->getTile(static_cast<uint16_t>(tx), static_cast<uint16_t>(ty));

								// Skip water tiles - we want land tiles adjacent to water
								if (tile.surface == engine::world::Surface::Water) {
									continue;
								}

								// Check if this land tile has water in any cardinal direction
								if (engine::world::TileAdjacency::hasAdjacentSurface(tile.adjacency, kWaterSurfaceId)) {
									// This is a shore tile!
									glm::vec2 shoreWorldPos{
										origin.x + static_cast<float>(tx) + 0.5F, origin.y + static_cast<float>(ty) + 0.5F
									};

									// Check if within sight radius
									float dx = shoreWorldPos.x - pos.value.x;
									float dy = shoreWorldPos.y - pos.value.y;
									if (dx * dx + dy * dy <= sightRadiusSq) {
										memory.rememberWorldEntity(shoreWorldPos, m_shoreTileDefNameId, m_shoreTileCapabilityMask);

										// Update permanent knowledge for shore tiles
										if (knowledge != nullptr) {
											knowledge->learn(m_shoreTileDefNameId);
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}

} // namespace ecs
