#pragma once

// NoiseUtils - Stateless noise utilities for terrain generation
// All functions are pure and deterministic (seed-based).
// Extracted from Chunk.cpp for shared use across biome generators.

#include "world/chunk/ChunkCoordinate.h"

#include <cmath>
#include <cstdint>

namespace engine::world::generation {

/// Stateless noise utilities for terrain generation.
/// All functions are static, pure, and deterministic.
class NoiseUtils {
public:
	/// Smoothstep interpolation: 3t² - 2t³ (Hermite curve)
	[[nodiscard]] static float smoothstep(float t) {
		return t * t * (3.0F - 2.0F * t);
	}

	/// Deterministic hash for a tile position.
	/// Uses XOR-folding with prime multipliers for good distribution.
	[[nodiscard]] static uint32_t tileHash(
	    ChunkCoordinate chunk, uint16_t localX, uint16_t localY, uint64_t seed
	) {
		uint64_t h = seed;
		h ^= static_cast<uint64_t>(chunk.x) * 0x9E3779B97F4A7C15ULL;
		h ^= static_cast<uint64_t>(chunk.y) * 0xC6A4A7935BD1E995ULL;
		h ^= static_cast<uint64_t>(localX) * 0x85EBCA6B;
		h ^= static_cast<uint64_t>(localY) * 0xC2B2AE35;
		h ^= h >> 33;
		h *= 0xFF51AFD7ED558CCDULL;
		h ^= h >> 33;
		return static_cast<uint32_t>(h);
	}

	/// Value noise in range [0, 1] for organic patch generation.
	/// Uses bilinear interpolation with smoothstep for smooth transitions.
	[[nodiscard]] static float valueNoise(float x, float y, uint64_t seed) {
		auto	x0 = static_cast<int32_t>(std::floor(x));
		auto	y0 = static_cast<int32_t>(std::floor(y));
		int32_t x1 = x0 + 1;
		int32_t y1 = y0 + 1;

		float fx = x - static_cast<float>(x0);
		float fy = y - static_cast<float>(y0);

		float sx = smoothstep(fx);
		float sy = smoothstep(fy);

		constexpr float kNormalize = 1.0F / static_cast<float>(UINT32_MAX);
		float			n00 = static_cast<float>(tileHash({x0, y0}, 0, 0, seed)) * kNormalize;
		float			n10 = static_cast<float>(tileHash({x1, y0}, 0, 0, seed)) * kNormalize;
		float			n01 = static_cast<float>(tileHash({x0, y1}, 0, 0, seed)) * kNormalize;
		float			n11 = static_cast<float>(tileHash({x1, y1}, 0, 0, seed)) * kNormalize;

		float nx0 = n00 * (1.0F - sx) + n10 * sx;
		float nx1 = n01 * (1.0F - sx) + n11 * sx;
		return nx0 * (1.0F - sy) + nx1 * sy;
	}

	/// Fractal noise (fBm) - multiple octaves for natural-looking variation.
	/// @param x World X coordinate (scaled by caller)
	/// @param y World Y coordinate (scaled by caller)
	/// @param seed Random seed
	/// @param octaves Number of noise layers to combine (default: 2)
	/// @param persistence Amplitude multiplier per octave (default: 0.5)
	/// @return Noise value in range [0, 1]
	[[nodiscard]] static float fractalNoise(
	    float x, float y, uint64_t seed, int octaves = 2, float persistence = 0.5F
	) {
		float total = 0.0F;
		float amplitude = 1.0F;
		float frequency = 1.0F;
		float maxValue = 0.0F;

		for (int i = 0; i < octaves; ++i) {
			total += valueNoise(x * frequency, y * frequency, seed + static_cast<uint64_t>(i)) * amplitude;
			maxValue += amplitude;
			amplitude *= persistence;
			frequency *= 2.0F;
		}

		return total / maxValue; // Normalize to [0, 1]
	}
};

} // namespace engine::world::generation
