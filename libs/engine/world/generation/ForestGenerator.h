#pragma once

// ForestGenerator - Terrain generation for Forest biome
// Similar to Grassland but with denser vegetation patterns.

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"
#include "world/generation/NoiseUtils.h"

namespace engine::world::generation {

	/// Forest surface generator.
	/// Uses similar moisture-based logic to Grassland.
	/// TODO: Add forest-specific features (undergrowth patterns, clearings)
	class ForestGenerator {
	  public:
		[[nodiscard]] static GenerationResult generate(const GenerationContext& ctx) {
			GenerationResult result;
			float			 worldX = ctx.worldX();
			float			 worldY = ctx.worldY();

			// Moisture noise shapes the grass variants. Standing water is NOT made
			// here -- it comes from the 3D hydrology (RiverNetwork2D / PondNetwork2D).
			constexpr float kMoistureScale = 0.08F;
			float			moistureNoise =
				NoiseUtils::fractalNoise(worldX * kMoistureScale, worldY * kMoistureScale, ctx.worldSeed + 100000, 2, 0.5F);

			// Dirt patches (forest floor)
			constexpr float kDirtScale = 0.15F;
			float			dirtNoise = NoiseUtils::fractalNoise(worldX * kDirtScale, worldY * kDirtScale, ctx.worldSeed + 50000, 2, 0.5F);
			if (dirtNoise > 0.88F) {
				result.surface = Surface::Dirt;
				result.moisture = static_cast<uint8_t>(moistureNoise * 200);
				return result;
			}

			// Forest uses mostly regular grass with some tall grass in wet areas
			if (moistureNoise > 0.72F) {
				result.surface = Surface::GrassTall;
				result.moisture = static_cast<uint8_t>(180 + moistureNoise * 75);
			} else {
				result.surface = Surface::Grass;
				result.moisture = static_cast<uint8_t>(80 + moistureNoise * 100);
			}

			return result;
		}
	};

} // namespace engine::world::generation
