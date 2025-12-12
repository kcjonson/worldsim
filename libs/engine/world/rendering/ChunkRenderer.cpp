#include "ChunkRenderer.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>

namespace engine::world {

	// Maximum vertices per batch to stay within uint16_t index range
	// Leave headroom for a full chunk (64x64 tiles × 4 vertices = 16,384 vertices)
	constexpr size_t kMaxVerticesPerBatch = 60000;

	ChunkRenderer::ChunkRenderer(float pixelsPerMeter)
		: m_pixelsPerMeter(pixelsPerMeter) {}

	void ChunkRenderer::flushBatch() {
		if (m_indices.empty()) {
			return;
		}

		Renderer::Primitives::drawTriangles(
			Renderer::Primitives::TrianglesArgs{
				.vertices = m_vertices.data(),
				.indices = m_indices.data(),
				.vertexCount = m_vertices.size(),
				.indexCount = m_indices.size(),
				.colors = m_colors.data()
			}
		);

		// Clear for next batch (keep capacity)
		m_vertices.clear();
		m_colors.clear();
		m_indices.clear();
	}

	void ChunkRenderer::render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight) {
		m_lastTileCount = 0;

		// Clear per-frame buffers (but keep capacity for reuse)
		m_vertices.clear();
		m_colors.clear();
		m_indices.clear();

		// Reserve capacity to avoid reallocations
		// ~15k tiles × 4 vertices each = ~60k vertices typical
		constexpr size_t kExpectedVertices = 60000;
		constexpr size_t kExpectedIndices = 90000;
		if (m_vertices.capacity() < kExpectedVertices) {
			m_vertices.reserve(kExpectedVertices);
			m_colors.reserve(kExpectedVertices);
			m_indices.reserve(kExpectedIndices);
		}

		// Get visible world rectangle for tile culling
		Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, m_pixelsPerMeter);

		// Get visible chunks
		auto [minCorner, maxCorner] = camera.getVisibleCorners(viewportWidth, viewportHeight, m_pixelsPerMeter);
		std::vector<const Chunk*> visibleChunks = chunkManager.getVisibleChunks(minCorner, maxCorner);

		// Accumulate geometry for all visible tiles
		for (const Chunk* chunk : visibleChunks) {
			if (chunk->isPure()) {
				addPureChunk(*chunk, camera, viewportWidth, viewportHeight);
			} else {
				addChunkTiles(*chunk, camera, visibleRect, viewportWidth, viewportHeight);
			}
		}

		// Final flush for remaining geometry
		flushBatch();
	}

	void ChunkRenderer::addChunkTiles(
		const Chunk&			chunk,
		const WorldCamera&		camera,
		const Foundation::Rect& visibleRect,
		int						viewportWidth,
		int						viewportHeight
	) {
		WorldPosition chunkOrigin = chunk.worldOrigin();

		// Calculate chunk bounds in world space
		float chunkMinX = chunkOrigin.x;
		float chunkMaxX = chunkOrigin.x + static_cast<float>(kChunkSize) * kTileSize;
		float chunkMinY = chunkOrigin.y;
		float chunkMaxY = chunkOrigin.y + static_cast<float>(kChunkSize) * kTileSize;

		// Clamp to visible rect (only render tiles actually on screen)
		float visMinX = std::max(chunkMinX, visibleRect.x);
		float visMaxX = std::min(chunkMaxX, visibleRect.x + visibleRect.width);
		float visMinY = std::max(chunkMinY, visibleRect.y);
		float visMaxY = std::min(chunkMaxY, visibleRect.y + visibleRect.height);

		if (visMinX >= visMaxX || visMinY >= visMaxY) {
			return; // Chunk not visible
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

		// Pre-compute transform constants
		float halfViewW = static_cast<float>(viewportWidth) * 0.5F;
		float halfViewH = static_cast<float>(viewportHeight) * 0.5F;
		float scale = m_pixelsPerMeter * camera.zoom();
		float camX = camera.position().x;
		float camY = camera.position().y;
		float tileScreenSize = kTileSize * scale * static_cast<float>(m_tileResolution);

		// Add geometry for visible tiles
		for (int32_t tileY = startTileY; tileY < endTileY; tileY += m_tileResolution) {
			for (int32_t tileX = startTileX; tileX < endTileX; tileX += m_tileResolution) {
				// Flush batch if approaching uint16_t index limit (each tile adds 4 vertices)
				if (m_vertices.size() + 4 > kMaxVerticesPerBatch) {
					flushBatch();
				}

				// Get tile data and color
				TileData		  tile = chunk.getTile(static_cast<uint16_t>(tileX), static_cast<uint16_t>(tileY));
				Foundation::Color color = Chunk::getSurfaceColor(tile.surface);

				// Calculate world position of tile
				float worldX = chunkMinX + static_cast<float>(tileX) * kTileSize;
				float worldY = chunkMinY + static_cast<float>(tileY) * kTileSize;

				// Transform to screen coordinates
				float screenX = (worldX - camX) * scale + halfViewW;
				float screenY = (worldY - camY) * scale + halfViewH;

				// Current vertex index for indices (safe after flush check)
				auto baseVertex = static_cast<uint16_t>(m_vertices.size());

				// Add 4 vertices (quad corners) - already in screen space
				m_vertices.emplace_back(screenX, screenY);									 // Top-left
				m_vertices.emplace_back(screenX + tileScreenSize, screenY);					 // Top-right
				m_vertices.emplace_back(screenX + tileScreenSize, screenY + tileScreenSize); // Bottom-right
				m_vertices.emplace_back(screenX, screenY + tileScreenSize);					 // Bottom-left

				// Add colors (same for all 4 corners)
				m_colors.emplace_back(color);
				m_colors.emplace_back(color);
				m_colors.emplace_back(color);
				m_colors.emplace_back(color);

				// Add indices for 2 triangles
				m_indices.push_back(baseVertex);
				m_indices.push_back(baseVertex + 1);
				m_indices.push_back(baseVertex + 2);
				m_indices.push_back(baseVertex);
				m_indices.push_back(baseVertex + 2);
				m_indices.push_back(baseVertex + 3);

				m_lastTileCount++;
			}
		}
	}

	void ChunkRenderer::addPureChunk(const Chunk& chunk, const WorldCamera& camera, int viewportWidth, int viewportHeight) {
		// Flush batch if approaching uint16_t index limit (each chunk adds 4 vertices)
		if (m_vertices.size() + 4 > kMaxVerticesPerBatch) {
			flushBatch();
		}

		WorldPosition	  chunkOrigin = chunk.worldOrigin();
		Foundation::Color color = Chunk::getBiomeColor(chunk.primaryBiome());

		float halfViewW = static_cast<float>(viewportWidth) * 0.5F;
		float halfViewH = static_cast<float>(viewportHeight) * 0.5F;
		float scale = m_pixelsPerMeter * camera.zoom();
		float camX = camera.position().x;
		float camY = camera.position().y;

		// Calculate screen position
		float screenX = (chunkOrigin.x - camX) * scale + halfViewW;
		float screenY = (chunkOrigin.y - camY) * scale + halfViewH;
		float chunkScreenSize = static_cast<float>(kChunkSize) * kTileSize * scale;

		// Current vertex index for indices (safe after flush check)
		auto baseVertex = static_cast<uint16_t>(m_vertices.size());

		// Add 4 vertices for chunk quad
		m_vertices.emplace_back(screenX, screenY);									   // Top-left
		m_vertices.emplace_back(screenX + chunkScreenSize, screenY);				   // Top-right
		m_vertices.emplace_back(screenX + chunkScreenSize, screenY + chunkScreenSize); // Bottom-right
		m_vertices.emplace_back(screenX, screenY + chunkScreenSize);				   // Bottom-left

		// Add colors
		m_colors.emplace_back(color);
		m_colors.emplace_back(color);
		m_colors.emplace_back(color);
		m_colors.emplace_back(color);

		// Add indices for 2 triangles
		m_indices.push_back(baseVertex);
		m_indices.push_back(baseVertex + 1);
		m_indices.push_back(baseVertex + 2);
		m_indices.push_back(baseVertex);
		m_indices.push_back(baseVertex + 2);
		m_indices.push_back(baseVertex + 3);

		m_lastTileCount++; // Count as 1 tile (the whole chunk)
	}

} // namespace engine::world
