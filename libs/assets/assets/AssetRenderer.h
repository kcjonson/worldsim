#pragma once

// AssetRenderer: turn an asset definition into pixels through the game's own
// render pipeline (tessellation + BatchRenderer), with no world or camera inputs.
// This is the shared render path used by the headless CLI and the GUI previews.

#include "graphics/Color.h"
#include "graphics/Rect.h"
#include "vector/Types.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace assets {

	struct PreparedAsset {
		std::vector<renderer::TessellatedMesh> meshes;								 // vertices in target-rect pixel space, drawn in order
		Foundation::Rect					   sourceBounds{0.0F, 0.0F, 0.0F, 0.0F}; // framed region in asset-local space, before fit
		Foundation::Rect					   targetRect{0.0F, 0.0F, 0.0F, 0.0F};	 // rect the mesh was fit into (pixel space)
		std::optional<Foundation::Rect>		   clip;			 // a ground tile's square in pixel space; draw clipped to it
		bool								   hasOutput = true; // false if the asset produced no geometry
	};

	// CPU geometry prep (no GL). Builds the asset's meshes for `seed` and fits them
	// centered into `target`, preserving aspect ratio. Simple assets ignore the seed. A
	// ground tile frames its [0, period] square rather than its mesh bounds, and is built at
	// the density that square lands at (`pixelScale` device pixels a target unit), so a path
	// tagged finer than that (setLod) is neither tessellated nor drawn.
	PreparedAsset prepareAsset(const std::string& defName, const Foundation::Rect& target, uint32_t seed, float pixelScale = 1.0F);

	// Prepare `count` procedural forms from seeds baseSeed .. baseSeed + count - 1.
	std::vector<PreparedAsset> prepareSamples(const std::string& defName, const Foundation::Rect& target, int count, uint32_t baseSeed);

	// Offscreen render of an asset to RGBA8 pixels (top-left origin, straight alpha)
	// through the game's pipeline. Requires a current GL context with
	// Renderer::Primitives initialized. Returns an empty vector on failure.
	std::vector<uint8_t> renderToPixels(const std::string& defName, int width, int height, Foundation::Color background, uint32_t seed);

	// Render an asset to a PNG file. Returns false on failure.
	bool renderToPng(const std::string& defName, const std::string& outPath, int width, int height, Foundation::Color background, uint32_t seed);

	// RGBA8 pixels, top-left origin, straight alpha.
	struct RenderedImage {
		std::vector<uint8_t> pixels;
		int					 width = 0;
		int					 height = 0;
	};

	// Meshes in asset-local meters laid out as rows of cells and all drawn at pixelsPerMeter, so a
	// young tree stands small beside an old one. Every cell is as big as the largest mesh, and each
	// mesh's origin (a tree's trunk foot) sits at the same place in its cell. A row shorter than the
	// longest leaves its last cells empty. `silhouette` fills every triangle the selection silhouette
	// keeps (at least one vertex at AssetRegistry::kSilhouetteMinAlpha) in one flat dark grey and leaves
	// out the rest. Requires a current GL context with Renderer::Primitives initialized. Empty pixels
	// when a mesh is null or empty, or the sheet is over 16384 pixels a side.
	RenderedImage renderMeshSheet(
		const std::vector<std::vector<const renderer::TessellatedMesh*>>& rows, float pixelsPerMeter, Foundation::Color background, bool silhouette
	);

} // namespace assets
