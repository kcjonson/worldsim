#pragma once

// The forest shape (sight-softening.md 4.2): the intersection of one half-plane a side of the sight
// polygon, each parallel to its side and range x cos(pi / kSightSides) from the point. With every
// range equal it is the sight polygon of that radius to 2 mm; it is convex by construction. In
// integers throughout.

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"
#include "ForestDepth.h"

namespace geometry {

	// Whether `query` lies in every side's half-plane of the forest shape of `ranges` round `point`.
	// Every range is positive and under kForestRadiusLimitMm, and `query` within that of `point`.
	bool insideForestHalfPlanes(const Vec2i64& point, const SightSideValues& ranges, const Vec2i64& query);

	// The forest shape of `ranges` round `point`, counter-clockwise, starting from the vertex at or
	// counter-clockwise past +x from the point. Its vertices are the crossings of consecutive kept
	// edges (an edge another pair of edges lies wholly inside is not kept), rounded toward the point
	// and pulled in a millimetre at a time while any half-plane fails, then the convex hull of those,
	// so it is strictly convex and every vertex lies in every half-plane.
	Ring forestShape(const Vec2i64& point, const SightSideValues& ranges);

} // namespace geometry
