#pragma once

// Chunk - A 512×512 tile region of the world.
// Contains sampled biome data and cached rendering data.
// Tiles are generated procedurally on-demand from the biome data.

#include "world/Biome.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"
#include "world/chunk/GroundMacro.h"
#include "world/chunk/RenderTiles.h"
#include "world/chunk/TerrainDistanceField.h"
#include "world/chunk/TerrainPolygons.h"
#include "world/chunk/TileBiomes.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace world {

/// Surface types for terrain rendering. Land surfaces are painted as isolines of
/// per-surface fields (SurfaceField, D16). Water is tile data only: the renderer
/// draws it from the terrain distance field (D10).
enum class Surface : uint8_t {
	Grass,       // 0 - Regular grassland (standard temperate grass)
	Dirt,        // 1 - Exposed dirt/mud
	Sand,        // 2 - Sandy terrain
	Rock,        // 3 - Rocky/stone surface
	Water,       // 4 - Water bodies
	Snow,        // 5 - Snow-covered ground
	Mud,         // 6 - Wet mud (darker than Dirt, appears near water)
	GrassTall,   // 7 - Long grass with seed heads, meadow feel
	GrassShort,  // 8 - Rocky/sparse grass, short stubble
	GrassMeadow, // 9 - Lush meadow variant, thicker coverage
	Count        // 10 - Sentinel value for iteration (must be last)
};

/// Convert Surface enum to string for placement rules and debugging
[[nodiscard]] inline std::string surfaceToString(Surface surface) {
	switch (surface) {
		case Surface::Grass:
			return "Grass";
		case Surface::Dirt:
			return "Dirt";
		case Surface::Sand:
			return "Sand";
		case Surface::Rock:
			return "Rock";
		case Surface::Water:
			return "Water";
		case Surface::Snow:
			return "Snow";
		case Surface::Mud:
			return "Mud";
		case Surface::GrassTall:
			return "GrassTall";
		case Surface::GrassShort:
			return "GrassShort";
		case Surface::GrassMeadow:
			return "GrassMeadow";
		default:
			return "Unknown";
	}
}

/// The Surface named by `name` (as surfaceToString spells it), or nullopt.
[[nodiscard]] inline std::optional<Surface> surfaceFromString(std::string_view name) {
	for (uint8_t i = 0; i < static_cast<uint8_t>(Surface::Count); ++i) {
		const auto surface = static_cast<Surface>(i);
		if (surfaceToString(surface) == name) {
			return surface;
		}
	}
	return std::nullopt;
}

/// Tile data - 8 bytes, stored in flat array per chunk.
/// Designed for single source of truth: computed once, read by all systems.
struct TileData {
	Surface surface = Surface::Grass;   ///< 1 byte - THE definitive terrain type
	Biome primaryBiome = Biome::TemperateGrassland;   ///< 1 byte - dominant biome
	Biome secondaryBiome = Biome::TemperateGrassland; ///< 1 byte - for ecotones (may equal primary)
	uint8_t biomeBlend = 255;           ///< 1 byte - weight of primary (255 = 100% primary)
	uint16_t elevation = 0;             ///< 2 bytes - centimeters above sea level
	uint8_t moisture = 128;             ///< 1 byte - normalized 0-255
	uint8_t waterDepth = 0;             ///< 1 byte - cosmetic water depth (0=land/shallowest, 255=deepest); data only, the renderer paints depth from the distance field
};

class ExtendedTiles;
class TerrainPolygonQuery;

namespace generation {
	class PatchNoise;
}

/// A 512×512 region of the world.
/// Tiles are pre-computed during generate() and stored in a flat array.
/// All systems read from the same definitive tile data.
class Chunk {
  public:
	/// Designated-initializer args for the shared tile-compute core (below). Biome
	/// and elevation are passed in already resolved (ChunkNeighborhood) rather
	/// than a sample-data reference, because the caller may be resolving them
	/// from a neighbor chunk's own corners (ApronField), not this chunk's.
	/// `hydrology` supplies riverHalfWidthAt/pondDepthAt; it is always the
	/// generating chunk's own sample data (gathered over the extended AABB, D4),
	/// since river/pond geometry is world-position-based and doesn't care which
	/// chunk's sampler gathered it.
	struct TileComputeArgs {
		ChunkCoordinate coord;   ///< Tile's owning chunk (tileHash + world position)
		uint16_t localX = 0;
		uint16_t localY = 0;
		TileBiome biome;
		const generation::PatchNoise* patchNoise = nullptr; ///< the owning chunk's (ChunkNeighborhood)
		float elevationMeters = 0.0F;
		const ChunkSampleResult* hydrology = nullptr;
		uint64_t worldSeed = 0;
	};

	/// Compute one tile. The one place tile surface/moisture/water logic is
	/// spelled out (docs/technical/organic-terrain/terrain-polygons-architecture.md
	/// D14): both Chunk::generate() (the chunk's own 512x512 tiles) and ApronField
	/// (the apron ring, evaluated against a neighbor's own corners) call through
	/// it, so there is exactly one tile-computation code path.
	[[nodiscard]] static TileData computeTileFrom(const TileComputeArgs& args);

	/// Deterministic hash of a tile (its owning chunk and local coordinates) and
	/// a seed: the world tile, whichever chunk asks.
	[[nodiscard]] static uint32_t tileHash(ChunkCoordinate chunk, uint16_t localX, uint16_t localY, uint64_t seed);

	/// Create a chunk with sampled biome data
	Chunk(ChunkCoordinate coord, ChunkSampleResult biomeData, uint64_t worldSeed);

	/// Pre-compute all tiles in this chunk. Call once after construction.
	/// Thread-safe: sets atomic flag when complete.
	void generate();

	/// Check if tiles have been generated (thread-safe)
	[[nodiscard]] bool isReady() const { return m_generationComplete.load(std::memory_order_acquire); }

	/// Get the chunk's grid coordinate
	[[nodiscard]] ChunkCoordinate coordinate() const { return m_coord; }

	/// Get the chunk's origin in world space
	[[nodiscard]] WorldPosition worldOrigin() const { return m_coord.origin(); }

	/// Get tile data at local coordinates (0-511, 0-511)
	/// Returns pre-computed tile from flat array (requires isReady() == true)
	[[nodiscard]] const TileData& getTile(uint16_t localX, uint16_t localY) const;

	[[nodiscard]] uint64_t worldSeed() const { return m_worldSeed; }

	/// Get the biome data for this chunk (used during generation)
	[[nodiscard]] const ChunkSampleResult& biomeData() const { return m_biomeData; }

	/// Get primary biome (dominant biome at chunk center)
	[[nodiscard]] Biome primaryBiome() const {
		// Return the biome of the center tile
		return m_tiles[256 * kChunkSize + 256].primaryBiome;
	}

	/// Update last accessed time (for LRU eviction)
	/// Note: touch() is const because m_lastAccessed is mutable - LRU timestamp
	/// is not considered part of the logical state of the chunk.
	void touch() const { m_lastAccessed = std::chrono::steady_clock::now(); }

	/// Get last accessed time
	[[nodiscard]] auto lastAccessed() const { return m_lastAccessed; }

	/// The render tiles the land pass paints from (RenderTiles.h, SurfaceField.h): the
	/// chunk square plus kRenderApronTiles on every side, by world tile. The apron holds
	/// the neighbors' own tiles, so a point of the square reads the same field from this
	/// chunk as from any other.
	[[nodiscard]] RenderTileView renderTiles() const { return chunkRenderTiles(m_coord, m_renderData); }

	/// The render tiles as a raw kRenderTilesSide^2 array, row-major, uploaded as the
	/// chunk's tile-data texture by ChunkRenderer.
	[[nodiscard]] const TileRenderData* renderData() const { return m_renderData.data(); }

	/// The ground's macro texture (GroundMacro.h), kGroundMacroBytes of RGBA8, uploaded
	/// beside the render tiles under renderDataVersion().
	[[nodiscard]] const uint8_t* groundMacro() const { return m_groundMacro.data(); }

	/// Version counter for render data and the macro texture, bumped by generate(). GPU
	/// caches compare it to detect stale uploads.
	[[nodiscard]] uint32_t renderDataVersion() const { return m_renderDataVersion.load(std::memory_order_acquire); }

	/// Get the chunk's terrain polygon rings (D2): waterline, channel, and pond,
	/// built by TerrainPolygonBuilder in generate().
	[[nodiscard]] const ChunkTerrainPolygons& terrainPolygons() const { return *m_terrainPolygons; }

	/// The same rings, held for a reader that may outlive the chunk (a placement task).
	[[nodiscard]] std::shared_ptr<const ChunkTerrainPolygons> sharedTerrainPolygons() const { return m_terrainPolygons; }

	/// The distance-field textures baked from terrainPolygons() (D10), carrying
	/// the same version.
	[[nodiscard]] const TerrainDistanceField& terrainDistanceField() const { return m_terrainDistanceField; }

  private:
	ChunkCoordinate m_coord;
	ChunkSampleResult m_biomeData;
	uint64_t m_worldSeed;
	mutable std::chrono::steady_clock::time_point m_lastAccessed;

	/// Flat array of pre-computed tiles (512×512 = 262,144 tiles × 8 bytes = 2.0 MB)
	std::array<TileData, kChunkSize * kChunkSize> m_tiles;

	/// Render tiles over the chunk square plus its render apron (518 x 518 x 2 bytes)
	std::array<TileRenderData, static_cast<size_t>(kRenderTilesSide) * kRenderTilesSide> m_renderData;

	/// The ground's macro texture (257 x 257 x 4 bytes)
	std::array<uint8_t, kGroundMacroBytes> m_groundMacro{};

	/// Thread-safe flag indicating generation is complete
	std::atomic<bool> m_generationComplete{false};

	/// Bumped whenever m_renderData changes (generation)
	std::atomic<uint32_t> m_renderDataVersion{0};

	/// Terrain polygon rings (waterline/channel/pond, D2) and their distance
	/// field (D10). Installed together by generate() via setTerrainPolygons().
	std::shared_ptr<const ChunkTerrainPolygons> m_terrainPolygons = std::make_shared<const ChunkTerrainPolygons>();
	TerrainDistanceField						m_terrainDistanceField;

	/// Install a freshly built polygon set, bump its version (monotonic per
	/// chunk, like m_renderDataVersion), and bake its distance field at that
	/// version. Called from generate(), before m_generationComplete's release
	/// store, so any reader gated on isReady() sees the version that matches the
	/// rings and textures it reads.
	void setTerrainPolygons(ChunkTerrainPolygons polygons);

	/// Fill the render tiles from the chunk's post-processed tiles plus the apron,
	/// whose tiles `terrain` (the chunk's own polygons) post-processes.
	void computeRenderData(const ExtendedTiles& extended, const TerrainPolygonQuery& terrain);

	/// Select surface type based on biome using organic noise-based patches, for
	/// any local tile of the chunk whose patch fields `patchNoise` holds, not just
	/// this chunk's own. Elevation is passed in (already resolved by the caller)
	/// rather than re-derived from a sample source, so it works the same for a
	/// chunk's own tiles and for an apron tile resolved against a neighbor's
	/// corners (ApronField).
	[[nodiscard]] static Surface selectSurfaceFor(const generation::PatchNoise& patchNoise, Biome biome, uint16_t localX,
	                                               uint16_t localY, float elevationMeters);
};

}  // namespace world
