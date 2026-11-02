#pragma once

// GrasslandGenerator - Terrain generation for Grassland biome
// Features 4 grass variants distributed by a moisture field.

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Grassland surface generator with moisture-based grass variants.
///
/// Surfaces, in priority order, with their share of grassland tiles:
/// - Dirt: sparse exposed soil (0.2%)
/// - GrassTall: wet zones (12%)
/// - GrassShort: dry zones (18%)
/// - GrassMeadow: fertile patches in the mid zone (2.6%)
/// - Grass: the rest (67%)
///
/// Standing water is NOT produced here: it comes from the 3D hydrology
/// (RiverNetwork2D channels and PondNetwork2D ponds/oases).
class GrasslandGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		constexpr float kDirtThreshold = 0.512F;		 // the top 0.2% of the dirt field
		constexpr float kTallGrassThreshold = 0.225F;	 // the wettest 12% of the moisture field
		constexpr float kShortGrassThreshold = -0.1734F; // the driest 18%
		// Tested only in the mid moisture zone, so set for meadow on 2.6% of all grassland
		// tiles, not on a share of the fertility field.
		constexpr float kMeadowThreshold = 0.3323F;
		if (ctx.patch(PatchField::Dirt) > kDirtThreshold) {
			return Surface::Dirt;
		}
		const float moisture = ctx.patch(PatchField::Moisture);
		if (moisture > kTallGrassThreshold) {
			return Surface::GrassTall;
		}
		if (moisture < kShortGrassThreshold) {
			return Surface::GrassShort;
		}
		if (ctx.patch(PatchField::Fertility) > kMeadowThreshold) {
			return Surface::GrassMeadow;
		}
		return Surface::Grass;
	}
};

} // namespace world::generation
