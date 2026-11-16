#pragma once

// DesertGenerator - Terrain generation for Desert biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Desert: Sand, with rock outcrops on 0.9% of tiles.
class DesertGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		constexpr float kRockThreshold = 0.4371F; // the top 0.9% of the outcrop field
		return ctx.patch(PatchField::Outcrop) > kRockThreshold ? Surface::Rock : Surface::Sand;
	}
};

} // namespace world::generation
