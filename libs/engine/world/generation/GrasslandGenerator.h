#pragma once

// GrasslandGenerator - Terrain generation for Grassland biome
// Features 4 grass variants distributed based on moisture gradient from water.

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"
#include "world/generation/NoiseUtils.h"

namespace engine::world::generation {

/// Grassland surface generator with moisture-based grass variants.
///
/// Surfaces (in priority order):
/// - Water: Ponds (moistureNoise > 0.82)
/// - GrassTall: Wet zones near water (moistureNoise > 0.70)
/// - GrassShort: Dry zones far from water (moistureNoise < 0.35)
/// - GrassMeadow: Fertile patches in mid-zone (fertilityNoise > 0.80)
/// - Grass: Default grassland
/// - Dirt: Sparse exposed soil patches (overlay, ~2-3%)
///
/// Key design: Using the same moisture noise for ponds AND grass variants
/// creates natural terrain flow - tall grass rings ponds, short grass in dry areas.
class GrasslandGenerator {
public:
	[[nodiscard]] static GenerationResult generate(const GenerationContext& ctx) {
		GenerationResult result;
		float			 worldX = ctx.worldX();
		float			 worldY = ctx.worldY();

		// ===== PRIMARY MOISTURE NOISE =====
		// Low frequency for large coherent regions (~8-15 tiles across)
		// This single noise layer drives both ponds AND grass moisture zones
		constexpr float kMoistureScale = 0.08F;
		float			moistureNoise = NoiseUtils::fractalNoise(
            worldX * kMoistureScale,
            worldY * kMoistureScale,
            ctx.worldSeed + 100000,
            2, 0.5F
        );

		// ===== WATER (PONDS) =====
		// Highest moisture = standing water
		constexpr float kWaterThreshold = 0.82F;
		if (moistureNoise > kWaterThreshold) {
			result.surface = Surface::Water;
			result.moisture = 255; // Maximum moisture
			return result;
		}

		// ===== DIRT PATCHES =====
		// Separate high-frequency noise for sparse exposed soil
		// Checked early so dirt can appear in any moisture zone
		constexpr float kDirtScale = 0.18F;
		float			dirtNoise = NoiseUtils::fractalNoise(
            worldX * kDirtScale,
            worldY * kDirtScale,
            ctx.worldSeed + 50000,
            2, 0.5F
        );

		constexpr float kDirtThreshold = 0.90F; // ~2-3% coverage
		if (dirtNoise > kDirtThreshold) {
			result.surface = Surface::Dirt;
			result.moisture = static_cast<uint8_t>(moistureNoise * 200); // Scale moisture
			return result;
		}

		// ===== GRASS VARIANTS BASED ON MOISTURE =====

		// GrassTall: Wet zones (high moisture, near ponds)
		constexpr float kTallGrassThreshold = 0.70F;
		if (moistureNoise > kTallGrassThreshold) {
			result.surface = Surface::GrassTall;
			result.moisture = static_cast<uint8_t>(180 + moistureNoise * 75); // 180-255
			return result;
		}

		// GrassShort: Dry zones (low moisture, far from water)
		constexpr float kShortGrassThreshold = 0.35F;
		if (moistureNoise < kShortGrassThreshold) {
			result.surface = Surface::GrassShort;
			result.moisture = static_cast<uint8_t>(moistureNoise * 180); // 0-62
			return result;
		}

		// ===== MID-MOISTURE ZONE: Meadow or Regular Grass =====

		// Fertility noise for meadow patches (different pattern from moisture)
		constexpr float kFertilityScale = 0.12F;
		float			fertilityNoise = NoiseUtils::fractalNoise(
            worldX * kFertilityScale,
            worldY * kFertilityScale,
            ctx.worldSeed + 300000,
            2, 0.5F
        );

		// GrassMeadow: Fertile patches within the mid-moisture zone
		constexpr float kMeadowThreshold = 0.78F;
		if (fertilityNoise > kMeadowThreshold) {
			result.surface = Surface::GrassMeadow;
			result.moisture = static_cast<uint8_t>(120 + fertilityNoise * 80); // 120-199
			return result;
		}

		// Default: Regular grass
		result.surface = Surface::Grass;
		result.moisture = static_cast<uint8_t>(80 + moistureNoise * 100); // 80-180
		return result;
	}
};

} // namespace engine::world::generation
