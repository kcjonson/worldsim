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

} // namespace geometry
