#pragma once

// Turning a point about the origin, as a placed item and its parts turn (rotatable-placed-items.md, section 3).

#include <glm/vec2.hpp>

#include <cmath>

namespace assets {

	/// `point` turned by `radians`, positive from +X toward +Y: clockwise on the screen, since +Y is down.
	[[nodiscard]] inline glm::vec2 turnedPoint(glm::vec2 point, float radians) {
		const float cosine = std::cos(radians);
		const float sine = std::sin(radians);
		return {point.x * cosine - point.y * sine, point.x * sine + point.y * cosine};
	}

} // namespace assets
