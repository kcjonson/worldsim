#include "ChunkRenderer.h"

#include <primitives/BatchRenderer.h>
#include <primitives/Primitives.h>
#include <utils/Log.h>

#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace engine::world {

	namespace {
		// The tile-data texture is uploaded as raw TileRenderData; the fragment
		// shader unpacks bytes from uint texels assuming this exact layout.
		static_assert(sizeof(TileRenderData) == 16, "tile.frag texel layout must match TileRenderData");
	} // namespace

	ChunkRenderer::ChunkRenderer(float pixelsPerMeter)
		: m_pixelsPerMeter(pixelsPerMeter) {}

	bool ChunkRenderer::initGL() {
		if (m_glInitAttempted) {
			return m_shader.IsValid();
		}
		m_glInitAttempted = true;

		if (!m_shader.LoadFromFile("tile.vert", "tile.frag")) {
			LOG_ERROR(Renderer, "ChunkRenderer: failed to load tile shader");
			return false;
		}

		GLuint program = m_shader.getProgram();
		m_loc.projection = glGetUniformLocation(program, "u_projection");
		m_loc.chunkOrigin = glGetUniformLocation(program, "u_chunkOrigin");
		m_loc.chunkWorldSize = glGetUniformLocation(program, "u_chunkWorldSize");
		m_loc.chunkTileOrigin = glGetUniformLocation(program, "u_chunkTileOrigin");
		m_loc.cameraPos = glGetUniformLocation(program, "u_cameraPos");
		m_loc.cameraZoom = glGetUniformLocation(program, "u_cameraZoom");
		m_loc.pixelsPerMeter = glGetUniformLocation(program, "u_pixelsPerMeter");
		m_loc.viewportSize = glGetUniformLocation(program, "u_viewportSize");
		m_loc.tileData = glGetUniformLocation(program, "u_tileData");
		m_loc.tileAtlas = glGetUniformLocation(program, "u_tileAtlas");
		m_loc.tileAtlasRectCount = glGetUniformLocation(program, "u_tileAtlasRectCount");
		m_loc.tileAtlasRects = glGetUniformLocation(program, "u_tileAtlasRects");

		// Unit quad as triangle strip: (0,0) (1,0) (0,1) (1,1)
		constexpr float kQuad[] = {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F, 1.0F, 1.0F};
		m_quadVAO = Renderer::GLVertexArray::create();
		m_quadVBO = Renderer::GLBuffer::create(GL_ARRAY_BUFFER);
		m_quadVAO.bind();
		m_quadVBO.bind();
		glBufferData(GL_ARRAY_BUFFER, sizeof(kQuad), kQuad, GL_STATIC_DRAW);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
		Renderer::GLVertexArray::unbind();

		return true;
	}

	ChunkRenderer::ChunkTileTexture& ChunkRenderer::ensureTexture(const Chunk& chunk) {
		ChunkTileTexture& entry = m_textureCache[chunk.coordinate()];
		entry.lastAccessFrame = m_frameCounter;

		uint32_t version = chunk.renderDataVersion();
		if (entry.texture.isValid() && entry.version == version) {
			return entry;
		}

		if (!entry.texture.isValid()) {
			entry.texture = Renderer::GLTexture(
				kChunkSize, kChunkSize, GL_RGBA32UI, GL_RGBA_INTEGER, GL_UNSIGNED_INT, chunk.renderData()
			);
			// Integer textures require NEAREST filtering (ctor default is LINEAR)
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		} else {
			entry.texture.bind();
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kChunkSize, kChunkSize, GL_RGBA_INTEGER, GL_UNSIGNED_INT, chunk.renderData());
		}
		entry.version = version;
		return entry;
	}

	void ChunkRenderer::evictStaleTextures() {
		if (m_textureCache.size() <= kMaxCachedTextures) {
			return;
		}

		std::vector<std::pair<ChunkCoordinate, uint64_t>> byAge;
		byAge.reserve(m_textureCache.size());
		for (const auto& [coord, entry] : m_textureCache) {
			if (entry.lastAccessFrame != m_frameCounter) { // never evict chunks drawn this frame
				byAge.emplace_back(coord, entry.lastAccessFrame);
			}
		}
		std::sort(byAge.begin(), byAge.end(), [](const auto& a, const auto& b) { return a.second < b.second; });

		size_t toEvict = std::min(byAge.size(), kEvictionBatchSize);
		for (size_t i = 0; i < toEvict; ++i) {
			m_textureCache.erase(byAge[i].first);
		}
	}

	void ChunkRenderer::render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight) {
		m_lastTileCount = 0;
		m_lastChunkCount = 0;
		m_frameCounter++;

		Foundation::Rect visibleRect = camera.getVisibleRect(viewportWidth, viewportHeight, m_pixelsPerMeter);
		auto [minCorner, maxCorner] = camera.getVisibleCorners(viewportWidth, viewportHeight, m_pixelsPerMeter);
		std::vector<const Chunk*> visibleChunks = chunkManager.getVisibleChunks(minCorner, maxCorner);
		if (visibleChunks.empty()) {
			return;
		}

		if (!initGL()) {
			return;
		}

		// Flush any pending batched geometry so draw order stays correct
		auto* batchRenderer = Renderer::Primitives::getBatchRenderer();
		if (batchRenderer != nullptr) {
			batchRenderer->flush();
		}

		// Save GL state before modifying (mirrors EntityRenderer's baked pass)
		GLboolean blendEnabled = glIsEnabled(GL_BLEND);
		GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
		GLboolean cullFaceEnabled = glIsEnabled(GL_CULL_FACE);

		glDisable(GL_BLEND); // tiles are opaque and fill their quad
		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		m_shader.use();

		glm::mat4 projection =
			glm::ortho(0.0F, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight), 0.0F, -1.0F, 1.0F);
		glUniformMatrix4fv(m_loc.projection, 1, GL_FALSE, glm::value_ptr(projection));
		glUniform2f(m_loc.cameraPos, camera.position().x, camera.position().y);
		glUniform1f(m_loc.cameraZoom, camera.zoom());
		glUniform1f(m_loc.pixelsPerMeter, m_pixelsPerMeter);
		glUniform2f(m_loc.viewportSize, static_cast<float>(viewportWidth), static_cast<float>(viewportHeight));

		// Tile atlas on unit 0, per-chunk tile data on unit 1
		const auto& atlasRects = Renderer::Primitives::getTileAtlasRects();
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, Renderer::Primitives::getTileAtlasTexture());
		glUniform1i(m_loc.tileAtlas, 0);
		glUniform1i(m_loc.tileAtlasRectCount, static_cast<GLint>(atlasRects.size()));
		if (!atlasRects.empty()) {
			glUniform4fv(m_loc.tileAtlasRects, static_cast<GLsizei>(atlasRects.size()), reinterpret_cast<const float*>(atlasRects.data()));
		}
		glUniform1i(m_loc.tileData, 1);
		glActiveTexture(GL_TEXTURE1);

		m_quadVAO.bind();

		constexpr float kChunkWorldSize = static_cast<float>(kChunkSize) * kTileSize;
		for (const Chunk* chunk : visibleChunks) {
			if (!chunk->isReady()) {
				continue;
			}

			ChunkTileTexture& entry = ensureTexture(*chunk);
			entry.texture.bind(); // unit 1 is active

			WorldPosition origin = chunk->worldOrigin();
			ChunkCoordinate coord = chunk->coordinate();
			glUniform2f(m_loc.chunkOrigin, origin.x, origin.y);
			glUniform1f(m_loc.chunkWorldSize, kChunkWorldSize);
			glUniform2i(m_loc.chunkTileOrigin, coord.x * kChunkSize, coord.y * kChunkSize);

			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
			m_lastChunkCount++;

			// Visible tile count for metrics: intersection of viewport and chunk, in tiles
			float visMinX = std::max(origin.x, visibleRect.x);
			float visMaxX = std::min(origin.x + kChunkWorldSize, visibleRect.x + visibleRect.width);
			float visMinY = std::max(origin.y, visibleRect.y);
			float visMaxY = std::min(origin.y + kChunkWorldSize, visibleRect.y + visibleRect.height);
			if (visMaxX > visMinX && visMaxY > visMinY) {
				m_lastTileCount += static_cast<uint32_t>(std::ceil(visMaxX - visMinX) * std::ceil(visMaxY - visMinY));
			}
		}

		Renderer::GLVertexArray::unbind();
		glActiveTexture(GL_TEXTURE0);

		evictStaleTextures();

		// Restore GL state
		if (blendEnabled == GL_TRUE) {
			glEnable(GL_BLEND);
		} else {
			glDisable(GL_BLEND);
		}
		if (depthTestEnabled == GL_TRUE) {
			glEnable(GL_DEPTH_TEST);
		} else {
			glDisable(GL_DEPTH_TEST);
		}
		if (cullFaceEnabled == GL_TRUE) {
			glEnable(GL_CULL_FACE);
		} else {
			glDisable(GL_CULL_FACE);
		}
	}

} // namespace engine::world
