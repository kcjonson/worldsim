#pragma once

// ChunkRenderer - Renders chunks as colored ground tiles.
// Uses interior tile early-out optimization in shader for performance.
// FBO caching infrastructure exists but is currently disabled due to quality issues.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/camera/WorldCamera.h"

#include <gl/GLBuffer.h>
#include <gl/GLFramebuffer.h>
#include <gl/GLTexture.h>
#include <gl/GLVertexArray.h>
#include <graphics/Color.h>
#include <graphics/Rect.h>
#include <math/Types.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace engine::world {

/// Renders chunks as colored ground tiles.
/// Uses interior tile early-out in shader for performance.
/// FBO caching infrastructure exists but is disabled due to quality issues (blur, lost edges).
class ChunkRenderer {
  public:
	/// Create a chunk renderer
	/// @param pixelsPerMeter Scale factor for world-to-screen conversion
	explicit ChunkRenderer(float pixelsPerMeter = 16.0F);

	/// Destructor - releases GPU resources
	~ChunkRenderer();

	// Non-copyable, non-movable (owns GPU resources)
	ChunkRenderer(const ChunkRenderer&) = delete;
	ChunkRenderer& operator=(const ChunkRenderer&) = delete;
	ChunkRenderer(ChunkRenderer&&) = delete;
	ChunkRenderer& operator=(ChunkRenderer&&) = delete;

	/// Render visible chunks
	/// @param chunkManager Chunk manager with loaded chunks
	/// @param camera Camera for visibility culling
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	void render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight);

	/// Set pixels per meter (zoom level)
	void setPixelsPerMeter(float pixelsPerMeter) { m_pixelsPerMeter = pixelsPerMeter; }
	[[nodiscard]] float pixelsPerMeter() const { return m_pixelsPerMeter; }

	/// Set tile resolution for rendering (how many tiles to skip when rendering)
	/// 1 = render every tile, 2 = skip every other tile, etc.
	/// Higher values = faster but lower quality
	void setTileResolution(int32_t resolution) { m_tileResolution = std::max(1, resolution); }
	[[nodiscard]] int32_t tileResolution() const { return m_tileResolution; }

	/// Get number of tiles rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastTileCount() const { return m_lastTileCount; }

	/// Get number of chunks rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastChunkCount() const { return m_lastChunkCount; }

  private:
	float m_pixelsPerMeter = 16.0F;
	int32_t m_tileResolution = 1;
	uint32_t m_lastTileCount = 0;
	uint32_t m_lastChunkCount = 0;

	// --- FBO Tile Cache ---
	// Tiles are static, so we render them once to a texture and reuse.

	/// Resolution of cached chunk textures (pixels per side)
	static constexpr int kCacheTextureSize = 2048;

	/// Cached chunk texture data
	struct CachedChunkTexture {
		Renderer::GLFramebuffer fbo;
		Renderer::GLTexture texture;
		bool valid = false;
	};

	/// Cache of rendered chunk textures, keyed by chunk coordinate
	std::unordered_map<ChunkCoordinate, CachedChunkTexture> m_chunkCache;

	/// VAO/VBO for drawing cached texture quads
	Renderer::GLVertexArray m_quadVAO;
	Renderer::GLBuffer m_quadVBO;
	bool m_quadInitialized = false;

	/// Initialize the quad geometry for cached texture rendering
	void initQuadGeometry();

	/// Render all tiles of a chunk to its cached texture (one-time per chunk)
	void renderChunkToCache(const Chunk& chunk);

	/// Draw a cached chunk texture to screen
	void drawCachedChunk(const CachedChunkTexture& cache, const Chunk& chunk,
	                     const WorldCamera& camera, int viewportWidth, int viewportHeight);

	/// Primary tile rendering method - adds visible tiles from a chunk directly
	void addChunkTiles(const Chunk& chunk, const WorldCamera& camera, const Foundation::Rect& visibleRect,
					   int viewportWidth, int viewportHeight);
};

}  // namespace engine::world
