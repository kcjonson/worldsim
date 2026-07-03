#pragma once

#include "../polygon/Polygon.h" // Ring

#include <cstdint>
#include <vector>

namespace geometry {

	// Filled outer silhouette of a set of contour rings. Covered region = nonzero
	// winding over all rings; interior holes are filled; disjoint components are
	// returned as separate CCW rings. Input rings are closed Vec2i64 loops of any
	// winding (CCW fills, CW rings punch holes under the nonzero rule). Output
	// rings are CCW and simplified. Empty input or fully-degenerate input returns {}.
	// targetResolution = target pixel count along the longest bbox side (raster fidelity).
	std::vector<Ring> silhouetteOfRings(const std::vector<Ring>& rings, std::int64_t targetResolution = 256);

	// Filled outer silhouette of a set of triangles (the tessellated draw geometry).
	// `triangleVerts.size()` is a multiple of 3; each consecutive triple is one
	// triangle of ANY winding. Covered region = the union of the triangles' pixel
	// coverage, so the result captures the EXACT rendered footprint (fills and
	// stroke bands both), not an approximation of the source contours. Interior
	// holes are filled, disjoint components are returned as separate CCW simplified
	// rings; degenerate (zero-area) triangles are ignored. Empty or fully-degenerate
	// input returns {}. targetResolution as in silhouetteOfRings.
	//
	// closeRadiusPx > 0 morphologically closes the mask (dilate then erode by that
	// many pixels) before hole-filling, bridging gaps up to 2*closeRadiusPx between
	// disjoint covered blobs without growing the outer boundary. Use it to turn a
	// scattered clump into one forgiving hit region. 0 leaves the mask untouched.
	std::vector<Ring> silhouetteOfTriangles(const std::vector<Vec2i64>& triangleVerts,
											std::int64_t targetResolution = 256, std::int64_t closeRadiusPx = 0);

} // namespace geometry
