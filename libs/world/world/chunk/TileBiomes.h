#pragma once

// The biome of a world tile (terrain-polygons-architecture.md, "Biome edges"). A chunk samples the
// planet at its four corners, 512 m apart, so their weights interpolated alone draw every biome
// boundary as a near-straight line kinked at chunk borders. Each biome's interpolated weight is raised
// by its own world-space gradient fBm (200, 50 and 12 m) and the largest wins, which bends a boundary
// into bays and headlands. Where two land biomes still nearly tie, a finer noise picks between them,
// leaving islands of each in the other. A boundary with water keeps the coarse line, so the waterline
// (D5), which reads the same biome, and the tiles under it agree. The pick is a pure function of the
// owning chunk's corners, the tile and the world seed (D14): every chunk that computes a tile, as its
// own or in its apron, computes the same biome.

#include "world/Biome.h"
#include "world/BiomeWeights.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/NoiseLattice.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace world {

	/// TileData's biome bytes.
	struct TileBiome {
		Biome	primary = Biome::TemperateGrassland;
		Biome	secondary = Biome::TemperateGrassland; ///< the runner-up, or primary where the chunk holds one biome
		uint8_t blend = 255;						   ///< primary's interpolated weight, 0..255
	};

	/// The biome of every tile of one chunk, from the chunk's corner samples.
	class ChunkBiomes {
	  public:
		/// `corners` NW, NE, SW, SE, as ChunkSampleResult::cornerBiomes holds them.
		ChunkBiomes(ChunkCoordinate coord, const std::array<BiomeWeights, 4>& corners, uint64_t worldSeed);

		[[nodiscard]] TileBiome at(uint16_t localX, uint16_t localY) const;

		/// Whether any tile can be biome water (D5): some corner weighs a water biome.
		[[nodiscard]] bool anyWater() const { return hasWater; }

	  private:
		/// A biome some corner weighs, with its weight at each corner.
		struct Candidate {
			Biome				 biome = Biome::TemperateGrassland;
			bool				 water = false;
			std::array<float, 4> corner{}; ///< NW, NE, SW, SE
		};

		static constexpr size_t kMaxCandidates = BiomeWeights::kMaxEntries * 4;

		int64_t								  originX = 0;
		int64_t								  originY = 0;
		std::array<Candidate, kMaxCandidates> candidates{};
		size_t								  count = 0;
		bool								  hasWater = false;
		std::vector<float> coarseNoise; ///< per candidate, its coarse octaves summed at every 8th tile; empty for a one-biome chunk
		std::vector<NoiseOctaveGrid>   fineNoise;	 ///< per candidate, its 12 m octave
		std::optional<NoiseOctaveGrid> ecotoneNoise; ///< where two land biomes can meet
	};

} // namespace world
