#pragma once

// SyntheticWorldSampler - a flat map built from a few parameters, for tests and the headless sim.
// One biome on level ground everywhere, and optionally one pond blob. The pond's shore is the blob's
// rim exactly (no biome blending across chunk corners), so a colonist can stand a known distance from
// the water.

#include "world/Biome.h"
#include "world/chunk/IWorldSampler.h"

#include <worldgen/sampling/PondNetwork2D.h>

#include <cstdint>
#include <optional>

namespace world {

	struct SyntheticMap {
		Biome biome = Biome::TemperateGrassland;
		/// No pond: no water anywhere.
		std::optional<worldgen::PondNetwork2D::Pond> pond;
	};

	class SyntheticWorldSampler : public IWorldSampler {
	  public:
		SyntheticWorldSampler(uint64_t worldSeed, SyntheticMap syntheticMap);

		[[nodiscard]] ChunkSampleResult sampleChunk(ChunkCoordinate coord) const override;
		[[nodiscard]] float				sampleElevation(WorldPosition pos) const override;
		[[nodiscard]] uint64_t			getWorldSeed() const override { return seed; }

	  private:
		uint64_t	 seed;
		SyntheticMap map;
		/// How far from the pond's center a chunk can be and still draw some of it: the rim's widest
		/// reach plus the apron a chunk generates beyond its square.
		double pondReachM = 0.0;
	};

} // namespace world
