#pragma once

// TundraGenerator - Terrain generation for Tundra biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Tundra: Snow, with rock outcrops on 0.4% of tiles.
class TundraGenerator {
public:
	[[nodiscard]] static Surface generate(const GenerationContext& ctx) {
		constexpr float kRockThreshold = 0.4815F; // the top 0.4% of the outcrop field
		return ctx.patch(PatchField::Outcrop) > kRockThreshold ? Surface::Rock : Surface::Snow;
	}
};

} // namespace world::generation
