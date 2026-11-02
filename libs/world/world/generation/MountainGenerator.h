#pragma once

// MountainGenerator - Terrain generation for Mountain biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Mountain: Rock, with Snow patches on 11% of tiles.
class MountainGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		return ctx.patch(PatchField::Outcrop) > 0.2304F ? Surface::Snow : Surface::Rock;
	}
};

} // namespace world::generation
