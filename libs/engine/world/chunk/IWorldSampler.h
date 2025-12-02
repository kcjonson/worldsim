#pragma once

// IWorldSampler - Interface for sampling world data from the 3D spherical world.
// This abstraction allows different implementations:
// - MockWorldSampler: Noise-based for development/testing
// - SphericalWorldSampler: Future full 3D world simulation
//
// The chunk system uses this interface to get biome and elevation data
// without knowing about the 3D world implementation.

#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"

#include <cstdint>

namespace engine::world {

/// Interface for sampling world data to generate chunks.
/// Implementations provide biome and elevation data from their world model.
class IWorldSampler {
  public:
	virtual ~IWorldSampler() = default;

	/// Sample biome and elevation data for a chunk.
	/// This is the primary method for chunk generation.
	/// @param coord Chunk grid coordinate
	/// @return ChunkSampleResult with biome weights and elevations
	[[nodiscard]] virtual ChunkSampleResult sampleChunk(ChunkCoordinate coord) const = 0;

	/// Sample elevation at a specific world position.
	/// Used for per-tile elevation queries when needed.
	/// @param pos World position in meters
	/// @return Elevation in meters above sea level
	[[nodiscard]] virtual float sampleElevation(WorldPosition pos) const = 0;

	/// Get the world seed for deterministic generation.
	/// All procedural generation should be based on this seed.
	/// @return World seed value
	[[nodiscard]] virtual uint64_t getWorldSeed() const = 0;

	// Non-copyable, non-movable interface
	IWorldSampler() = default;
	IWorldSampler(const IWorldSampler&) = delete;
	IWorldSampler& operator=(const IWorldSampler&) = delete;
	IWorldSampler(IWorldSampler&&) = delete;
	IWorldSampler& operator=(IWorldSampler&&) = delete;
};

}  // namespace engine::world
