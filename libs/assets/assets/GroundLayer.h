#pragma once

#include <algorithm>
#include <cmath>

// The layer size a ground tile can bake to. AssetValidator rejects a def that breaks it when the
// library loads; GroundTileBaker and GroundTileBank refuse one before they allocate anything.
// No power of two is needed here: the validator's pixelsPerMeter rule already makes the layer one,
// and the mip chain and the array textures take any size.

namespace assets {

	/// Texels along a layer's edge, at most. The baker's supersampled mip 0 is then 8192 px a side.
	inline constexpr int kMaxGroundLayerSize = 1024;

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

} // namespace assets
