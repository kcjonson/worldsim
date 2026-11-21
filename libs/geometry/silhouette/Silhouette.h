#pragma once

#include "../polygon/Polygon.h" // Ring

#include <cstdint>
#include <vector>

// Selection silhouettes as vector outlines: unions of polygons, closed and simplified by vector operations only.
// An outline is a set of disjoint simple CCW rings with their holes filled. Every length is in the input's units
// (integer millimetres for the game's assets).

namespace geometry {

	// The outline of the union of rings, each filled whatever its winding: every ring is wound CCW, the union
	// taken by the nonzero rule, and only the outer rings kept, so any hole is filled. Rings with no area are
	// ignored; empty for no area.
	std::vector<Ring> outlineOfUnion(std::vector<Ring> rings);

	// The outline grown by `radius`: its Minkowski sum with the regular octagon whose edges touch the circle of
	// that radius, so every point within `radius` of the outline is inside. Each edge swept by the octagon is
	// unioned with the outline. A radius of 0 or less returns the outline unchanged.
	std::vector<Ring> dilateOutline(const std::vector<Ring>& outline, std::int64_t radius);

	// The outline dilated and then eroded by the same octagon (a morphological closing), simplified by
	// `tolerance`: gaps and notches narrower than about twice `radius` fill, and the boundary elsewhere stays where
	// it was. The erosion is the dilation of the complement, taken inside a box round the outline. A radius of 0 or
	// less returns the outline unchanged.
	std::vector<Ring> closeOutline(const std::vector<Ring>& outline, std::int64_t radius, std::int64_t tolerance);

	// Each ring simplified (Douglas-Peucker) so no removed vertex lies further than `tolerance` from what is
	// kept, then unioned again so every ring stays simple.
	std::vector<Ring> simplifyOutline(std::vector<Ring> outline, std::int64_t tolerance);

	// The filled outline of a set of triangles (the tessellated draw geometry, fills and stroke bands alike):
	// `triangleVertices.size()` is a multiple of 3, each consecutive triple one triangle of any winding;
	// degenerate triangles are ignored. The union of the triangles simplified by `tolerance`, then closed by
	// `closeRadius`. Unioned in halves (each half's outline unioned with the other's), so a triangle's inner edges
	// never meet the rest of the shape. Empty for no area.
	std::vector<Ring> silhouetteOfTriangles(
		const std::vector<Vec2i64>& triangleVertices, std::int64_t closeRadius, std::int64_t tolerance
	);

} // namespace geometry
