#pragma once

// WetlandGenerator - Terrain generation for Wetland biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Wetland: Water, with islands of tall marsh grass on 11% of tiles.
class WetlandGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		return ctx.patch(PatchField::MarshIslands) > 0.2317F ? Surface::GrassTall : Surface::Water;
	}
};

} // namespace world::generation
