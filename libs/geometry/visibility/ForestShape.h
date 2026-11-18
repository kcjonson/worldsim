#pragma once

// The forest shape (sight-softening.md 4.2): the intersection of one half-plane a side of the sight
// polygon, each parallel to its side and range x cos(pi / kSightSides) from the point. With every
// range equal it is the sight polygon of that radius to 2 mm; it is convex by construction. In
// integers throughout.

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"
#include "ForestDepth.h"

#include <array>
#include <cstdint>

namespace geometry {

	// The least range a forest shape takes. Its half-planes then hold a disc of 99 mm round the point,
	// so the shape is a polygon of at least three vertices, never a point or a segment.
	constexpr std::int64_t kForestShapeLeastRangeMm = 100;

	// The half-planes of the forest shape of some ranges, built once for any number of point queries:
	// side k's holds the offsets from the point whose dot product with kSightSideNormals[k] is at most
	// offsets[k], that side's range x cos(pi / kSightSides) at the normals' scale.
	//
	// A point test reads `leastOffset` to accept anything inside the disc every half-plane holds, and
	// `window` for how many sides either side of the side its direction falls in can exclude it: side
	// j can only cut a point inside side k's half-plane whose direction lies within half a side of side
	// k's normal when cos((|j - k| - 1) x 2 pi / kSightSides) exceeds leastOffset x cos(pi /
	// kSightSides) / mostOffset, the normals' length spread allowed for.
	struct ForestHalfPlanes {
		std::array<std::int64_t, kSightSides> offsets{};
		std::int64_t						  leastOffset = 0;
		std::int64_t						  mostOffset  = 0;
		std::size_t							  window	  = kSightSides / 2;
	};

	// Every range lies in [kForestShapeLeastRangeMm, kForestRadiusLimitMm).
	ForestHalfPlanes forestHalfPlanes(const SightSideValues& ranges);

	// Whether `query` lies in every half-plane of `halfPlanes` round `point`: one squared-length test
	// accepts it inside the disc every half-plane holds; past that, a binary search by angle finds the
	// side its direction falls in and it is tested against that side and the `window` sides either side
	// of it, which give the same answer as all 64.
	bool insideForestHalfPlanes(const Vec2i64& point, const ForestHalfPlanes& halfPlanes, const Vec2i64& query);

	// The forest shape of `ranges` round `point`, counter-clockwise, starting from the vertex at or
	// counter-clockwise past +x from the point. Every range lies in [kForestShapeLeastRangeMm,
	// kForestRadiusLimitMm).
	//
	// Its vertices are the exact crossings of consecutive kept edges (an edge another pair of edges
	// lies wholly inside is not kept), each placed at the nearest of the crossing's four integer
	// neighbours that lies in every half-plane, or, when none does, at the first point that does on
	// the line from the crossing rounded to nearest to the point, a millimetre of the larger
	// coordinate a step, rounded to nearest. Then the convex hull of those, so it is strictly convex
	// and every vertex lies in every half-plane. A vertex placed at a neighbour is within sqrt(2) mm
	// of its crossing, and every vertex within sqrt(2) x (2 + R / r) mm, R the largest range and r
	// the least x cos(pi / kSightSides) less 1 mm: 5.1 mm with a 32.2 m reach over the defaults'
	// 20.1 m floor.
	Ring forestShape(const Vec2i64& point, const SightSideValues& ranges);

} // namespace geometry
