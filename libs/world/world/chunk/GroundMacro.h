#pragma once

// The ground's macro layer (docs/technical/ground-textures.md, "Shader"): a chunk's RGBA8 texture at
// 2 m a texel that the tile shader reads once a pixel, to tint each land surface toward its ground
// def's dry or lush color and to vary its value at scales the 4 m ground tiles can't carry. Filled
// on the chunk generation worker beside the render tiles, uploaded under the same version.
//
// Texel (i, j) holds the field at chunk-local (2i, 2j) m, i and j from 0 to 256, so a chunk's last
// row and column are its neighbor's first and a bilinear read matches across the border.

#include "world/chunk/ChunkCoordinate.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace world {

	inline constexpr int32_t kGroundMacroMetersPerTexel = 2;
	inline constexpr int32_t kGroundMacroSide = kChunkSize / kGroundMacroMetersPerTexel + 1;
	inline constexpr size_t	 kGroundMacroBytes = static_cast<size_t>(kGroundMacroSide) * kGroundMacroSide * 4;

	/// Wavelengths of the fields, meters. The mix is two octaves; the value terms are one each. 5 m is the
	/// shortest wave 2 m texels reconstruct without a lattice pattern under bilinear filtering.
	inline constexpr int32_t kGroundMacroMixWavelengthsM[2] = {36, 14};
	inline constexpr int32_t kGroundMacroMidWavelengthM = 10;
	inline constexpr int32_t kGroundMacroFineWavelengthM = 5;

	/// Fills `rgba` (kGroundMacroBytes, rows top to bottom) for chunk `coord`. R is the dry (0) to lush (1)
	/// mix; G and B are value noise centered on 0.5 at the mid and fine wavelengths, full scale at 0 and 1;
	/// A is 255. A pure function of the world position and seed, so neighbors agree on shared texels.
	void fillGroundMacro(ChunkCoordinate coord, uint64_t worldSeed, std::span<uint8_t> rgba);

} // namespace world
