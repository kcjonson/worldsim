#include "ChunkRenderer.h"

#include <primitives/Primitives.h>

namespace engine::world {

ChunkRenderer::ChunkRenderer(float pixelsPerMeter) : m_pixelsPerMeter(pixelsPerMeter) {}

void ChunkRenderer::render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth,
						   int viewportHeight) {
	m_lastTileCount = 0;  // Reset tile count for this frame

	// Get visible world rectangle
	Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, m_pixelsPerMeter);

	// Get visible chunks
	auto [minCorner, maxCorner] = camera.getVisibleCorners(viewportWidth, viewportHeight, m_pixelsPerMeter);
	std::vector<const Chunk*> visibleChunks = chunkManager.getVisibleChunks(minCorner, maxCorner);

	// Render each visible chunk
	for (const Chunk* chunk : visibleChunks) {
		renderChunk(*chunk, camera, visibleRect, viewportWidth, viewportHeight);
	}
}

void ChunkRenderer::renderChunk(const Chunk& chunk, const WorldCamera& camera, const Foundation::Rect& visibleRect,
								int viewportWidth, int viewportHeight) {
	WorldPosition chunkOrigin = chunk.worldOrigin();
	// Tile size must include zoom factor to match position calculations
	float tilePixelSize = m_pixelsPerMeter * kTileSize * static_cast<float>(m_tileResolution) * camera.zoom();

	// Calculate which tiles in this chunk are visible
	float chunkMinX = chunkOrigin.x;
	float chunkMaxX = chunkOrigin.x + static_cast<float>(kChunkSize) * kTileSize;
	float chunkMinY = chunkOrigin.y;
	float chunkMaxY = chunkOrigin.y + static_cast<float>(kChunkSize) * kTileSize;

	// Clamp to visible rect
	float visMinX = std::max(chunkMinX, visibleRect.x);
	float visMaxX = std::min(chunkMaxX, visibleRect.x + visibleRect.width);
	float visMinY = std::max(chunkMinY, visibleRect.y);
	float visMaxY = std::min(chunkMaxY, visibleRect.y + visibleRect.height);

	if (visMinX >= visMaxX || visMinY >= visMaxY) {
		return;	 // Chunk not visible
	}

	// Convert to local tile coordinates
	int32_t startTileX = static_cast<int32_t>((visMinX - chunkMinX) / kTileSize);
	int32_t endTileX = static_cast<int32_t>((visMaxX - chunkMinX) / kTileSize) + 1;
	int32_t startTileY = static_cast<int32_t>((visMinY - chunkMinY) / kTileSize);
	int32_t endTileY = static_cast<int32_t>((visMaxY - chunkMinY) / kTileSize) + 1;

	// Clamp to chunk bounds
	startTileX = std::max(0, std::min(startTileX, kChunkSize - 1));
	endTileX = std::max(0, std::min(endTileX, kChunkSize));
	startTileY = std::max(0, std::min(startTileY, kChunkSize - 1));
	endTileY = std::max(0, std::min(endTileY, kChunkSize));

	float halfViewW = static_cast<float>(viewportWidth) * 0.5F;
	float halfViewH = static_cast<float>(viewportHeight) * 0.5F;

	// For pure chunks, render as a single colored rectangle
	if (chunk.isPure() && m_tileResolution > 4) {
		Foundation::Color color = Chunk::getBiomeColor(chunk.primaryBiome());

		// Calculate screen position
		float screenX = (chunkMinX - camera.position().x) * m_pixelsPerMeter * camera.zoom() + halfViewW;
		float screenY = (chunkMinY - camera.position().y) * m_pixelsPerMeter * camera.zoom() + halfViewH;
		float chunkScreenSize = static_cast<float>(kChunkSize) * kTileSize * m_pixelsPerMeter * camera.zoom();

		Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
			.bounds = Foundation::Rect{screenX, screenY, chunkScreenSize, chunkScreenSize},
			.style = Foundation::RectStyle{.fill = color}
		});
		m_lastTileCount++;  // Count as 1 tile (the whole chunk)
		return;
	}

	// Render individual tiles (with resolution stepping)
	for (int32_t tileY = startTileY; tileY < endTileY; tileY += m_tileResolution) {
		for (int32_t tileX = startTileX; tileX < endTileX; tileX += m_tileResolution) {
			// Get tile data
			TileData tile = chunk.getTile(static_cast<uint16_t>(tileX), static_cast<uint16_t>(tileY));

			// Get color from ground cover
			Foundation::Color color = Chunk::getGroundCoverColor(tile.groundCover);

			// Calculate world position of tile
			float worldX = chunkMinX + static_cast<float>(tileX) * kTileSize;
			float worldY = chunkMinY + static_cast<float>(tileY) * kTileSize;

			// Convert to screen position (relative to camera, centered on screen)
			float screenX = (worldX - camera.position().x) * m_pixelsPerMeter * camera.zoom() + halfViewW;
			float screenY = (worldY - camera.position().y) * m_pixelsPerMeter * camera.zoom() + halfViewH;

			Renderer::Primitives::drawRect(Renderer::Primitives::RectArgs{
				.bounds = Foundation::Rect{screenX, screenY, tilePixelSize, tilePixelSize},
				.style = Foundation::RectStyle{.fill = color}
			});
			m_lastTileCount++;
		}
	}
}

}  // namespace engine::world
