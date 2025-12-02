#pragma once

// MockWorldSampler - Simple noise-based world sampler for development.
// Uses value noise to generate biome regions for testing chunk rendering.
// Will be replaced by SphericalWorldSampler for the real game.

#include "world/chunk/IWorldSampler.h"

#include <cmath>
#include <cstdint>

namespace engine::world {

/// Mock world sampler using value noise for biome generation.
/// Creates a simple but varied world for testing chunk rendering and streaming.
class MockWorldSampler : public IWorldSampler {
  public:
	/// Create a mock world with the given seed
	explicit MockWorldSampler(uint64_t seed) : m_seed(seed) {}

	/// Sample biome and elevation data for a chunk
	[[nodiscard]] ChunkSampleResult sampleChunk(ChunkCoordinate coord) const override;

	/// Sample elevation at a world position
	[[nodiscard]] float sampleElevation(WorldPosition pos) const override;

	/// Get the world seed
	[[nodiscard]] uint64_t getWorldSeed() const override { return m_seed; }

  private:
	uint64_t m_seed;

	/// Sample biome weights at a world position
	[[nodiscard]] BiomeWeights sampleBiomeAt(WorldPosition pos) const;

	/// Check if a chunk is "pure" (all corners have the same primary biome)
	[[nodiscard]] static bool isChunkPure(const BiomeWeights& nw, const BiomeWeights& ne, const BiomeWeights& sw,
										  const BiomeWeights& se);

	/// Hash function for deterministic noise
	[[nodiscard]] static uint32_t hash(int32_t x, int32_t y, uint64_t seed);

	/// Value noise in range [0, 1]
	[[nodiscard]] float valueNoise(float x, float y, uint64_t seed) const;

	/// Fractal noise (multiple octaves) in range [0, 1]
	[[nodiscard]] float fractalNoise(float x, float y, uint64_t seed, int octaves, float persistence) const;

	/// Smoothstep interpolation
	[[nodiscard]] static float smoothstep(float t);
};

}  // namespace engine::world
