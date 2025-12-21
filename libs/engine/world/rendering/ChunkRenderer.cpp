#include "ChunkRenderer.h"

#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <vector>

namespace engine::world {

	ChunkRenderer::ChunkRenderer(float pixelsPerMeter)
		: m_pixelsPerMeter(pixelsPerMeter) {}

	ChunkRenderer::~ChunkRenderer() {
		// RAII handles GPU resource cleanup
		m_chunkCache.clear();
	}

	void ChunkRenderer::initQuadGeometry() {
		if (m_quadInitialized) {
			return;
		}

		// Simple unit quad with UV coords: position (x, y), texCoord (u, v)
		// Positions are 0-1, will be scaled to chunk size in shader
		float quadVertices[] = {
			// pos      // uv
			0.0F, 0.0F, 0.0F, 0.0F, // bottom-left
			1.0F, 0.0F, 1.0F, 0.0F, // bottom-right
			1.0F, 1.0F, 1.0F, 1.0F, // top-right
			0.0F, 0.0F, 0.0F, 0.0F, // bottom-left
			1.0F, 1.0F, 1.0F, 1.0F, // top-right
			0.0F, 1.0F, 0.0F, 1.0F, // top-left
		};

		m_quadVAO = Renderer::GLVertexArray::create();
		m_quadVAO.bind();

		m_quadVBO = Renderer::GLBuffer(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

		// Position attribute
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);

		// TexCoord attribute
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), reinterpret_cast<void*>(2 * sizeof(float)));

		Renderer::GLVertexArray::unbind();
		m_quadInitialized = true;
	}

	void ChunkRenderer::renderChunkToCache(const Chunk& chunk) {
		ChunkCoordinate coord = chunk.coordinate();

		// Create cache entry if needed
		auto& cache = m_chunkCache[coord];
		if (!cache.fbo.isValid()) {
			cache.fbo = Renderer::GLFramebuffer::create();
			cache.texture = Renderer::GLTexture(kCacheTextureSize, kCacheTextureSize, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

			// Attach texture to FBO
			cache.fbo.bind();
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, cache.texture.handle(), 0);

			// Check FBO completeness
			if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
				Renderer::GLFramebuffer::unbind();
				return;
			}
			Renderer::GLFramebuffer::unbind();
		}

		// Save current viewport
		GLint prevViewport[4];
		glGetIntegerv(GL_VIEWPORT, prevViewport);

		// Bind FBO and set up for rendering
		cache.fbo.bind();
		glViewport(0, 0, kCacheTextureSize, kCacheTextureSize);
		glClearColor(0.0F, 0.0F, 0.0F, 0.0F);
		glClear(GL_COLOR_BUFFER_BIT);

		// Set up orthographic projection for chunk-local coordinates
		// Map 0..kChunkSize to 0..kCacheTextureSize
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer == nullptr) {
			Renderer::GLFramebuffer::unbind();
			glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
			return;
		}

		// Save and clear coordinate system - FBO rendering needs a simple ortho projection
		// based on FBO dimensions, not the screen's DPI-aware projection
		auto* savedCoordSystem = batchRenderer->getCoordinateSystem();
		batchRenderer->setCoordinateSystem(nullptr);

		// Temporarily set viewport for BatchRenderer
		batchRenderer->setViewport(kCacheTextureSize, kCacheTextureSize);

		// Calculate scale: pixels per tile in the cache texture
		float pixelsPerTile = static_cast<float>(kCacheTextureSize) / static_cast<float>(kChunkSize);

		// Render all tiles to the FBO
		for (int32_t tileY = 0; tileY < kChunkSize; tileY += m_tileResolution) {
			for (int32_t tileX = 0; tileX < kChunkSize; tileX += m_tileResolution) {
				const TileRenderData& render = chunk.getTileRenderData(static_cast<uint16_t>(tileX), static_cast<uint16_t>(tileY));

				// Tile position in cache texture (screen coords for FBO)
				float screenX = static_cast<float>(tileX) * pixelsPerTile;
				float screenY = static_cast<float>(tileY) * pixelsPerTile;
				float tileScreenSize = pixelsPerTile * static_cast<float>(m_tileResolution);

				// World tile coordinates for procedural edge variation
				int32_t worldTileX = coord.x * kChunkSize + tileX;
				int32_t worldTileY = coord.y * kChunkSize + tileY;

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
			}
		}

		// Flush to actually render tiles to FBO
		batchRenderer->flush();

		// Restore coordinate system and viewport
		batchRenderer->setCoordinateSystem(savedCoordSystem);
		Renderer::GLFramebuffer::unbind();
		glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
		batchRenderer->setViewport(prevViewport[2], prevViewport[3]);

		cache.valid = true;
	}

	void ChunkRenderer::drawCachedChunk(
		const CachedChunkTexture& cache,
		const Chunk&			  chunk,
		const WorldCamera&		  camera,
		int						  viewportWidth,
		int						  viewportHeight
	) {
		if (!cache.valid) {
			return;
		}

		// Get BatchRenderer for shader access
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer == nullptr) {
			return;
		}

		// Calculate screen-space bounds for this chunk
		WorldPosition chunkOrigin = chunk.worldOrigin();
		float		  chunkWorldSize = static_cast<float>(kChunkSize) * kTileSize;

		float scale = m_pixelsPerMeter * camera.zoom();
		float halfViewW = static_cast<float>(viewportWidth) * 0.5F;
		float halfViewH = static_cast<float>(viewportHeight) * 0.5F;
		float camX = camera.position().x;
		float camY = camera.position().y;

		// Chunk corners in screen space
		float screenX = (chunkOrigin.x - camX) * scale + halfViewW;
		float screenY = (chunkOrigin.y - camY) * scale + halfViewH;
		float screenW = chunkWorldSize * scale;
		float screenH = chunkWorldSize * scale;

		// Use BatchRenderer's shader but draw our own quad
		GLuint shaderProgram = batchRenderer->getShaderProgram();
		glUseProgram(shaderProgram);

		// Set up projection matrix (same as BatchRenderer)
		glm::mat4 projection = glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);

		GLint projLoc = glGetUniformLocation(shaderProgram, "u_projection");
		GLint transformLoc = glGetUniformLocation(shaderProgram, "u_transform");
		GLint instancedLoc = glGetUniformLocation(shaderProgram, "u_instanced");
		GLint atlasLoc = glGetUniformLocation(shaderProgram, "u_atlas");

		glUniformMatrix4fv(projLoc, 1, GL_FALSE, glm::value_ptr(projection));
		glm::mat4 identity(1.0F);
		glUniformMatrix4fv(transformLoc, 1, GL_FALSE, glm::value_ptr(identity));
		glUniform1i(instancedLoc, 0);

		// Bind cached texture to texture unit 0
		glActiveTexture(GL_TEXTURE0);
		cache.texture.bind();
		glUniform1i(atlasLoc, 0);

		// Enable blending
		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		// Build a textured quad with screen-space positions
		// Using cached texture mode (-4) for simple texture sampling
		float renderMode = -4.0F; // CachedTexture mode - simple texture lookup

		// Quad vertices: position (vec2), texCoord (vec2), color (vec4),
		// data1 (vec4), data2 (vec4), clipBounds (vec4), data3 (vec4)
		// Total: 96 bytes per vertex (matching UberVertex)
		struct QuadVertex {
			float pos[2];
			float uv[2];
			float color[4];
			float data1[4];
			float data2[4];
			float clipBounds[4];
			float data3[4];
		};

		// Note: FBO textures have Y-axis flipped (OpenGL origin at bottom-left)
		// So we flip V coordinates: top of screen (screenY) samples from V=1, bottom samples from V=0
		QuadVertex vertices[6] = {
			// Triangle 1
			{{screenX, screenY}, {0.0F, 1.0F}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, renderMode}, {0, 0, 0, 0}, {0, 0, 0, 0}},
			{{screenX + screenW, screenY}, {1.0F, 1.0F}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, renderMode}, {0, 0, 0, 0}, {0, 0, 0, 0}},
			{{screenX + screenW, screenY + screenH},
			 {1.0F, 0.0F},
			 {1, 1, 1, 1},
			 {0, 0, 0, 0},
			 {0, 0, 0, renderMode},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},
			// Triangle 2
			{{screenX, screenY}, {0.0F, 1.0F}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, renderMode}, {0, 0, 0, 0}, {0, 0, 0, 0}},
			{{screenX + screenW, screenY + screenH},
			 {1.0F, 0.0F},
			 {1, 1, 1, 1},
			 {0, 0, 0, 0},
			 {0, 0, 0, renderMode},
			 {0, 0, 0, 0},
			 {0, 0, 0, 0}},
			{{screenX, screenY + screenH}, {0.0F, 0.0F}, {1, 1, 1, 1}, {0, 0, 0, 0}, {0, 0, 0, renderMode}, {0, 0, 0, 0}, {0, 0, 0, 0}},
		};

		// Create temporary VAO/VBO for this draw
		GLuint vao, vbo;
		glGenVertexArrays(1, &vao);
		glGenBuffers(1, &vbo);

		glBindVertexArray(vao);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STREAM_DRAW);

		// Set up vertex attributes to match UberVertex layout
		size_t stride = sizeof(QuadVertex);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, nullptr);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(2 * sizeof(float)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(4 * sizeof(float)));
		glEnableVertexAttribArray(3);
		glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(8 * sizeof(float)));
		glEnableVertexAttribArray(4);
		glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(12 * sizeof(float)));
		glEnableVertexAttribArray(5);
		glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(16 * sizeof(float)));
		glEnableVertexAttribArray(8);
		glVertexAttribPointer(8, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<void*>(20 * sizeof(float)));

		glDrawArrays(GL_TRIANGLES, 0, 6);

		// Cleanup
		glBindVertexArray(0);
		glDeleteVertexArrays(1, &vao);
		glDeleteBuffers(1, &vbo);
		Renderer::GLTexture::unbind();
	}

	void ChunkRenderer::render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight) {
		m_lastTileCount = 0;
		m_lastChunkCount = 0;

		Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, m_pixelsPerMeter);
		auto [minCorner, maxCorner] = camera.getVisibleCorners(viewportWidth, viewportHeight, m_pixelsPerMeter);
		std::vector<const Chunk*> visibleChunks = chunkManager.getVisibleChunks(minCorner, maxCorner);

		// Direct rendering path - uses interior tile early-out in shader for performance
		// FBO caching was attempted but caused quality issues:
		// - Blur at high zoom levels (fixed resolution cache texture)
		// - Lost hard edges on terrain transitions
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
		// Fallback path - kept for reference but no longer used in normal operation
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
