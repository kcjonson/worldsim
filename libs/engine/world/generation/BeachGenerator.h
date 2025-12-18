#pragma once

// BeachGenerator - Terrain generation for Beach biome

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"
#include "world/generation/NoiseUtils.h"

namespace engine::world::generation {

/// Beach surface generator.
/// Primary: Sand, Variation: Rock outcrops
class BeachGenerator {
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

		constexpr float kRockThreshold = 0.88F;
		result.surface = (variationNoise > kRockThreshold) ? Surface::Rock : Surface::Sand;
		result.moisture = 150; // Moderate (sea spray)

		return result;
	}
};

} // namespace engine::world::generation
