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
		if (ctx.patch(PatchField::Dirt) > 0.512F) {
			return Surface::Dirt;
		}
		const float moisture = ctx.patch(PatchField::Moisture);
		if (moisture > 0.225F) {
			return Surface::GrassTall;
		}
		if (moisture < -0.1734F) {
			return Surface::GrassShort;
		}
		if (ctx.patch(PatchField::Fertility) > 0.3323F) {
			return Surface::GrassMeadow;
		}
		return Surface::Grass;
	}
};

} // namespace world::generation
