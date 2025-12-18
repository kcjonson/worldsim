#pragma once

// MountainGenerator - Terrain generation for Mountain biome

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"
#include "world/generation/NoiseUtils.h"

namespace engine::world::generation {

/// Mountain surface generator.
/// Primary: Rock, Variation: Snow at high elevations
class MountainGenerator {
public:
	[[nodiscard]] static GenerationResult generate(const GenerationContext& ctx) {
		GenerationResult result;
		float			 worldX = ctx.worldX();
		float			 worldY = ctx.worldY();

		constexpr float kPatchScale = 0.15F;
		float			variationNoise = NoiseUtils::fractalNoise(
            worldX * kPatchScale, worldY * kPatchScale,
            ctx.worldSeed + 50000, 2, 0.5F
        );

		// Mountains have more variation (lower threshold)
		constexpr float kSnowThreshold = 0.70F;
		result.surface = (variationNoise > kSnowThreshold) ? Surface::Snow : Surface::Rock;
		result.moisture = 100;

		return result;
	}
};

} // namespace engine::world::generation
