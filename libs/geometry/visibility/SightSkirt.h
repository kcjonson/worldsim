#pragma once

// The seen record's skirt (player-visibility.md 3.3): a sight shape with walls in sight enters the
// record grown outward a little, so the slivers and pinholes that near-coincident wall hits leave
// in an exact union cannot form. The covered test keeps the true shape.

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"

#include <cstdint>

namespace geometry {

	constexpr std::int64_t kSeenSkirtMm = 50;

	// `shape` (a visibility polygon round `centre`, from computeVisibilityPolygon with
	// `sightRadiusMm`) grown outward by up to `skirtMm` everywhere but along the rim of
	// sightPolygon(centre, sightRadiusMm).
	//
	// Every side of the shape off the rim is moved out along its normal by just under the skirt,
	// the moved sides joined round each convex corner by a chord (two above 90 degrees of turn)
	// and through the corner itself at each reflex one. That closed curve winds at least once
	// round every point of the shape, and every point of it lies within the skirt of the shape.
	// The shape grown by a disc of the skirt is star-shaped round `centre`, because a point's
	// distance from a star-shaped shape never falls along a ray out from its centre. So the
	// curve's farthest point along each ray from `centre` bounds a star-shaped ring that holds
	// the shape and lies within the skirt of it; that ring, held to the rim, is the result. A
	// notch narrower than twice the skirt closes up to where it is that wide.
	//
	// Its vertices are the shape's own on the rim, the rim's, the moved sides' ends and their
	// crossings, snapped to the millimetre. Empty when the snapped ring is not simple round
	// `centre` (simpleAround), or when a moved side passes through `centre`, which needs `centre`
	// within the skirt of the shape's edge.
	Ring skirtedSightShape(const Ring& shape, const Vec2i64& centre, std::int64_t sightRadiusMm, std::int64_t skirtMm);

} // namespace geometry
