// AssetSpawner Implementation

#include "assets/AssetSpawner.h"

#include "world/Biome.h"

#include <random>

namespace engine::assets {

std::vector<SpawnedInstance>
AssetSpawner::spawn(const world::TileGrid& grid, const AssetDefinition& def, const SpawnConfig& config) {
	std::vector<SpawnedInstance> instances;

	// Seed RNG for reproducibility
	std::mt19937						   rng(config.seed);
	std::uniform_real_distribution<float>  spawnDist(0.0F, 1.0F);

	for (const auto& tile : grid.tiles()) {
		// Find placement config for this tile's primary biome
		const BiomePlacement* bp = findBiomePlacement(def.placement, tile.primaryBiome());
		if (bp == nullptr) {
			// Asset doesn't spawn in this biome
			continue;
		}

		// Check spawn chance
		if (spawnDist(rng) > bp->spawnChance) {
			continue;
		}

		// Spawn based on distribution type
		switch (bp->distribution) {
			case Distribution::Clumped:
				spawnClumped(*bp, tile, rng, config.colorVariation, instances);
				break;
			case Distribution::Uniform:
				spawnUniform(*bp, tile, rng, config.colorVariation, instances);
				break;
			case Distribution::Spaced:
				// TODO: Implement spaced distribution
				spawnUniform(*bp, tile, rng, config.colorVariation, instances);
				break;
		}
	}

	return instances;
}

const BiomePlacement* AssetSpawner::findBiomePlacement(const PlacementParams& placement, world::Biome biome) {
	const std::string biomeName = world::biomeToString(biome);
	return placement.findBiome(biomeName);
}

void AssetSpawner::spawnClumped(
	const BiomePlacement&		  bp,
	const world::Tile&			  tile,
	std::mt19937&				  rng,
	float						  colorVariation,
	std::vector<SpawnedInstance>& out
) {
	// RNG distributions
	std::uniform_real_distribution<float>  rotationDist(-0.3F, 0.3F);
	std::uniform_real_distribution<float>  scaleDist(0.8F, 1.5F);
	std::uniform_real_distribution<float>  colorDist(-colorVariation, colorVariation);

	// Clump size and radius from placement params
	std::uniform_int_distribution<int32_t> clumpSizeDist(bp.clumping.clumpSizeMin, bp.clumping.clumpSizeMax);
	std::uniform_real_distribution<float>  clumpRadiusDist(
		 bp.clumping.clumpRadiusMin * tile.width,
		 bp.clumping.clumpRadiusMax * tile.width
	 );

	// Position within tile for clump center
	std::uniform_real_distribution<float> tileXDist(tile.worldPos.x, tile.worldPos.x + tile.width);
	std::uniform_real_distribution<float> tileYDist(tile.worldPos.y, tile.worldPos.y + tile.height);

	// Generate clump
	Foundation::Vec2 clumpCenter(tileXDist(rng), tileYDist(rng));
	int32_t			 clumpSize = clumpSizeDist(rng);
	float			 clumpRadius = clumpRadiusDist(rng);

	std::uniform_real_distribution<float> clumpOffsetDist(-clumpRadius, clumpRadius);

	for (int32_t i = 0; i < clumpSize; ++i) {
		SpawnedInstance inst;
		inst.position = {
			clumpCenter.x + clumpOffsetDist(rng),
			clumpCenter.y + clumpOffsetDist(rng)
		};
		inst.rotation = rotationDist(rng);
		inst.scale = scaleDist(rng);

		// Green color with variation
		float greenVar = colorDist(rng);
		inst.colorTint = Foundation::Color(
			0.15F + greenVar,
			0.35F + greenVar * 2.0F,
			0.1F + greenVar * 0.5F,
			1.0F
		);

		out.push_back(inst);
	}
}

void AssetSpawner::spawnUniform(
	const BiomePlacement&		  bp,
	const world::Tile&			  tile,
	std::mt19937&				  rng,
	float						  colorVariation,
	std::vector<SpawnedInstance>& out
) {
	// RNG distributions
	std::uniform_real_distribution<float> rotationDist(-0.3F, 0.3F);
	std::uniform_real_distribution<float> scaleDist(0.8F, 1.5F);
	std::uniform_real_distribution<float> colorDist(-colorVariation, colorVariation);

	// Position within tile
	std::uniform_real_distribution<float> tileXDist(tile.worldPos.x, tile.worldPos.x + tile.width);
	std::uniform_real_distribution<float> tileYDist(tile.worldPos.y, tile.worldPos.y + tile.height);

	SpawnedInstance inst;
	inst.position = {tileXDist(rng), tileYDist(rng)};
	inst.rotation = rotationDist(rng);
	inst.scale = scaleDist(rng);

	// Green color with variation
	float greenVar = colorDist(rng);
	inst.colorTint = Foundation::Color(
		0.15F + greenVar,
		0.35F + greenVar * 2.0F,
		0.1F + greenVar * 0.5F,
		1.0F
	);

	out.push_back(inst);
}

}  // namespace engine::assets
