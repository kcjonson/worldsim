#pragma once

// WetlandGenerator - Terrain generation for Wetland biome

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"
#include "world/generation/NoiseUtils.h"

namespace engine::world::generation {

/// Wetland surface generator.
/// Primary: Water, Variation: Grass patches (islands)
class WetlandGenerator {
public:
	[[nodiscard]] static GenerationResult generate(const GenerationContext& ctx) {
		GenerationResult result;
		float			 worldX = ctx.worldX();
		float			 worldY = ctx.worldY();

		constexpr float kPatchScale = 0.12F;
		float			variationNoise = NoiseUtils::fractalNoise(
            worldX * kPatchScale, worldY * kPatchScale,
            ctx.worldSeed + 50000, 2, 0.5F
        );

		// Wetland is mostly water with grass islands
		constexpr float kGrassThreshold = 0.70F;
		if (variationNoise > kGrassThreshold) {
			result.surface = Surface::GrassTall; // Tall marsh grass
		} else {
			result.surface = Surface::Water;
		}
		result.moisture = 240; // Very wet

		return result;
	}
};

} // namespace engine::world::generation
