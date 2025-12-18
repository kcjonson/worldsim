#pragma once

// TundraGenerator - Terrain generation for Tundra biome

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"
#include "world/generation/NoiseUtils.h"

namespace engine::world::generation {

/// Tundra surface generator.
/// Primary: Snow, Variation: Rock outcrops
class TundraGenerator {
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
		result.surface = (variationNoise > kRockThreshold) ? Surface::Rock : Surface::Snow;
		result.moisture = 200; // Frozen moisture

		return result;
	}
};

} // namespace engine::world::generation
