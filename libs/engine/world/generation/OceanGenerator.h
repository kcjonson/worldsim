#pragma once

// OceanGenerator - Terrain generation for Ocean biome

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"

namespace engine::world::generation {

/// Ocean surface generator.
/// Always returns Water.
class OceanGenerator {
public:
	[[nodiscard]] static GenerationResult generate([[maybe_unused]] const GenerationContext& ctx) {
		GenerationResult result;
		result.surface = Surface::Water;
		result.moisture = 255;
		return result;
	}
};

} // namespace engine::world::generation
