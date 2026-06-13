#include "GeneratedWorldSampler.h"

namespace engine::world {

	GeneratedWorldSampler::GeneratedWorldSampler(std::shared_ptr<const worldgen::GeneratedWorld> world,
	                                             double landingLatDeg, double landingLonDeg)
		: sampler(std::move(world), landingLatDeg, landingLonDeg) {}

	ChunkSampleResult GeneratedWorldSampler::sampleChunk(ChunkCoordinate coord) const {
		ChunkSampleResult result;

		result.cornerBiomes[0] = sampleBiomeAt(coord.corner(ChunkCorner::NorthWest));
		result.cornerBiomes[1] = sampleBiomeAt(coord.corner(ChunkCorner::NorthEast));
		result.cornerBiomes[2] = sampleBiomeAt(coord.corner(ChunkCorner::SouthWest));
		result.cornerBiomes[3] = sampleBiomeAt(coord.corner(ChunkCorner::SouthEast));

		result.cornerElevations[0] = sampleElevation(coord.corner(ChunkCorner::NorthWest));
		result.cornerElevations[1] = sampleElevation(coord.corner(ChunkCorner::NorthEast));
		result.cornerElevations[2] = sampleElevation(coord.corner(ChunkCorner::SouthWest));
		result.cornerElevations[3] = sampleElevation(coord.corner(ChunkCorner::SouthEast));

		result.computeSectorGrid();
		return result;
	}

	float GeneratedWorldSampler::sampleElevation(WorldPosition pos) const {
		return sampler.elevationAt(static_cast<double>(pos.x), static_cast<double>(pos.y));
	}

	BiomeWeights GeneratedWorldSampler::sampleBiomeAt(WorldPosition pos) const {
		auto sample = sampler.sampleAt(static_cast<double>(pos.x), static_cast<double>(pos.y));
		BiomeWeights weights;
		for (uint32_t i = 0; i < sample.weightCount; ++i)
			weights.set(sample.weights[i].biome, sample.weights[i].weight);
		weights.normalize();
		return weights;
	}

} // namespace engine::world
