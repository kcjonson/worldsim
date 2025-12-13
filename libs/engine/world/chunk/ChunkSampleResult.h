#pragma once

// ChunkSampleResult - Biome data sampled from the 3D world for a chunk.
// This is the result of sampling the spherical world at chunk corners.
// Used temporarily during Chunk::generate(), then tile data is stored in flat array.

#include "world/Biome.h"
#include "world/BiomeWeights.h"
#include "world/chunk/ChunkCoordinate.h"

#include <array>
#include <cstdint>

namespace engine::world {

/// Size of the sector grid for biome interpolation
inline constexpr int32_t kSectorGridSize = 32;

/// Result of sampling the 3D world for a chunk.
/// Contains biome and elevation data needed to generate tiles.
/// This is temporary data - after Chunk::generate(), tiles are stored in flat array.
struct ChunkSampleResult {
	/// Biome weights at each corner (for interpolation)
	/// Order: NW, NE, SW, SE (matches ChunkCorner enum)
	std::array<BiomeWeights, 4> cornerBiomes{};

	/// Elevation at each corner (meters above sea level)
	/// Used for bilinear interpolation within chunk
	std::array<float, 4> cornerElevations{};

	/// Pre-computed 32×32 sector grid for O(1) tile biome lookup.
	/// Each sector covers 16×16 tiles (512/32 = 16).
	std::array<BiomeWeights, kSectorGridSize * kSectorGridSize> sectorGrid{};

	/// Compute sector grid from corner biomes via bilinear interpolation.
	/// Call this after setting cornerBiomes.
	void computeSectorGrid() {
		for (int32_t sy = 0; sy < kSectorGridSize; ++sy) {
			for (int32_t sx = 0; sx < kSectorGridSize; ++sx) {
				float u = static_cast<float>(sx) / static_cast<float>(kSectorGridSize - 1);
				float v = static_cast<float>(sy) / static_cast<float>(kSectorGridSize - 1);
				sectorGrid[static_cast<size_t>(sy * kSectorGridSize + sx)] = bilinearInterpolate(u, v);
			}
		}
	}

	/// Get biome weights for a tile at local coordinates (0-511, 0-511).
	[[nodiscard]] BiomeWeights getTileBiome(uint16_t localX, uint16_t localY) const {
		// Map tile coordinate to sector (16 tiles per sector)
		int32_t sectorX = localX / 16;
		int32_t sectorY = localY / 16;
		sectorX = std::min(sectorX, kSectorGridSize - 1);
		sectorY = std::min(sectorY, kSectorGridSize - 1);
		return sectorGrid[static_cast<size_t>(sectorY * kSectorGridSize + sectorX)];
	}

	/// Get interpolated elevation at a tile position (0-511, 0-511).
	/// Uses bilinear interpolation from corner elevations.
	[[nodiscard]] float getTileElevation(uint16_t localX, uint16_t localY) const {
		float u = static_cast<float>(localX) / static_cast<float>(kChunkSize - 1);
		float v = static_cast<float>(localY) / static_cast<float>(kChunkSize - 1);

		// Bilinear interpolation
		float top = cornerElevations[0] * (1.0F - u) + cornerElevations[1] * u;
		float bottom = cornerElevations[2] * (1.0F - u) + cornerElevations[3] * u;
		return top * (1.0F - v) + bottom * v;
	}

  private:
	/// Bilinear interpolation of biome weights from corners.
	/// u = 0..1 (west to east), v = 0..1 (north to south)
	[[nodiscard]] BiomeWeights bilinearInterpolate(float u, float v) const {
		BiomeWeights result;

		// Interpolate each biome weight separately
		for (size_t i = 0; i < static_cast<size_t>(Biome::Count); ++i) {
			Biome biome = static_cast<Biome>(i);

			float nw = cornerBiomes[0].get(biome);
			float ne = cornerBiomes[1].get(biome);
			float sw = cornerBiomes[2].get(biome);
			float se = cornerBiomes[3].get(biome);

			float top = nw * (1.0F - u) + ne * u;
			float bottom = sw * (1.0F - u) + se * u;
			float weight = top * (1.0F - v) + bottom * v;

			if (weight > 0.001F) {
				result.set(biome, weight);
			}
		}

		result.normalize();
		return result;
	}
};

}  // namespace engine::world
