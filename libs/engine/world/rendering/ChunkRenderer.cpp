#include "ChunkRenderer.h"

#include "world/chunk/TileAdjacency.h"

#include <primitives/Primitives.h>
#include <utils/Log.h>

namespace engine::world {

	// Maximum vertices per batch to stay within uint16_t index range
	// Leave headroom for a full chunk (64x64 tiles × 4 vertices = 16,384 vertices)
	constexpr size_t kMaxVerticesPerBatch = 60000;

	// Edge stroke widths in pixels (at 1:1 zoom)
	// Thinner on top/left (light side), thicker on bottom/right (shadow side)
	constexpr float kEdgeStrokeLight = 1.0F;   // North and West edges
	constexpr float kEdgeStrokeShadow = 2.0F;  // South and East edges

	// Darken factor for edge strokes (multiply RGB by this value)
	constexpr float kEdgeStrokeDarken = 0.65F;

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
			// Skip chunks that haven't finished generating
			if (!chunk->isReady()) {
				continue;
			}
			// Render all tiles - no pure chunk shortcut since surfaces vary within biomes
			// (e.g., grassland has water ponds, soil vs dirt patches)
			addChunkTiles(*chunk, camera, visibleRect, viewportWidth, viewportHeight);
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
				// Flush batch if approaching uint16_t index limit
				// Each tile adds 4 vertices, plus up to 32 for shore (4 edges + 4 corners × 4 vertices each)
				if (m_vertices.size() + 36 > kMaxVerticesPerBatch) {
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

				// ========== Surface Edge Strokes ==========
				// Draw edges where this tile is adjacent to a LOWER surface in the stack.
				// Stack order: Water < Mud < Sand < Dirt < Soil < Rock < Snow
				// The higher surface draws the edge (e.g., grass next to water draws edge)

				uint8_t thisSurface = static_cast<uint8_t>(tile.surface);

				// Get edges where neighbor is lower in stack
				uint8_t edges = TileAdjacency::getEdgeMaskByStack(tile.adjacency, thisSurface);

				// Get corners where diagonal neighbor is lower (but cardinals aren't)
				uint8_t corners = TileAdjacency::getCornerMaskByStack(tile.adjacency, thisSurface);

				if (edges != 0 || corners != 0) {
					// Darken the tile's own color for the edge stroke
					Foundation::Color strokeColor{
						color.r * kEdgeStrokeDarken,
						color.g * kEdgeStrokeDarken,
						color.b * kEdgeStrokeDarken,
						color.a
					};

					// Stroke widths - light (top/left) vs shadow (bottom/right)
					float strokeLight = kEdgeStrokeLight * camera.zoom();
					float strokeShadow = kEdgeStrokeShadow * camera.zoom();

					// Draw stroke on each edge facing a lower-stack surface
					if ((edges & TileAdjacency::EdgeBit::North) != 0) {
						// North edge: top of tile (light/thin)
						auto sv = static_cast<uint16_t>(m_vertices.size());
						m_vertices.emplace_back(screenX, screenY);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY + strokeLight);
						m_vertices.emplace_back(screenX, screenY + strokeLight);
						for (int i = 0; i < 4; ++i) m_colors.emplace_back(strokeColor);
						m_indices.push_back(sv); m_indices.push_back(sv + 1); m_indices.push_back(sv + 2);
						m_indices.push_back(sv); m_indices.push_back(sv + 2); m_indices.push_back(sv + 3);
					}

					if ((edges & TileAdjacency::EdgeBit::South) != 0) {
						// South edge: bottom of tile (shadow/thick)
						auto sv = static_cast<uint16_t>(m_vertices.size());
						m_vertices.emplace_back(screenX, screenY + tileScreenSize - strokeShadow);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY + tileScreenSize - strokeShadow);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY + tileScreenSize);
						m_vertices.emplace_back(screenX, screenY + tileScreenSize);
						for (int i = 0; i < 4; ++i) m_colors.emplace_back(strokeColor);
						m_indices.push_back(sv); m_indices.push_back(sv + 1); m_indices.push_back(sv + 2);
						m_indices.push_back(sv); m_indices.push_back(sv + 2); m_indices.push_back(sv + 3);
					}

					if ((edges & TileAdjacency::EdgeBit::West) != 0) {
						// West edge: left side of tile (light/thin)
						auto sv = static_cast<uint16_t>(m_vertices.size());
						m_vertices.emplace_back(screenX, screenY);
						m_vertices.emplace_back(screenX + strokeLight, screenY);
						m_vertices.emplace_back(screenX + strokeLight, screenY + tileScreenSize);
						m_vertices.emplace_back(screenX, screenY + tileScreenSize);
						for (int i = 0; i < 4; ++i) m_colors.emplace_back(strokeColor);
						m_indices.push_back(sv); m_indices.push_back(sv + 1); m_indices.push_back(sv + 2);
						m_indices.push_back(sv); m_indices.push_back(sv + 2); m_indices.push_back(sv + 3);
					}

					if ((edges & TileAdjacency::EdgeBit::East) != 0) {
						// East edge: right side of tile (shadow/thick)
						auto sv = static_cast<uint16_t>(m_vertices.size());
						m_vertices.emplace_back(screenX + tileScreenSize - strokeShadow, screenY);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY + tileScreenSize);
						m_vertices.emplace_back(screenX + tileScreenSize - strokeShadow, screenY + tileScreenSize);
						for (int i = 0; i < 4; ++i) m_colors.emplace_back(strokeColor);
						m_indices.push_back(sv); m_indices.push_back(sv + 1); m_indices.push_back(sv + 2);
						m_indices.push_back(sv); m_indices.push_back(sv + 2); m_indices.push_back(sv + 3);
					}

					// ========== Diagonal Corner Strokes ==========
					// Draw small corner rectangles where lower surface is diagonally adjacent
					// Corner sizes follow the shadow pattern: light on top/left, shadow on bottom/right
					if ((corners & TileAdjacency::CornerBit::NW) != 0) {
						// NW corner: top-left (light width × light height)
						auto sv = static_cast<uint16_t>(m_vertices.size());
						m_vertices.emplace_back(screenX, screenY);
						m_vertices.emplace_back(screenX + strokeLight, screenY);
						m_vertices.emplace_back(screenX + strokeLight, screenY + strokeLight);
						m_vertices.emplace_back(screenX, screenY + strokeLight);
						for (int i = 0; i < 4; ++i) m_colors.emplace_back(strokeColor);
						m_indices.push_back(sv); m_indices.push_back(sv + 1); m_indices.push_back(sv + 2);
						m_indices.push_back(sv); m_indices.push_back(sv + 2); m_indices.push_back(sv + 3);
					}

					if ((corners & TileAdjacency::CornerBit::NE) != 0) {
						// NE corner: top-right (shadow width × light height)
						auto sv = static_cast<uint16_t>(m_vertices.size());
						m_vertices.emplace_back(screenX + tileScreenSize - strokeShadow, screenY);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY + strokeLight);
						m_vertices.emplace_back(screenX + tileScreenSize - strokeShadow, screenY + strokeLight);
						for (int i = 0; i < 4; ++i) m_colors.emplace_back(strokeColor);
						m_indices.push_back(sv); m_indices.push_back(sv + 1); m_indices.push_back(sv + 2);
						m_indices.push_back(sv); m_indices.push_back(sv + 2); m_indices.push_back(sv + 3);
					}

					if ((corners & TileAdjacency::CornerBit::SE) != 0) {
						// SE corner: bottom-right (shadow width × shadow height)
						auto sv = static_cast<uint16_t>(m_vertices.size());
						m_vertices.emplace_back(screenX + tileScreenSize - strokeShadow, screenY + tileScreenSize - strokeShadow);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY + tileScreenSize - strokeShadow);
						m_vertices.emplace_back(screenX + tileScreenSize, screenY + tileScreenSize);
						m_vertices.emplace_back(screenX + tileScreenSize - strokeShadow, screenY + tileScreenSize);
						for (int i = 0; i < 4; ++i) m_colors.emplace_back(strokeColor);
						m_indices.push_back(sv); m_indices.push_back(sv + 1); m_indices.push_back(sv + 2);
						m_indices.push_back(sv); m_indices.push_back(sv + 2); m_indices.push_back(sv + 3);
					}

					if ((corners & TileAdjacency::CornerBit::SW) != 0) {
						// SW corner: bottom-left (light width × shadow height)
						auto sv = static_cast<uint16_t>(m_vertices.size());
						m_vertices.emplace_back(screenX, screenY + tileScreenSize - strokeShadow);
						m_vertices.emplace_back(screenX + strokeLight, screenY + tileScreenSize - strokeShadow);
						m_vertices.emplace_back(screenX + strokeLight, screenY + tileScreenSize);
						m_vertices.emplace_back(screenX, screenY + tileScreenSize);
						for (int i = 0; i < 4; ++i) m_colors.emplace_back(strokeColor);
						m_indices.push_back(sv); m_indices.push_back(sv + 1); m_indices.push_back(sv + 2);
						m_indices.push_back(sv); m_indices.push_back(sv + 2); m_indices.push_back(sv + 3);
					}
				}
			}
		}
	}

} // namespace engine::world
