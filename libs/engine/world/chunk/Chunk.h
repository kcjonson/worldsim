#pragma once

// Chunk - A 512×512 tile region of the world.
// Contains sampled biome data and cached rendering data.
// Tiles are generated procedurally on-demand from the biome data.

#include "world/Biome.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"

#include <graphics/Color.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

namespace engine::world {

/// Surface types for terrain rendering
/// Ground family surfaces use soft blending; Water/Rock use hard edges.
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

/// Tile data - 16 bytes, stored in flat array per chunk.
/// Designed for single source of truth: computed once, read by all systems.
struct TileData {
	Surface surface = Surface::Grass;   ///< 1 byte - THE definitive terrain type
	Biome primaryBiome = Biome::Grassland;   ///< 1 byte - dominant biome
	Biome secondaryBiome = Biome::Grassland; ///< 1 byte - for ecotones (may equal primary)
	uint8_t biomeBlend = 255;           ///< 1 byte - weight of primary (255 = 100% primary)
	uint16_t elevation = 0;             ///< 2 bytes - centimeters above sea level
	uint8_t moisture = 128;             ///< 1 byte - normalized 0-255
	uint8_t attributes = 0;             ///< 1 byte - reserved for future non-adjacency flags
	uint64_t adjacency = 0;             ///< 8 bytes - neighbor surface types (8 dirs × 6 bits)

	/// Get biome weights as BiomeWeights (for compatibility during migration)
	[[nodiscard]] BiomeWeights biome() const {
		if (biomeBlend == 255 || primaryBiome == secondaryBiome) {
			return BiomeWeights::single(primaryBiome);
		}
		BiomeWeights bw;
		float primaryWeight = static_cast<float>(biomeBlend) / 255.0F;
		bw.set(primaryBiome, primaryWeight);
		bw.set(secondaryBiome, 1.0F - primaryWeight);
		return bw;
	}
};

/// Pre-computed tile rendering data - 16 bytes per tile.
/// Cached during chunk generation to avoid per-frame adjacency extraction.
/// Used by ChunkRenderer for fast tile rendering.
struct TileRenderData {
	uint8_t surfaceId;     ///< Surface type (0-255)
	uint8_t edgeMask;      ///< Edge shadow mask (N,E,S,W bits)
	uint8_t cornerMask;    ///< Corner shadow mask (NW,NE,SE,SW bits)
	uint8_t hardEdgeMask;  ///< Family-based hard edges (8 directions)
	uint8_t neighborN;     ///< North neighbor surface ID
	uint8_t neighborE;     ///< East neighbor surface ID
	uint8_t neighborS;     ///< South neighbor surface ID
	uint8_t neighborW;     ///< West neighbor surface ID
	uint8_t neighborNW;    ///< Northwest neighbor surface ID
	uint8_t neighborNE;    ///< Northeast neighbor surface ID
	uint8_t neighborSE;    ///< Southeast neighbor surface ID
	uint8_t neighborSW;    ///< Southwest neighbor surface ID
	uint8_t padding[4];    ///< Pad to 16 bytes for cache alignment
};

/// A 512×512 region of the world.
/// Tiles are pre-computed during generate() and stored in a flat array.
/// All systems read from the same definitive tile data.
class Chunk {
  public:
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

	/// Update adjacency for a single tile (used when neighbor chunks arrive)
	void setAdjacency(uint16_t localX, uint16_t localY, uint64_t adjacency);

	/// Get the biome data for this chunk (used during generation)
	[[nodiscard]] const ChunkSampleResult& biomeData() const { return m_biomeData; }

	/// Get primary biome (dominant biome at chunk center)
	[[nodiscard]] Biome primaryBiome() const {
		// Return the biome of the center tile
		return m_tiles[256 * kChunkSize + 256].primaryBiome;
	}

	/// Get color for a biome (for ground rendering)
	[[nodiscard]] static Foundation::Color getBiomeColor(Biome biome);

	/// Get color for surface type
	[[nodiscard]] static Foundation::Color getSurfaceColor(Surface surface);

	/// Update last accessed time (for LRU eviction)
	/// Note: touch() is const because m_lastAccessed is mutable - LRU timestamp
	/// is not considered part of the logical state of the chunk.
	void touch() const { m_lastAccessed = std::chrono::steady_clock::now(); }

	/// Get last accessed time
	[[nodiscard]] auto lastAccessed() const { return m_lastAccessed; }

	/// Get cached shore tile positions (land tiles adjacent to water)
	/// Positions are local chunk coordinates (0-511)
	/// Pre-computed during generation for O(1) lookup by VisionSystem
	[[nodiscard]] const std::vector<std::pair<uint16_t, uint16_t>>& getShoreTiles() const { return m_shoreTiles; }

	/// Get pre-computed tile rendering data for fast rendering
	/// Use instead of extracting adjacency data per-frame
	[[nodiscard]] const TileRenderData& getTileRenderData(uint16_t localX, uint16_t localY) const {
		return m_renderData[localY * kChunkSize + localX];
	}

  private:
	ChunkCoordinate m_coord;
	ChunkSampleResult m_biomeData;
	uint64_t m_worldSeed;
	mutable std::chrono::steady_clock::time_point m_lastAccessed;

	/// Flat array of pre-computed tiles (512×512 = 262,144 tiles × 16 bytes = 4.0 MB)
	std::array<TileData, kChunkSize * kChunkSize> m_tiles;

	/// Pre-computed rendering data (512×512 = 262,144 tiles × 16 bytes = 4.0 MB)
	/// Caches adjacency extraction for ChunkRenderer to avoid per-frame computation
	std::array<TileRenderData, kChunkSize * kChunkSize> m_renderData;

	/// Thread-safe flag indicating generation is complete
	std::atomic<bool> m_generationComplete{false};

	/// Cached shore tile positions (land tiles adjacent to water)
	/// Computed during generation, used by VisionSystem for fast shore discovery
	std::vector<std::pair<uint16_t, uint16_t>> m_shoreTiles;

	/// Compute tile data for a single tile during generation
	[[nodiscard]] TileData computeTile(uint16_t localX, uint16_t localY) const;

	/// Pre-compute shore tiles (land adjacent to water) for VisionSystem
	void computeShoreTiles();

	/// Pre-compute rendering data (adjacency masks, neighbors) for ChunkRenderer
	void computeRenderData();

	/// Select surface type based on biome using organic noise-based patches
	[[nodiscard]] Surface selectSurface(Biome biome, uint16_t localX, uint16_t localY) const;

	/// Hash function for deterministic tile generation
	[[nodiscard]] static uint32_t tileHash(ChunkCoordinate chunk, uint16_t localX, uint16_t localY, uint64_t seed);

	/// Value noise in range [0, 1] for organic patch generation
	[[nodiscard]] float valueNoise(float x, float y, uint64_t seed) const;

	/// Fractal noise (multiple octaves) for natural-looking variation
	[[nodiscard]] float fractalNoise(float x, float y, uint64_t seed, int octaves, float persistence) const;

	/// Smoothstep interpolation for noise
	[[nodiscard]] static float smoothstep(float t);
};

}  // namespace engine::world
