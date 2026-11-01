#pragma once

// A procedural asset's paths, each tessellated once and tagged with the screen density it is drawn
// from (path:setLod), so one generator run serves every density: meshesAt packs the paths drawn at a
// density into meshes, the ones AssetRegistry::buildMeshes returns at it. The ground tile bake draws
// all of a variant's mips from one of these.

#include "assets/IAssetGenerator.h"

#include <graphics/Color.h>
#include <math/Types.h>
#include <vector/Types.h>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace assets {

	struct TessellatedPaths {
		/// One mesh holds at most this many vertices: its indices are 16-bit.
		static constexpr size_t kMaxMeshVertices = std::numeric_limits<uint16_t>::max();

		struct Path {
			uint32_t firstVertex = 0;
			uint32_t vertexCount = 0;
			uint32_t firstIndex = 0;
			uint32_t indexCount = 0;
			float	 minPixelsPerMeter = 0.0F; // drawn where the screen shows at least this many px/m
		};

		std::vector<Foundation::Vec2>  vertices;
		std::vector<Foundation::Color> colors;			 // parallel to vertices, each its path's fill
		std::vector<uint16_t>		   indices;			 // each path's own, counted from its first vertex
		std::vector<Path>			   paths;			 // paint order
		double						   generateMs = 0.0; // the generator run that drew them
		double						   tessellateMs = 0.0;

		/// Replaces what this holds with the asset's paths drawn at or below maxPixelsPerMeter, each
		/// tessellated. A path of under 3 vertices, one the tessellator rejects, and one that alone
		/// passes kMaxMeshVertices are dropped, the last two logged.
		void tessellate(const GeneratedAsset& asset, float maxPixelsPerMeter);

		/// The paths drawn at pixelsPerMeter, in order, packed into as many meshes as keep each within
		/// kMaxMeshVertices. False when that's none.
		bool meshesAt(float pixelsPerMeter, std::vector<renderer::TessellatedMesh>& outMeshes) const;
	};

} // namespace assets
