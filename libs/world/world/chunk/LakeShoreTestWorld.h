#pragma once

// A lake's shore across one chunk, for the benchmarks that measure what a real shore costs the
// waterline and its distance field. The planet blends two biome cells over 500 m either side of their
// edge (PlanetSampler's blend band); the chunk samples that at its corners and the per-tile pick
// (TileBiomes.h) draws the shore from them, bent by the edge noise into bays and headlands, so the
// rings carry the vertices a generated shore does. A closed lake can't sit inside one chunk: the pick
// interpolates corners 512 m apart, so a shore crosses chunks, as real ones do.

#include "world/BiomeWeights.h"
#include "world/chunk/ChunkCoordinate.h"
#include "world/chunk/ChunkSampleResult.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace world::shore_test {

	inline const ChunkCoordinate kLakeShoreChunk{3, -2};

	/// The planet's weights at `p` across a cell edge through the chunk's center at 0.6 rad: Lake on the
	/// side the edge's normal points to, grassland on the other, 50/50 on the edge and pure 500 m out.
	inline BiomeWeights lakeShoreWeights(WorldPosition p) {
		constexpr double kAngle = 0.6;
		const double	 x0 = (static_cast<double>(kLakeShoreChunk.x) + 0.5) * kChunkWorldSize;
		const double	 y0 = (static_cast<double>(kLakeShoreChunk.y) + 0.5) * kChunkWorldSize;
		const double	 d = (p.x - x0) * std::cos(kAngle) + (p.y - y0) * std::sin(kAngle);
		const auto		 lake = static_cast<float>(0.5 * (1.0 + std::clamp(d / 500.0, -1.0, 1.0)));
		BiomeWeights	 weights;
		weights.set(Biome::Lake, lake);
		weights.set(Biome::TemperateGrassland, 1.0F - lake);
		weights.normalize();
		return weights;
	}

	/// kLakeShoreChunk's sample: its corners and its neighborhood's, land at 10 m and the lake bed at 0.
	inline ChunkSampleResult lakeShoreSample() {
		const auto elevation = [](WorldPosition p) {
			return 10.0F * (1.0F - lakeShoreWeights(p).get(Biome::Lake));
		};
		ChunkSampleResult				 sample;
		const std::array<ChunkCorner, 4> corners = {ChunkCorner::NorthWest, ChunkCorner::NorthEast, ChunkCorner::SouthWest, ChunkCorner::SouthEast};
		for (size_t i = 0; i < corners.size(); ++i) {
			const WorldPosition corner = kLakeShoreChunk.corner(corners[i]);
			sample.cornerBiomes[i] = lakeShoreWeights(corner);
			sample.cornerElevations[i] = elevation(corner);
		}
		fillNeighborhoodCorners(sample, kLakeShoreChunk, lakeShoreWeights, elevation);
		return sample;
	}

} // namespace world::shore_test
