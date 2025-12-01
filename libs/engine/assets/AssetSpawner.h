#pragma once

// AssetSpawner - Spawns asset instances on tiles using placement rules.
// Handles biome matching, spawn chance, distribution patterns (uniform/clumped).
// Produces SpawnedInstance data ready for batching and rendering.

#include "assets/AssetDefinition.h"
#include "world/Biome.h"
#include "world/TileGrid.h"

#include <graphics/Color.h>
#include <math/Types.h>

#include <cstdint>
#include <random>
#include <vector>

namespace engine::assets {

/// A single spawned asset instance with transform and color
struct SpawnedInstance {
	Foundation::Vec2  position;
	float			  rotation = 0.0F;
	float			  scale = 1.0F;
	Foundation::Color colorTint;
};

/// Configuration for asset spawning
struct SpawnConfig {
	uint32_t seed = 42;				 // Random seed for reproducibility
	float	 colorVariation = 0.08F; // Random color offset range
};

/// Spawns asset instances on tiles according to placement rules.
class AssetSpawner {
  public:
	/// Spawn instances for an asset definition on a tile grid.
	/// Returns instances for tiles matching the asset's biome requirements.
	[[nodiscard]] static std::vector<SpawnedInstance>
	spawn(const world::TileGrid& grid, const AssetDefinition& def, const SpawnConfig& config = {});

  private:
	/// Find placement config for a tile's primary biome
	[[nodiscard]] static const BiomePlacement*
	findBiomePlacement(const PlacementParams& placement, world::Biome biome);

	/// Spawn using clumped distribution
	static void spawnClumped(
		const BiomePlacement&		   bp,
		const world::Tile&			   tile,
		std::mt19937&				   rng,
		float						   colorVariation,
		std::vector<SpawnedInstance>&  out
	);

	/// Spawn using uniform distribution
	static void spawnUniform(
		const BiomePlacement&		   bp,
		const world::Tile&			   tile,
		std::mt19937&				   rng,
		float						   colorVariation,
		std::vector<SpawnedInstance>&  out
	);
};

}  // namespace engine::assets
