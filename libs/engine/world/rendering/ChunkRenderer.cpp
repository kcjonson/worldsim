#include "ChunkRenderer.h"

#include <primitives/Primitives.h>

#include <algorithm>
#include <vector>

namespace engine::world {

	ChunkRenderer::ChunkRenderer(float pixelsPerMeter)
		: m_pixelsPerMeter(pixelsPerMeter) {}

	void ChunkRenderer::render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight) {
		m_lastTileCount = 0;
		m_lastChunkCount = 0;

		Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, m_pixelsPerMeter);
		auto [minCorner, maxCorner] = camera.getVisibleCorners(viewportWidth, viewportHeight, m_pixelsPerMeter);
		std::vector<const Chunk*> visibleChunks = chunkManager.getVisibleChunks(minCorner, maxCorner);

		for (const Chunk* chunk : visibleChunks) {
			if (!chunk->isReady()) {
				continue;
			}
			addChunkTiles(*chunk, camera, visibleRect, viewportWidth, viewportHeight);
			m_lastChunkCount++;
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
				const TileRenderData& render = chunk.getTileRenderData(static_cast<uint16_t>(tileX), static_cast<uint16_t>(tileY));

				float worldX = chunkMinX + static_cast<float>(tileX) * kTileSize;
				float worldY = chunkMinY + static_cast<float>(tileY) * kTileSize;

				float screenX = (worldX - camX) * scale + halfViewW;
				float screenY = (worldY - camY) * scale + halfViewH;

				int32_t worldTileX = chunkCoord.x * kChunkSize + tileX;
				int32_t worldTileY = chunkCoord.y * kChunkSize + tileY;

				Renderer::Primitives::drawTile(
					{.bounds = Foundation::Rect{screenX, screenY, tileScreenSize, tileScreenSize},
					 .color = Foundation::Color::white(),
					 .edgeMask = render.edgeMask,
					 .cornerMask = render.cornerMask,
					 .surfaceId = render.surfaceId,
					 .hardEdgeMask = render.hardEdgeMask,
					 .tileX = worldTileX,
					 .tileY = worldTileY,
					 .neighborN = render.neighborN,
					 .neighborE = render.neighborE,
					 .neighborS = render.neighborS,
					 .neighborW = render.neighborW,
					 .neighborNW = render.neighborNW,
					 .neighborNE = render.neighborNE,
					 .neighborSE = render.neighborSE,
					 .neighborSW = render.neighborSW}
				);

				m_lastTileCount++;
			}
		}
	}

} // namespace engine::world
