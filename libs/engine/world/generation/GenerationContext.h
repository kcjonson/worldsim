#pragma once

// GenerationContext - Input data for biome generators
// Contains all information needed to deterministically generate a tile.

#include "world/Biome.h"
#include "world/chunk/ChunkCoordinate.h" // Provides kChunkSize

#include <cstdint>

namespace engine::world::generation {

/// Context passed to biome generators containing all available input data.
/// Designed for deterministic generation: same context = same output.
struct GenerationContext {
	ChunkCoordinate chunkCoord; ///< Chunk being generated
	uint16_t localX = 0;        ///< Tile X within chunk (0 to kChunkSize-1)
	uint16_t localY = 0;        ///< Tile Y within chunk (0 to kChunkSize-1)
	uint64_t worldSeed = 0;     ///< World seed for determinism
	Biome biome = Biome::Grassland; ///< Primary biome at this tile
	float elevation = 0.0F;     ///< Elevation in meters

	/// Calculate world X position in tile units
	[[nodiscard]] float worldX() const {
		return static_cast<float>(chunkCoord.x * engine::world::kChunkSize + localX);
	}

	/// Calculate world Y position in tile units
	[[nodiscard]] float worldY() const {
		return static_cast<float>(chunkCoord.y * engine::world::kChunkSize + localY);
	}
};

} // namespace engine::world::generation
