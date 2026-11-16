#pragma once

// The land shader's pair table (docs/technical/ground-textures.md, "Pair parameters" and "The contest"):
// for every pair of land surfaces, what the upper one's def says about meeting the lower one, at
// landPairIndex(upper, lower) by D13 level (0 Mud .. 8 Snow). Built once from the defs; each draw packs
// it into land.glsl's u_landPair with that draw's mip window and pixel size folded in, so the shader's
// contest is a handful of multiply-adds and no divide.

#include "world/chunk/SurfaceField.h"

#include <assets/GroundTransition.h>

#include <array>
#include <cstddef>
#include <span>

namespace worldview {

	inline constexpr size_t kLandLevels = 9;
	/// Pairs of D13 levels, the upper above the lower: the only ones a band reads.
	inline constexpr size_t kLandPairs = kLandLevels * (kLandLevels - 1) / 2;
	/// vec4s an entry takes in u_landPair.
	inline constexpr size_t kLandPairVec4s = 2;
	inline constexpr size_t kLandPairFloats = kLandPairs * kLandPairVec4s * 4;

	/// The window's slope per mip level: W = max(softness, mip / 6), fitted offline against a supersampled
	/// reference (ground-textures.md, "The mip window").
	inline constexpr float kLandWindowPerMip = 1.0F / 6.0F;

	/// Where D13 level `upper` over `lower` (upper > lower) sits in the table and in u_landPair: the rows of
	/// a lower triangle, as land.glsl indexes it.
	[[nodiscard]] constexpr size_t landPairIndex(size_t upper, size_t lower) {
		return (upper * (upper - 1) / 2) + lower;
	}

	using GroundPairTable = std::array<assets::GroundTransition, kLandPairs>;

	/// The table from every surface's transitions, indexed by surface id (Water's are never read).
	[[nodiscard]] GroundPairTable buildGroundPairTable(const std::array<assets::GroundTransitions, world::kSurfaceCount>& bySurface);

	/// One draw's u_landPair, entry by entry two vec4:
	///   x 0.5 / w, y 0.5 + 0.5 o / w: the band coordinate (b + 1) / 2 is saturate(sigma * x + y)
	///   z 1 / (2 W), w nu / (4 W): the contest is saturate((hA - hB) z + b (z + w + 0.5) + along w + 0.5)
	///   then shadow, stain, ridge and tint.
	/// W is the window, max(softness, max(mip of either surface) * kLandWindowPerMip), and w the width
	/// floored at one pixel so a band never narrows below the line it replaces. The floor stops where w plus
	/// the offset reaches assets::kGroundBandReachM, past which the field reports no edge and the band would
	/// end in a cut. `mipLevels` is each surface's mip level this draw, by surface id, lod bias included.
	void packGroundPairs(
		const GroundPairTable& table, const std::array<float, world::kSurfaceCount>& mipLevels, float metersPerPixel, std::span<float, kLandPairFloats> out
	);

} // namespace worldview
