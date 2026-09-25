#pragma once

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"

#include <vector>

namespace geometry {

	// Closed axis-aligned rectangle in integer mm, min < max on both axes.
	struct RectMm {
		Vec2i64 min;
		Vec2i64 max;
	};

	// The intersection of ring and rect as simple rings in the ring's orientation: a CCW ring yields CCW
	// pieces, a CW hole ring CW pieces (terrain-polygons D4, navRings). Crossings
	// with the rect boundary are inserted with pinAxisLineCrossings, so they land
	// on the same integer points two neighboring chunks compute for a shared
	// border. Each piece is a run of the ring inside the rect closed along the
	// rect boundary (a Weiler-Atherton walk against the rectangle), so a concave
	// ring that crosses the border several times gives several disconnected
	// pieces, never one ring joined by zero-width bridges. Ring edges lying on the
	// boundary are kept only when the rect is on their inner side.
	//
	// A ring entirely inside is returned unchanged, one entirely outside gives no
	// pieces, and one containing the whole rect gives the rect. Precondition: the
	// ring is simple.
	std::vector<Ring> clipRingToRect(const Ring& ring, const RectMm& rect);

} // namespace geometry
