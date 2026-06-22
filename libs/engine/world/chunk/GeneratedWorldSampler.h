#pragma once

// GeneratedWorldSampler - IWorldSampler backed by a 3D generated planet.
// Samples biomes and elevations around a landing site via worldgen::PlanetSampler;
// elevations are meters above sea level (the PlanetSampler unit contract).

#include "world/chunk/IWorldSampler.h"

#include <worldgen/data/GeneratedWorld.h>
#include <worldgen/sampling/PlanetSampler.h>
#include <worldgen/sampling/RiverNetwork2D.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace engine::world {

class GeneratedWorldSampler : public IWorldSampler {
  public:
	GeneratedWorldSampler(std::shared_ptr<const worldgen::GeneratedWorld> world,
	                      double landingLatDeg, double landingLonDeg);

	/// Sample biome and elevation data for a chunk from the generated planet
	[[nodiscard]] ChunkSampleResult sampleChunk(ChunkCoordinate coord) const override;

	/// Sample elevation (meters above sea level) at a world position
	[[nodiscard]] float sampleElevation(WorldPosition pos) const override;

	/// Get the world seed (GeneratedWorld::params.seed)
	[[nodiscard]] uint64_t getWorldSeed() const override { return sampler.seed(); }

  private:
	[[nodiscard]] BiomeWeights sampleBiomeAt(WorldPosition pos) const;

	worldgen::PlanetSampler sampler;
	// Present only when the world carries drainage data (FlowAccum + Downhill).
	// Worlds saved before the hydrology epic lack it; rivers are then absent.
	std::optional<worldgen::RiverNetwork2D> riverNetwork;
};

}  // namespace engine::world
