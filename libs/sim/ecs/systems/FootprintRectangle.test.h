#pragma once

#include "../GoalTaskRegistry.h"

#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <glm/vec2.hpp>

namespace ecs {

	// The rectangle from `min` to `max` as a footprint, for a test whose site is a plain box.
	inline FootprintBounds rectangleFootprint(glm::vec2 min, glm::vec2 max) {
		const geometry::Vec2i64 low = geometry::quantize({min.x, min.y});
		const geometry::Vec2i64 high = geometry::quantize({max.x, max.y});
		return FootprintBounds{geometry::Ring{low, {high.x, low.y}, high, {low.x, high.y}}};
	}

} // namespace ecs
