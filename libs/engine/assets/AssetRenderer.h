#pragma once

// AssetRenderer: turn an asset definition into pixels through the game's own
// render pipeline (tessellation + BatchRenderer), with no world or camera inputs.
// This is the shared render path used by the headless CLI and the GUI previews.

#include "graphics/Color.h"
#include "graphics/Rect.h"
#include "vector/Types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace engine::assets {

	struct PreparedAsset {
		renderer::TessellatedMesh mesh;		   // vertices in target-rect pixel space
		Foundation::Rect		  sourceBounds{0.0F, 0.0F, 0.0F, 0.0F}; // raw mesh bounds (asset-local space) before fit
		Foundation::Rect		  targetRect{0.0F, 0.0F, 0.0F, 0.0F};	// rect the mesh was fit into (pixel space)
		bool					  hasOutput = true; // false if the asset produced no geometry
	};

	// CPU geometry prep (no GL). Builds the asset's mesh for `seed` and fits it
	// centered into `target`, preserving aspect ratio. Simple assets ignore the seed.
	PreparedAsset prepareAsset(const std::string& defName, const Foundation::Rect& target, uint32_t seed);

	// Prepare `count` procedural forms from seeds baseSeed .. baseSeed + count - 1.
	std::vector<PreparedAsset> prepareSamples(const std::string& defName, const Foundation::Rect& target, int count, uint32_t baseSeed);

	// Offscreen render of an asset to RGBA8 pixels (top-left origin) through the
	// game's pipeline. Requires a current GL context with Renderer::Primitives
	// initialized. Returns an empty vector on failure.
	std::vector<uint8_t> renderToPixels(const std::string& defName, int width, int height, Foundation::Color background, uint32_t seed);

	// Render an asset to a PNG file. Returns false on failure.
	bool renderToPng(const std::string& defName, const std::string& outPath, int width, int height, Foundation::Color background, uint32_t seed);

} // namespace engine::assets
