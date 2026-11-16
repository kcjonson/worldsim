#pragma once

// A ground def's SVG variant read into the paths a ground generator draws (docs/technical/ground-textures.md,
// "Ground tile assets" and "LOD"): each shape one GeneratedPath in document order, so the registry ranks,
// wraps and tessellates it as it does a Lua generator's output. The loader and AssetValidator read an SVG
// through this one function.

#include "assets/IAssetGenerator.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace assets {

	struct GroundSvgArgs {
		std::span<const std::byte> svg;
		float					   periodMeters = 4.0F;
		uint32_t				   pixelsPerMeter = 64; ///< the bake's mip 0 density, which sets how finely curves flatten
		std::string_view		   name;				///< what the log calls the SVG
	};

	/// The SVG's shapes in meters on its [0, period] square, its viewBox, each a path with its data-lod
	/// (the largest on it and its ancestors, else 0) as minPixelsPerMeter and its data-height (its own,
	/// else its nearest ancestor's, else unset) as height. What the bake can't draw as written goes in
	/// `issues`, one message each naming the element and its line: a viewBox other than "0 0 period
	/// period", a data-lod or data-height out of range, a shape with a stroke, a gradient fill or more
	/// than one outline, a <use>, <text> or <image> (NanoSVG draws none of them), a clip-path or mask
	/// (it ignores both), and an SVG that draws nothing. Any issue means the paths aren't the SVG's art.
	[[nodiscard]] GeneratedAsset readGroundSvg(const GroundSvgArgs& args, std::vector<std::string>& issues);

} // namespace assets
