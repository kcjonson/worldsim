#include "world/chunk/SyntheticWorldSampler.h"

#include <algorithm>
#include <numbers>
#include <utility>

namespace world {

	namespace {
		constexpr float kGroundElevationM = 1.0F;
	} // namespace

	SyntheticWorldSampler::SyntheticWorldSampler(uint64_t worldSeed, SyntheticMap syntheticMap)
		: seed(worldSeed),
		  map(std::move(syntheticMap)) {
		if (map.pond.has_value()) {
			constexpr int kRimSamples = 720;
			double		  widestRim = 0.0;
			for (int i = 0; i < kRimSamples; ++i) {
				const double theta = 2.0 * std::numbers::pi * i / kRimSamples;
				widestRim = std::max(widestRim, worldgen::PondNetwork2D::rimRadiusAt(*map.pond, theta));
			}
			// The gather margin every world sampler uses (ChunkSampleResult.h), widened by the pond's
			// footprint; one more meter covers the rim between samples.
			pondReachM = kRiverGatherMarginM + widestRim + 1.0;
		}
	}

	ChunkSampleResult SyntheticWorldSampler::sampleChunk(ChunkCoordinate coord) const {
		ChunkSampleResult result = makeUniformChunkSampleResult(BiomeWeights::single(map.biome), kGroundElevationM);
		if (map.pond.has_value()) {
			const WorldPosition origin = coord.origin();
			const double		nearestX =
				std::clamp(map.pond->cx, static_cast<double>(origin.x), static_cast<double>(origin.x + kChunkWorldSize));
			const double nearestY =
				std::clamp(map.pond->cy, static_cast<double>(origin.y), static_cast<double>(origin.y + kChunkWorldSize));
			const double dx = map.pond->cx - nearestX;
			const double dy = map.pond->cy - nearestY;
			if (dx * dx + dy * dy <= pondReachM * pondReachM) {
				result.pondBlobs.push_back(*map.pond);
			}
		}
		return result;
	}

	float SyntheticWorldSampler::sampleElevation(WorldPosition /*pos*/) const {
		return kGroundElevationM;
	}

} // namespace world
