#pragma once

// Chunk - A 512×512 tile region of the world.
// Contains sampled biome data and cached rendering data.
// Tiles are generated procedurally on-demand from the biome data.

#include "world/Biome.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"

#include <graphics/Color.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace engine::world {

/// Ground cover types for rendering
enum class GroundCover : uint8_t {
	Grass,
	Dirt,
	Sand,
	Rock,
	Water,
	Snow
};

/// Tile data generated from biome + seed
struct TileData {
	BiomeWeights biome;
	GroundCover groundCover = GroundCover::Grass;
	float elevation = 0.0F;
	float moisture = 0.5F;
};

/// A 512×512 region of the world.
/// Holds biome data sampled from the 3D world and generates tiles on-demand.
class Chunk {
  public:
	/// Create a chunk with sampled biome data
	Chunk(ChunkCoordinate coord, ChunkSampleResult biomeData, uint64_t worldSeed);

	/// Get the chunk's grid coordinate
	[[nodiscard]] ChunkCoordinate coordinate() const { return m_coord; }

	/// Get the chunk's origin in world space
	[[nodiscard]] WorldPosition worldOrigin() const { return m_coord.origin(); }

	/// Get tile data at local coordinates (0-511, 0-511)
	/// Generates tile procedurally if not cached
	[[nodiscard]] TileData getTile(uint16_t localX, uint16_t localY) const;

	/// Get the biome data for this chunk
	[[nodiscard]] const ChunkSampleResult& biomeData() const { return m_biomeData; }

	/// Check if chunk is pure (single biome)
	[[nodiscard]] bool isPure() const { return m_biomeData.isPure; }

	/// Get primary biome (for pure chunks or dominant biome for boundary chunks)
	[[nodiscard]] Biome primaryBiome() const {
		if (m_biomeData.isPure) {
			return m_biomeData.singleBiome;
		}
		// Return the biome of the center sector
		return m_biomeData.getTileBiome(256, 256).primary();
	}

	/// Get color for a biome (for ground rendering)
	[[nodiscard]] static Foundation::Color getBiomeColor(Biome biome);

	/// Get color for ground cover
	[[nodiscard]] static Foundation::Color getGroundCoverColor(GroundCover cover);

	/// Update last accessed time (for LRU eviction)
	/// Note: touch() is const because m_lastAccessed is mutable - LRU timestamp
	/// is not considered part of the logical state of the chunk.
	void touch() const { m_lastAccessed = std::chrono::steady_clock::now(); }

	/// Get last accessed time
	[[nodiscard]] auto lastAccessed() const { return m_lastAccessed; }

  private:
	ChunkCoordinate m_coord;
	ChunkSampleResult m_biomeData;
	uint64_t m_worldSeed;
	mutable std::chrono::steady_clock::time_point m_lastAccessed;

	/// Generate tile data procedurally
	[[nodiscard]] TileData generateTile(uint16_t localX, uint16_t localY) const;

	/// Select ground cover based on biome using organic noise-based patches
	[[nodiscard]] GroundCover selectGroundCover(Biome biome, uint16_t localX, uint16_t localY) const;

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
