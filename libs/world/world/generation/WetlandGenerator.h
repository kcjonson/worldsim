#pragma once

// WetlandGenerator - Terrain generation for Wetland biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Wetland: Water, with islands of tall marsh grass on 11% of tiles.
class WetlandGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		constexpr float kIslandThreshold = 0.2317F; // the top 11% of the marsh island field
		return ctx.patch(PatchField::MarshIslands) > kIslandThreshold ? Surface::GrassTall : Surface::Water;
	}
};

} // namespace world::generation
