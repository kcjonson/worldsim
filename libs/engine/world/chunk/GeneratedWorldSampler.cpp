#include "GeneratedWorldSampler.h"

namespace engine::world {

	GeneratedWorldSampler::GeneratedWorldSampler(std::shared_ptr<const worldgen::GeneratedWorld> world,
	                                             double landingLatDeg, double landingLonDeg)
		: sampler(world, landingLatDeg, landingLonDeg) {
		// Build the river network only when the world carries drainage data, so
		// older saves (pre-hydrology) degrade to no rivers rather than asserting.
		// Must match RiverNetwork2D's constructor contract (it reads elevation and
		// flags too), or this guard would let through a world that then asserts.
		constexpr uint32_t kRiverFields =
			static_cast<uint32_t>(worldgen::WorldField::Elevation) |
			static_cast<uint32_t>(worldgen::WorldField::Flags) |
			static_cast<uint32_t>(worldgen::WorldField::FlowAccum) |
			static_cast<uint32_t>(worldgen::WorldField::Downhill);
		if ((world->validFields & kRiverFields) == kRiverFields) {
			riverNetwork.emplace(world, landingLatDeg, landingLonDeg);
		}

		// Ponds additionally need precipitation + biome to weight placement.
		constexpr uint32_t kPondFields =
			kRiverFields |
			static_cast<uint32_t>(worldgen::WorldField::Precipitation) |
			static_cast<uint32_t>(worldgen::WorldField::Biome);
		if ((world->validFields & kPondFields) == kPondFields) {
			pondNetwork.emplace(world, landingLatDeg, landingLonDeg);
		}
	}

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

		fillNeighborhoodCorners(result, coord,
		                         [this](WorldPosition p) { return sampleBiomeAt(p); },
		                         [this](WorldPosition p) { return sampleElevation(p); });

		// Gather any river channels and ponds touching this chunk, expanded by the
		// apron so the terrain-polygon builder's apron tiles (up to kApronTiles from
		// the border) see every channel/pond a neighbor chunk would (D4 seam rule).
		if (riverNetwork || pondNetwork) {
			const WorldPosition origin = coord.origin();
			const double apronMeters = static_cast<double>(kApronTiles) * static_cast<double>(kTileSize);
			const double minX = static_cast<double>(origin.x) - apronMeters;
			const double minY = static_cast<double>(origin.y) - apronMeters;
			const double maxX = minX + static_cast<double>(kChunkSize) * static_cast<double>(kTileSize) + 2.0 * apronMeters;
			const double maxY = minY + static_cast<double>(kChunkSize) * static_cast<double>(kTileSize) + 2.0 * apronMeters;
			if (riverNetwork) riverNetwork->gatherSegments(minX, minY, maxX, maxY, result.riverSegments);
			if (pondNetwork) pondNetwork->gatherPonds(minX, minY, maxX, maxY, result.pondBlobs);
		}

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
