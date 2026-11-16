#pragma once

// MountainGenerator - Terrain generation for Mountain biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Mountain: Rock, with Snow patches on 11% of tiles.
class MountainGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		constexpr float kSnowThreshold = 0.2304F; // the top 11% of the outcrop field
		return ctx.patch(PatchField::Outcrop) > kSnowThreshold ? Surface::Snow : Surface::Rock;
	}
};

} // namespace world::generation
