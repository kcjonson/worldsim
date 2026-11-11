#pragma once

// ChunkRenderer - Renders ground tiles and water from per-chunk textures.
// Each visible chunk is a single quad; the fragment shader paints the ground from
// an RG8UI texture of the chunk's render tiles (paint surface and edge byte, the
// square plus its render apron), every surface boundary the isoline of a warped
// field (terrain-polygons-architecture.md D16), each surface's baked ground tiles
// (GroundTileBank) hex-tiled and tinted by the chunk's macro texture, then paints
// water and shore from the chunk's terrain distance field (D10). Tile geometry
// never touches the CPU per frame.

#include "world/chunk/Chunk.h"
#include "world/chunk/ChunkManager.h"
#include "world/chunk/SurfaceField.h"
#include "world/camera/WorldCamera.h"
#include "world/rendering/GroundTileBank.h"

#include <backend/gl/GLBuffer.h>
#include <backend/gl/GLTexture.h>
#include <backend/gl/GLVertexArray.h>
#include <backend/gl/Shader.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace worldview {

/// Renders chunks as colored ground tiles via tile-data textures, with water on top.
class ChunkRenderer {
  public:
	/// @param pixelsPerMeter Scale factor for world-to-screen conversion
	/// @param groundTiles The baked ground tiles, ready before the first render; outlives the renderer
	ChunkRenderer(float pixelsPerMeter, const GroundTileBank& groundTiles);

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
	void render(const world::ChunkManager& chunkManager, const world::WorldCamera& camera, int viewportWidth, int viewportHeight);

	/// Set pixels per meter (zoom level)
	void setPixelsPerMeter(float newPixelsPerMeter) { pixelsPerMeterValue = newPixelsPerMeter; }
	[[nodiscard]] float pixelsPerMeter() const { return pixelsPerMeterValue; }

	/// Get number of tiles visible in last frame (for profiling)
	[[nodiscard]] uint32_t lastTileCount() const { return lastTiles; }

	/// Get number of chunks rendered in last frame (for profiling)
	[[nodiscard]] uint32_t lastChunkCount() const { return lastChunks; }

	/// GPU bytes held by the cache: render tiles with the macro textures, and distance-field textures.
	[[nodiscard]] size_t cachedTileBytes() const;
	[[nodiscard]] size_t cachedWaterBytes() const;

  private:
	/// A chunk's GPU textures: the tile data (keyed by renderDataVersion) and the
	/// distance field (keyed by terrainPolygons().version). A chunk with no water
	/// in reach holds no distance-field textures and draws the no-water path.
	struct ChunkTextures {
		Renderer::GLTexture tileData;
		Renderer::GLTexture groundMacro; ///< 257x257 RGBA8, same version as tileData
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

	// LRU cache: 518x518 RG8UI = 524 KB of render tiles and a 264 KB macro texture per
	// chunk, plus the distance field (0 for a dry chunk, a few MB on a shore)
	static constexpr size_t kMaxCachedTextures = 32;
	static constexpr size_t kEvictionBatchSize = 8;
	static constexpr size_t kTileDataBytes = static_cast<size_t>(world::kRenderTilesSide) * world::kRenderTilesSide * sizeof(world::TileRenderData);
	static constexpr size_t kChunkTileBytes = kTileDataBytes + world::kGroundMacroBytes;

	// Stale re-uploads (a regenerated chunk, a rebuilt ring set) can dirty several
	// visible chunks in the same update. The first stale re-upload each frame
	// always runs; more run only while the frame stays under this many bytes.
	// A frame of stale data is invisible.
	static constexpr size_t kStaleReuploadBudgetBytes = 4U << 20U;

	/// Lazily create shader, unit quad, and default textures (requires GL context)
	bool initGL();

	/// Get (uploading/refreshing if needed) the textures for a chunk
	ChunkTextures& ensureTextures(const world::Chunk& chunk);

	/// True when a stale re-upload of `bytes` fits this frame; counts it if so.
	bool takeStaleBudget(size_t bytes);

	/// (Re)create a chunk's distance-field textures from its bake. Returns bytes uploaded.
	static size_t uploadDistanceField(ChunkTextures& entry, const world::TerrainDistanceField& field);

	/// Bind a chunk's distance-field textures (or the defaults) and set its per-chunk water uniforms.
	void bindWater(const ChunkTextures& entry) const;

	/// Evict least-recently-used textures when over the cache cap
	void evictStaleTextures();

	/// Set every tunable uniform from the Tunables registry (once per frame).
	void applyTunables() const;

	/// Set the land field's uniforms (once per frame): its tunables through
	/// SurfaceFieldParams, so the shader gets the exact values an evaluation on the
	/// CPU uses, seeded from the world, and the fine warp octaves worth drawing at
	/// this pixel size.
	void applyLandUniforms(uint64_t worldSeed, float metersPerPixel) const;

	/// Bind the ground tile arrays and set the ground's uniforms (once per frame):
	/// each surface's layers and mip level at this many screen pixels a meter, the
	/// zoom's blend gain and hex cutoff, the macro tints, and the pair table with
	/// this zoom's mip window (GroundPairs.h).
	void applyGroundUniforms(uint64_t worldSeed, float screenPixelsPerMeter) const;

	const GroundTileBank* groundTiles = nullptr;
	bool loggedUnreadyGround = false;
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

	std::unordered_map<world::ChunkCoordinate, ChunkTextures> textureCache;

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
		std::array<int, GroundTileBank::kMaxArrays> groundTiles{-1, -1, -1, -1};
		int groundLayers = -1;
		int groundParams = -1;
		int groundTintBase = -1;
		int groundTintLush = -1;
		int groundFloorTint = -1;
		int groundSeed = -1;
		int groundCellOrigin = -1;
		int groundMacroValue = -1;
		int groundHex = -1;
		int groundBlendGainK = -1;
		int groundMacro = -1;
		int hasWater = -1;
		int sdfTileMap = -1;
		int sdfNear = -1;
		int sdfNearColumns = -1;
		int sdfFar = -1;
		int shoreProfile = -1;
		int channelFrame = -1;
		int time = -1;
		int metersPerPixel = -1;
		int landWarpFine = -1;
		int landWarpLow = -1;
		int landWarpWavelength = -1;
		int landWarpInvWavelength = -1;
		int landWarpFineGain = -1;
		int landWarpSeeds = -1;
		int landThinFloor = -1;
		int landThinCeil = -1;
		int landWarpFineOctaves = -1;
		int landPair = -1;
	};
	UniformLocations loc;

	/// The ground's tunables the CPU turns into per-frame uniforms; the ones the
	/// shader reads as they stand ride tunableUniforms.
	struct GroundTunables {
		const float* lodBias = nullptr;			 ///< added to every surface's mip level
		const float* hexMinPxPerMeter = nullptr; ///< below it, one sample a pixel
		const float* blendGain = nullptr;		 ///< Mikkelsen's r near, mid and far (3 values)
		const float* macroAmp = nullptr;		 ///< scales every def's macro tint amplitude
		const float* macroMidAmp = nullptr;		 ///< value swing of the macro's mid octave
		const float* macroFineAmp = nullptr;	 ///< and of its fine one
	};
	GroundTunables groundLook;

	/// A tunable's uniform: its location and the registry storage it reads.
	struct TunableUniform {
		int location = -1;
		const float* values = nullptr;
		size_t count = 1;
	};
	std::vector<TunableUniform> tunableUniforms;
};

}  // namespace worldview
