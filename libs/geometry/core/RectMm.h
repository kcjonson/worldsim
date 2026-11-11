#pragma once

#include "Vec2i64.h"

namespace geometry {

	// Closed axis-aligned rectangle in integer mm, min < max on both axes.
	struct RectMm {
		Vec2i64 min;
		Vec2i64 max;
	};

} // namespace geometry
