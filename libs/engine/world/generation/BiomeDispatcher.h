#pragma once

// BiomeDispatcher - Routes generation to appropriate biome generator
// Uses static dispatch (switch) for zero runtime overhead.

#include "world/generation/GenerationContext.h"
#include "world/generation/GenerationResult.h"

#include "world/generation/BeachGenerator.h"
#include "world/generation/DesertGenerator.h"
#include "world/generation/ForestGenerator.h"
#include "world/generation/GrasslandGenerator.h"
#include "world/generation/MountainGenerator.h"
#include "world/generation/OceanGenerator.h"
#include "world/generation/TundraGenerator.h"
#include "world/generation/WetlandGenerator.h"

namespace engine::world::generation {

/// Dispatches to the appropriate biome generator based on biome type.
/// Uses static dispatch (switch) for zero overhead - equivalent to direct function call.
class BiomeDispatcher {
public:
	/// Generate terrain for a tile based on its biome.
	/// @param ctx Generation context with all input data
	/// @return GenerationResult with surface type and optional metadata
	[[nodiscard]] static GenerationResult generate(const GenerationContext& ctx) {
		switch (ctx.biome) {
			case Biome::Grassland:
				return GrasslandGenerator::generate(ctx);
			case Biome::Forest:
				return ForestGenerator::generate(ctx);
			case Biome::Desert:
				return DesertGenerator::generate(ctx);
			case Biome::Tundra:
				return TundraGenerator::generate(ctx);
			case Biome::Wetland:
				return WetlandGenerator::generate(ctx);
			case Biome::Mountain:
				return MountainGenerator::generate(ctx);
			case Biome::Beach:
				return BeachGenerator::generate(ctx);
			case Biome::Ocean:
				return OceanGenerator::generate(ctx);
			default:
				// Unknown biome - fallback to grassland
				return GrasslandGenerator::generate(ctx);
		}
	}
};

} // namespace engine::world::generation
