#include "assets/MeshSilhouette.h"

#include <core/Vec2i64.h>
#include <silhouette/Silhouette.h>

#include <cstdint>

namespace assets {

	AssetSilhouette silhouetteOfMesh(const renderer::TessellatedMesh& mesh) {
		constexpr std::int64_t kSilhouetteResolution = 256; // pixels along the longest side
		constexpr std::int64_t kHitRegionCloseRadiusPx = 4;	// bridge disjoint-blob gaps (whole-clump)

		AssetSilhouette silhouette;
		const bool		hasColors = mesh.hasColors();
		const auto		faint = [&](uint16_t index) { return hasColors && mesh.colors[index].a < kSilhouetteMinAlpha; };
		std::vector<geometry::Vec2i64> triangleVertices;
		triangleVertices.reserve(mesh.indices.size());
		for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
			const uint16_t first = mesh.indices[t];
			const uint16_t second = mesh.indices[t + 1];
			const uint16_t third = mesh.indices[t + 2];
			if (faint(first) && faint(second) && faint(third)) {
				continue;
			}
			for (const uint16_t index : {first, second, third}) {
				const Foundation::Vec2& vertex = mesh.vertices[index];
				triangleVertices.push_back(geometry::quantize({vertex.x, vertex.y}));
			}
		}
		if (!triangleVertices.empty()) {
			silhouette.rings = geometry::silhouetteOfTriangles(triangleVertices, kSilhouetteResolution, 0);
			silhouette.hitRegion = geometry::silhouetteOfTriangles(triangleVertices, kSilhouetteResolution, kHitRegionCloseRadiusPx);
		}
		if (silhouette.hitRegion.empty()) {
			silhouette.hitRegion = silhouette.rings; // single-blob / degenerate: the outline is the hit region
		}
		silhouette.valid = !silhouette.rings.empty();
		return silhouette;
	}

} // namespace assets
