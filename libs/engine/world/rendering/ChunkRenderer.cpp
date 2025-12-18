#include "ChunkRenderer.h"

#include "world/chunk/TileAdjacency.h"

#include <algorithm>
#include <primitives/Primitives.h>
#include <vector>

namespace engine::world {

	ChunkRenderer::ChunkRenderer(float pixelsPerMeter)
		: m_pixelsPerMeter(pixelsPerMeter) {}

	void ChunkRenderer::render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight) {
		m_lastTileCount = 0;

		Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, m_pixelsPerMeter);

		auto [minCorner, maxCorner] = camera.getVisibleCorners(viewportWidth, viewportHeight, m_pixelsPerMeter);
		std::vector<const Chunk*> visibleChunks = chunkManager.getVisibleChunks(minCorner, maxCorner);

		for (const Chunk* chunk : visibleChunks) {
			if (!chunk->isReady()) {
				continue;
			}
			addChunkTiles(*chunk, camera, visibleRect, viewportWidth, viewportHeight);
		}
	}

	void ChunkRenderer::addChunkTiles(
		const Chunk&			chunk,
		const WorldCamera&		camera,
		const Foundation::Rect& visibleRect,
		int						viewportWidth,
		int						viewportHeight
	) {
		WorldPosition	chunkOrigin = chunk.worldOrigin();
		ChunkCoordinate chunkCoord = chunk.coordinate();

		float chunkMinX = chunkOrigin.x;
		float chunkMaxX = chunkOrigin.x + static_cast<float>(kChunkSize) * kTileSize;
		float chunkMinY = chunkOrigin.y;
		float chunkMaxY = chunkOrigin.y + static_cast<float>(kChunkSize) * kTileSize;

		float visMinX = std::max(chunkMinX, visibleRect.x);
		float visMaxX = std::min(chunkMaxX, visibleRect.x + visibleRect.width);
		float visMinY = std::max(chunkMinY, visibleRect.y);
		float visMaxY = std::min(chunkMaxY, visibleRect.y + visibleRect.height);

		if (visMinX >= visMaxX || visMinY >= visMaxY) {
			return;
		}

		int32_t startTileX = static_cast<int32_t>((visMinX - chunkMinX) / kTileSize);
		int32_t endTileX = static_cast<int32_t>((visMaxX - chunkMinX) / kTileSize) + 1;
		int32_t startTileY = static_cast<int32_t>((visMinY - chunkMinY) / kTileSize);
		int32_t endTileY = static_cast<int32_t>((visMaxY - chunkMinY) / kTileSize) + 1;

		startTileX = std::max(0, std::min(startTileX, kChunkSize - 1));
		endTileX = std::max(0, std::min(endTileX, kChunkSize));
		startTileY = std::max(0, std::min(startTileY, kChunkSize - 1));
		endTileY = std::max(0, std::min(endTileY, kChunkSize));

		float halfViewW = static_cast<float>(viewportWidth) * 0.5F;
		float halfViewH = static_cast<float>(viewportHeight) * 0.5F;
		float scale = m_pixelsPerMeter * camera.zoom();
		float camX = camera.position().x;
		float camY = camera.position().y;
		float tileScreenSize = kTileSize * scale * static_cast<float>(m_tileResolution);

		for (int32_t tileY = startTileY; tileY < endTileY; tileY += m_tileResolution) {
			for (int32_t tileX = startTileX; tileX < endTileX; tileX += m_tileResolution) {
				TileData tile = chunk.getTile(static_cast<uint16_t>(tileX), static_cast<uint16_t>(tileY));
				// Tile textures carry their own coloration; use neutral tint to avoid double-darkening.
				Foundation::Color color = Foundation::Color::white();
				uint8_t			  surfaceId = static_cast<uint8_t>(tile.surface);

				float worldX = chunkMinX + static_cast<float>(tileX) * kTileSize;
				float worldY = chunkMinY + static_cast<float>(tileY) * kTileSize;

				float screenX = (worldX - camX) * scale + halfViewW;
				float screenY = (worldY - camY) * scale + halfViewH;

				// World tile coordinates for procedural edge variation
				int32_t worldTileX = chunkCoord.x * kChunkSize + tileX;
				int32_t worldTileY = chunkCoord.y * kChunkSize + tileY;

				// Extract cardinal neighbor surface IDs for soft edge blending
				uint8_t neighborN = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::N);
				uint8_t neighborE = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::E);
				uint8_t neighborS = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::S);
				uint8_t neighborW = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::W);

				// Extract diagonal neighbor surface IDs for corner blending
				uint8_t neighborNW = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::NW);
				uint8_t neighborNE = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::NE);
				uint8_t neighborSE = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::SE);
				uint8_t neighborSW = TileAdjacency::getNeighbor(tile.adjacency, TileAdjacency::SW);

				Renderer::Primitives::drawTile(
					{.bounds = Foundation::Rect{screenX, screenY, tileScreenSize, tileScreenSize},
					 .color = color,
					 .edgeMask = TileAdjacency::getEdgeMaskByStack(tile.adjacency, surfaceId),
					 .cornerMask = TileAdjacency::getCornerMaskByStack(tile.adjacency, surfaceId),
					 .surfaceId = surfaceId,
					 .hardEdgeMask = TileAdjacency::getHardEdgeMaskByFamily(tile.adjacency, surfaceId),
					 .tileX = worldTileX,
					 .tileY = worldTileY,
					 .neighborN = neighborN,
					 .neighborE = neighborE,
					 .neighborS = neighborS,
					 .neighborW = neighborW,
					 .neighborNW = neighborNW,
					 .neighborNE = neighborNE,
					 .neighborSE = neighborSE,
					 .neighborSW = neighborSW}
				);

				m_lastTileCount++;
			}
		}
	}

} // namespace engine::world
