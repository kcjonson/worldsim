#include "assets/MeshSilhouette.h"

#include <core/Vec2i64.h>
#include <silhouette/Silhouette.h>

#include <algorithm>
#include <cstdint>

namespace assets {

	namespace {

		/// The longer side of the box round some points, in millimetres.
		std::int64_t longestSide(const std::vector<geometry::Vec2i64>& points) {
			if (points.empty()) {
				return 0;
			}
			geometry::Vec2i64 low = points.front();
			geometry::Vec2i64 high = low;
			for (const geometry::Vec2i64& point : points) {
				low = {std::min(low.x, point.x), std::min(low.y, point.y)};
				high = {std::max(high.x, point.x), std::max(high.y, point.y)};
			}
			return std::max(high.x - low.x, high.y - low.y);
		}

		/// At least a millimetre, the smallest length the rings can hold.
		std::int64_t fractionOf(std::int64_t length, std::int64_t divisor) {
			return std::max<std::int64_t>(1, length / divisor);
		}

		AssetSilhouette silhouetteOfRings(std::vector<geometry::Ring> rings, std::int64_t longest) {
			AssetSilhouette silhouette;
			// closeOutline holds its input, so a click anywhere on what is outlined hits.
			silhouette.hitRegion =
				geometry::closeOutline(rings, fractionOf(longest, kHitRegionCloseDivisor), fractionOf(longest, kHitRegionToleranceDivisor));
			silhouette.rings = std::move(rings);
			silhouette.valid = !silhouette.rings.empty();
			return silhouette;
		}

	} // namespace

	AssetSilhouette silhouetteOfMesh(const renderer::TessellatedMesh& mesh) {
		const bool hasColors = mesh.hasColors();
		const auto faint = [&](uint16_t index) { return hasColors && mesh.colors[index].a < kSilhouetteMinAlpha; };
		std::vector<geometry::Vec2i64> triangleVertices;
		triangleVertices.reserve(mesh.indices.size());
		for (size_t triangle = 0; triangle + 2 < mesh.indices.size(); triangle += 3) {
			const uint16_t first = mesh.indices[triangle];
			const uint16_t second = mesh.indices[triangle + 1];
			const uint16_t third = mesh.indices[triangle + 2];
			if (faint(first) && faint(second) && faint(third)) {
				continue;
			}
			for (const uint16_t index : {first, second, third}) {
				const Foundation::Vec2& vertex = mesh.vertices[index];
				triangleVertices.push_back(geometry::quantize({vertex.x, vertex.y}));
			}
		}
		const std::int64_t longest = longestSide(triangleVertices);
		if (longest == 0) {
			return {};
		}
		return silhouetteOfRings(
			geometry::silhouetteOfTriangles(triangleVertices, fractionOf(longest, kSilhouetteCloseDivisor), fractionOf(longest, kSilhouetteToleranceDivisor)),
			longest
		);
	}

	AssetSilhouette silhouetteOfShapes(std::vector<geometry::Ring> shapes) {
		std::vector<geometry::Vec2i64> points;
		for (const geometry::Ring& shape : shapes) {
			points.insert(points.end(), shape.begin(), shape.end());
		}
		const std::int64_t longest = longestSide(points);
		if (longest == 0) {
			return {};
		}
		return silhouetteOfRings(geometry::outlineOfUnionInOnePass(std::move(shapes)), longest);
	}

} // namespace assets
