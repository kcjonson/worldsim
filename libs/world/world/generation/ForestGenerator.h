#pragma once

// ForestGenerator - Terrain generation for Forest biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Forest floor: mostly Grass, GrassTall where the grassland's moisture field is wet (9%),
/// and sparse Dirt (0.4%). Standing water comes from the 3D hydrology, not here.
class ForestGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		constexpr float kDirtThreshold = 0.4815F;	   // the top 0.4% of the outcrop field
		constexpr float kTallGrassThreshold = 0.2506F; // the top 9% of the moisture field
		if (ctx.patch(PatchField::Outcrop) > kDirtThreshold) {
			return Surface::Dirt;
		}
		if (ctx.patch(PatchField::Moisture) > kTallGrassThreshold) {
			return Surface::GrassTall;
		}
		return Surface::Grass;
	}
};

} // namespace world::generation
