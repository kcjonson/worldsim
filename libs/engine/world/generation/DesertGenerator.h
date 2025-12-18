#pragma once

// DesertGenerator - Terrain generation for Desert biome

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"
#include "world/generation/NoiseUtils.h"

namespace engine::world::generation {

/// Desert surface generator.
/// Primary: Sand, Variation: Rock outcrops
class DesertGenerator {
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

		// Rock outcrops in desert
		constexpr float kRockThreshold = 0.85F;
		result.surface = (variationNoise > kRockThreshold) ? Surface::Rock : Surface::Sand;
		result.moisture = 25; // Very dry

		return result;
	}
};

} // namespace engine::world::generation
