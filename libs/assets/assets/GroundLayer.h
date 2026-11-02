#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// The limits a ground def's bake must fit. AssetValidator rejects a def that breaks one when the
// library loads; GroundTileBaker and GroundTileBank refuse one before they allocate anything.
// No power of two is needed for the layer: the validator's pixelsPerMeter rule already makes it one,
// and the mip chain and the array textures take any size.

namespace assets {

	/// Texels along a layer's edge, at most. The baker's supersampled mip 0 is then 8192 px a side.
	inline constexpr int kMaxGroundLayerSize = 1024;

	/// Variants a ground def bakes, at most. The baker runs every variant's generator at once.
	inline constexpr uint32_t kMaxGroundVariants = 32;

	/// Distinct layer sizes the ground defs may use together: the bank keeps one texture array per size, and
	/// the tile shader has a sampler for each (u_groundTiles0 .. 3).
	inline constexpr int kMaxGroundArrays = 4;

	struct GroundLayer {
		int				   texels = 0;		// period x pixelsPerMeter, rounded; 0 when the product isn't a positive finite number
		bool			   whole = false;	// the product is a whole number of texels
		bool			   inRange = false; // texels is from 1 to kMaxGroundLayerSize
		[[nodiscard]] bool supported() const { return whole && inRange; }
	};

	[[nodiscard]] inline GroundLayer groundLayer(double periodMeters, double pixelsPerMeter) {
		GroundLayer	 layer;
		const double product = periodMeters * pixelsPerMeter;
		if (!(product > 0.0) || !std::isfinite(product)) {
			return layer;
		}
		layer.whole = std::abs(product - std::round(product)) < 1e-6;
		layer.texels = static_cast<int>(std::lround(std::min(product, 1.0e9)));
		layer.inRange = layer.texels >= 1 && layer.texels <= kMaxGroundLayerSize;
		return layer;
	}

	[[nodiscard]] constexpr bool groundVariantCountSupported(int64_t count) {
		return count >= 1 && count <= static_cast<int64_t>(kMaxGroundVariants);
	}

} // namespace assets
