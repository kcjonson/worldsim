#pragma once

// BeachGenerator - Terrain generation for Beach biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Beach: Sand, with rock outcrops on 0.4% of tiles.
class BeachGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		constexpr float kRockThreshold = 0.4815F; // the top 0.4% of the outcrop field
		return ctx.patch(PatchField::Outcrop) > kRockThreshold ? Surface::Rock : Surface::Sand;
	}
};

} // namespace world::generation
