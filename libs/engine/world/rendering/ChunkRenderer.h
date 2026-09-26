#pragma once

// ChunkRenderer - Renders ground tiles and water from per-chunk textures.
// Each visible chunk is a single quad; the fragment shader fetches per-tile
// data (surface, masks, neighbors) from an RGBA32UI texture that mirrors the
// chunk's TileRenderData array, then paints water and shore from the chunk's
// terrain distance field (terrain-polygons-architecture.md D10). Tile geometry
// never touches the CPU per frame.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/camera/WorldCamera.h"

#include <gl/GLBuffer.h>
#include <gl/GLTexture.h>
#include <gl/GLVertexArray.h>
#include <shader/Shader.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace engine::world {

/// Renders chunks as colored ground tiles via tile-data textures, with water on top.
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
	void setPixelsPerMeter(float newPixelsPerMeter) { pixelsPerMeterValue = newPixelsPerMeter; }
	[[nodiscard]] float pixelsPerMeter() const { return pixelsPerMeterValue; }

	/// Get number of tiles visible in last frame (for profiling)
	[[nodiscard]] uint32_t lastTileCount() const { return lastTiles; }

	/// Get number of chunks rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastChunkCount() const { return lastChunks; }

	/// GPU bytes held by the cache: tile-data textures and distance-field textures.
	[[nodiscard]] size_t cachedTileBytes() const;
	[[nodiscard]] size_t cachedWaterBytes() const;

  private:
	/// A chunk's GPU textures: the tile data (keyed by renderDataVersion) and the
	/// distance field (keyed by terrainPolygons().version). A chunk with no water
	/// in reach holds no distance-field textures and draws the no-water path.
	struct ChunkTextures {
		Renderer::GLTexture tileData;
		uint32_t tileVersion = 0;

		Renderer::GLTexture sdfTileMap; ///< 32x32 R16UI atlas cell per 16 m tile; invalid when no near tiles
		Renderer::GLTexture sdfNear;	///< near tiles in an atlas, nearColumns cells per row
		Renderer::GLTexture sdfFar;
		Renderer::GLTexture shoreProfile;
		Renderer::GLTexture channelFrame;
		int nearColumns = 1;
		bool hasWater = false;
		bool waterUploaded = false;
		uint32_t waterVersion = 0;
		size_t waterBytes = 0;

		uint64_t lastAccessFrame = 0;
	};

	// LRU cache: 512x512 RGBA32UI = 4 MB tile data per chunk, plus the distance
	// field (0 for a dry chunk, a few MB on a shore)
	static constexpr size_t kMaxCachedTextures = 32;
	static constexpr size_t kEvictionBatchSize = 8;
	static constexpr size_t kTileDataBytes = static_cast<size_t>(kChunkSize) * kChunkSize * sizeof(TileRenderData);

	// Stale re-uploads (adjacency stitching, a rebuilt ring set) can dirty several
	// visible chunks in the same update. The first stale re-upload each frame
	// always runs; more run only while the frame stays under this many bytes.
	// A frame of stale data is invisible.
	static constexpr size_t kStaleReuploadBudgetBytes = kTileDataBytes;

	/// Lazily create shader, unit quad, and default textures (requires GL context)
	bool initGL();

	/// Get (uploading/refreshing if needed) the textures for a chunk
	ChunkTextures& ensureTextures(const Chunk& chunk);

	/// True when a stale re-upload of `bytes` fits this frame; counts it if so.
	bool takeStaleBudget(size_t bytes);

	/// (Re)create a chunk's distance-field textures from its bake. Returns bytes uploaded.
	static size_t uploadDistanceField(ChunkTextures& entry, const TerrainDistanceField& field);

	/// Bind a chunk's distance-field textures (or the defaults) and set its per-chunk water uniforms.
	void bindWater(const ChunkTextures& entry) const;

	/// Evict least-recently-used textures when over the cache cap
	void evictStaleTextures();

	/// Set every tunable uniform from the Tunables registry (once per frame).
	void applyTunables() const;

	float pixelsPerMeterValue = 16.0F;
	uint32_t lastTiles = 0;
	uint32_t lastChunks = 0;
	uint64_t frameCounter = 0;
	size_t staleBytesThisFrame = 0;
	std::chrono::steady_clock::time_point startTime = std::chrono::steady_clock::now();

	bool glInitAttempted = false;
	Renderer::Shader shader;
	Renderer::GLVertexArray quadVAO;
	Renderer::GLBuffer quadVBO;

	// 1x1 stand-ins bound for a water chunk's empty textures (clamped, so every lookup reads the default)
	Renderer::GLTexture defaultTileMap;	 ///< kNoNearTile
	Renderer::GLTexture defaultFar;		 ///< kLandSdfTexel
	Renderer::GLTexture defaultProfile;	 ///< zero
	Renderer::GLTexture defaultFrame;	 ///< zero

	std::unordered_map<ChunkCoordinate, ChunkTextures> textureCache;

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
		int hasWater = -1;
		int sdfTileMap = -1;
		int sdfNear = -1;
		int sdfNearColumns = -1;
		int sdfFar = -1;
		int shoreProfile = -1;
		int channelFrame = -1;
		int time = -1;
		int metersPerPixel = -1;
	};
	UniformLocations loc;

	/// A tunable's uniform: its location and the registry storage it reads.
	struct TunableUniform {
		int location = -1;
		const float* values = nullptr;
		size_t count = 1;
	};
	std::vector<TunableUniform> tunableUniforms;
};

}  // namespace engine::world
