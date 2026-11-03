#pragma once

// OceanGenerator - Terrain generation for Ocean biome

#include "world/chunk/Chunk.h"
#include "world/generation/GenerationContext.h"

namespace world::generation {

/// Ocean: always Water.
class OceanGenerator {
public:
	[[nodiscard]] static Surface generate([[maybe_unused]] const GenerationContext& ctx) {
		return Surface::Water;
	}
};

} // namespace world::generation
