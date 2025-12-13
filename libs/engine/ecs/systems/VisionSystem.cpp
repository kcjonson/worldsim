#include "VisionSystem.h"

#include "../World.h"
#include "../components/Memory.h"
#include "../components/Transform.h"

#include "assets/AssetDefinition.h"
#include "assets/AssetRegistry.h"
#include "assets/placement/PlacementExecutor.h"
#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"

#include <utils/Log.h>

#include <cmath>

namespace ecs {

	namespace {
		// Synthetic definition name for water tiles (not a placed entity, but a terrain feature)
		constexpr const char* kWaterTileDefName = "Terrain_Water";

		// Scan every tile for water edges (stride=1 needed for accurate shore detection)
		// With stride>1, we might skip edge tiles and land on interior tiles whose
		// neighbors are all water, causing us to miss registering shore positions.
		constexpr int kWaterScanStride = 1;
	} // namespace

	void VisionSystem::ensureTerrainDefinitionsRegistered() {
		if (m_terrainDefsRegistered) {
			return;
		}

		auto& registry = engine::assets::AssetRegistry::Get();

		// Create capability mask for water (Drinkable)
		m_waterTileCapabilityMask = (1 << static_cast<uint8_t>(engine::assets::CapabilityType::Drinkable));

		// Register synthetic water tile definition
		m_waterTileDefNameId = registry.registerSyntheticDefinition(kWaterTileDefName, m_waterTileCapabilityMask);

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
					}
				}
			}

			// Scan for water tiles in ALL visible chunks (not just processed ones)
			// Water is a terrain feature, not a placed entity
			if (m_chunkManager != nullptr && m_waterTileDefNameId != 0) {
				for (int32_t cy = chunkMinY; cy <= chunkMaxY; ++cy) {
					for (int32_t cx = chunkMinX; cx <= chunkMaxX; ++cx) {
						engine::world::ChunkCoordinate coord{cx, cy};

						const auto* chunk = m_chunkManager->getChunk(coord);
						if (chunk == nullptr || !chunk->isReady()) {
							continue;
						}

						// Calculate the tile range to scan within this chunk
						auto origin = chunk->worldOrigin();

						// Calculate visible tile range within this chunk
						int tileMinX = std::max(0, static_cast<int>(std::floor(minX - origin.x)));
						int tileMaxX =
							std::min(static_cast<int>(engine::world::kChunkSize) - 1, static_cast<int>(std::floor(maxX - origin.x)));
						int tileMinY = std::max(0, static_cast<int>(std::floor(minY - origin.y)));
						int tileMaxY =
							std::min(static_cast<int>(engine::world::kChunkSize) - 1, static_cast<int>(std::floor(maxY - origin.y)));

						// Scan tiles with stride for performance (water bodies are large)
						// We look for water tiles at the EDGE (adjacent to non-water) and register
						// the shore position (the adjacent land tile) so colonists drink from shore
						for (int ty = tileMinY; ty <= tileMaxY; ty += kWaterScanStride) {
							for (int tx = tileMinX; tx <= tileMaxX; tx += kWaterScanStride) {
								auto tile = chunk->getTile(static_cast<uint16_t>(tx), static_cast<uint16_t>(ty));

								if (tile.surface == engine::world::Surface::Water) {
									// Calculate the world position of this water tile
									glm::vec2 waterWorldPos{
										origin.x + static_cast<float>(tx) + 0.5F, origin.y + static_cast<float>(ty) + 0.5F
									};

									// Find any adjacent non-water tile as the shore position
									// (where colonist stands to drink from this water tile)
									// Uses world coordinates to handle cross-chunk boundaries
									static const int kNeighborOffsets[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};

									for (const auto& offset : kNeighborOffsets) {
										// Calculate neighbor world position
										float neighborWorldX = waterWorldPos.x - 0.5F + static_cast<float>(offset[0]);
										float neighborWorldY = waterWorldPos.y - 0.5F + static_cast<float>(offset[1]);

										// Find which chunk this neighbor is in
										engine::world::ChunkCoordinate neighborChunkCoord{
											static_cast<int32_t>(
												std::floor(neighborWorldX / static_cast<float>(engine::world::kChunkSize))
											),
											static_cast<int32_t>(std::floor(neighborWorldY / static_cast<float>(engine::world::kChunkSize)))
										};

										const auto* neighborChunk = m_chunkManager->getChunk(neighborChunkCoord);
										if (neighborChunk == nullptr || !neighborChunk->isReady()) {
											continue; // Chunk not loaded or not ready
										}

										// Calculate local tile coordinates within that chunk
										auto neighborOrigin = neighborChunk->worldOrigin();
										int	 neighborTileX = static_cast<int>(std::floor(neighborWorldX - neighborOrigin.x));
										int	 neighborTileY = static_cast<int>(std::floor(neighborWorldY - neighborOrigin.y));

										// Clamp to valid range (should already be valid, but be safe)
										neighborTileX =
											std::max(0, std::min(neighborTileX, static_cast<int>(engine::world::kChunkSize) - 1));
										neighborTileY =
											std::max(0, std::min(neighborTileY, static_cast<int>(engine::world::kChunkSize) - 1));

										auto neighborTile = neighborChunk->getTile(
											static_cast<uint16_t>(neighborTileX), static_cast<uint16_t>(neighborTileY)
										);

										if (neighborTile.surface == engine::world::Surface::Water) {
											continue;
										}

										// Found a non-water neighbor - this is a shore!
										glm::vec2 shoreWorldPos{neighborWorldX + 0.5F, neighborWorldY + 0.5F};

										// Check if within sight radius
										float dx = shoreWorldPos.x - pos.value.x;
										float dy = shoreWorldPos.y - pos.value.y;
										if (dx * dx + dy * dy <= sightRadiusSq) {
											memory.rememberWorldEntity(shoreWorldPos, m_waterTileDefNameId, m_waterTileCapabilityMask);
										}
										break; // Only register one shore per water tile
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
