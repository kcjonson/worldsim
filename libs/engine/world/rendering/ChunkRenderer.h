#pragma once

// ChunkRenderer - Renders ground tiles from per-chunk tile-data textures.
// Each visible chunk is a single quad; the fragment shader fetches per-tile
// data (surface, masks, neighbors) from an RGBA32UI texture that mirrors the
// chunk's TileRenderData array. Tile geometry never touches the CPU per frame.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/camera/WorldCamera.h"

#include <gl/GLBuffer.h>
#include <gl/GLTexture.h>
#include <gl/GLVertexArray.h>
#include <shader/Shader.h>

#include <cstdint>
#include <unordered_map>

namespace engine::world {

/// Renders chunks as colored ground tiles via tile-data textures.
class ChunkRenderer {
  public:
	/// Create a chunk renderer
	/// @param pixelsPerMeter Scale factor for world-to-screen conversion
	explicit ChunkRenderer(float pixelsPerMeter = 16.0F);

	// Non-copyable, non-movable (owns GPU resources)
	ChunkRenderer(const ChunkRenderer&) = delete;
	ChunkRenderer& operator=(const ChunkRenderer&) = delete;
	ChunkRenderer(ChunkRenderer&&) = delete;
	ChunkRenderer& operator=(ChunkRenderer&&) = delete;
	~ChunkRenderer() = default;

	/// Render visible chunks
	/// @param chunkManager Chunk manager with loaded chunks
	/// @param camera Camera for visibility culling
	/// @param viewportWidth Viewport width in pixels
	/// @param viewportHeight Viewport height in pixels
	void render(const ChunkManager& chunkManager, const WorldCamera& camera, int viewportWidth, int viewportHeight);

	/// Set pixels per meter (zoom level)
	void setPixelsPerMeter(float pixelsPerMeter) { m_pixelsPerMeter = pixelsPerMeter; }
	[[nodiscard]] float pixelsPerMeter() const { return m_pixelsPerMeter; }

	/// Get number of tiles visible in last frame (for profiling)
	[[nodiscard]] uint32_t lastTileCount() const { return m_lastTileCount; }

	/// Get number of chunks rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastChunkCount() const { return m_lastChunkCount; }

  private:
	/// GPU tile-data texture for one chunk, with the render-data version it was
	/// uploaded from (stale versions are re-uploaded before drawing).
	struct ChunkTileTexture {
		Renderer::GLTexture texture;
		uint32_t version = 0;
		uint64_t lastAccessFrame = 0;
	};

	// LRU cache: 512x512 RGBA32UI = 4 MB per chunk
	static constexpr size_t kMaxCachedTextures = 32; // 128 MB cap
	static constexpr size_t kEvictionBatchSize = 8;

	// Cap stale-texture re-uploads (4 MB each) per frame; adjacency stitching
	// can dirty several visible chunks in the same update
	static constexpr int kMaxStaleReuploadsPerFrame = 1;

	/// Lazily create shader + unit quad (requires GL context)
	bool initGL();

	/// Get (uploading/refreshing if needed) the tile-data texture for a chunk
	ChunkTileTexture& ensureTexture(const Chunk& chunk);

	/// Evict least-recently-used textures when over the cache cap
	void evictStaleTextures();

	float m_pixelsPerMeter = 16.0F;
	uint32_t m_lastTileCount = 0;
	uint32_t m_lastChunkCount = 0;
	uint64_t m_frameCounter = 0;
	int m_staleReuploadsThisFrame = 0;

	bool m_glInitAttempted = false;
	Renderer::Shader m_shader;
	Renderer::GLVertexArray m_quadVAO;
	Renderer::GLBuffer m_quadVBO;

	std::unordered_map<ChunkCoordinate, ChunkTileTexture> m_textureCache;

	struct UniformLocations {
		int projection = -1;
		int chunkOrigin = -1;
		int chunkWorldSize = -1;
		int chunkTileOrigin = -1;
		int cameraPos = -1;
		int cameraZoom = -1;
		int pixelsPerMeter = -1;
		int viewportSize = -1;
		int tileData = -1;
		int tileAtlas = -1;
		int tileAtlasRectCount = -1;
		int tileAtlasRects = -1;
	};
	UniformLocations m_loc;
};

}  // namespace engine::world
