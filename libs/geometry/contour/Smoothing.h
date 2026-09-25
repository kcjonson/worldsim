#pragma once

#include "../polygon/Polygon.h"

namespace geometry {

	// Chaikin corner cutting on a closed ring (terrain-polygons D6 step 3). Each
	// pass replaces edge a->b by the points (3a + b) / 4 and (a + 3b) / 4, each
	// rounded to the nearest mm with halves toward +infinity. Every output vertex
	// depends only on its own edge's two endpoints and the rounding does not depend
	// on edge direction, so the same edge smooths to the same points in any ring
	// that contains it. Consecutive duplicates left by rounding on edges under
	// 2 mm are collapsed. Rings with fewer than 3 vertices are left alone.
	void chaikin(Ring& ring, int iterations);

} // namespace geometry
