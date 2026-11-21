#include "WallStand.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace construction {

	namespace {
		// A stepped-out disc lands this far past the band's edge, so rounding never leaves it touching.
		constexpr float kStepOutSlackMeters = 0.001F;
		// Rounding allowed on the serves test's distance and span.
		constexpr float kServesSlackMeters = 0.001F;
	} // namespace

	glm::vec2 WallBand::closestPoint(glm::vec2 position) const {
		const glm::vec2 along = end - start;
		const float		lengthSquared = glm::dot(along, along);
		if (lengthSquared <= 0.0F) {
			return start;
		}
		return start + along * std::clamp(glm::dot(position - start, along) / lengthSquared, 0.0F, 1.0F);
	}

	bool WallBand::overlaps(glm::vec2 position, float radiusMeters) const {
		return glm::distance(position, closestPoint(position)) < halfThicknessMeters + radiusMeters;
	}

	glm::vec2 WallBand::stepOut(glm::vec2 position, float radiusMeters, glm::vec2 outward) const {
		return closestPoint(position) + outward * (halfThicknessMeters + radiusMeters + kStepOutSlackMeters);
	}

	float WallStand::standOffset(float agentRadiusMeters) const {
		return halfThicknessMeters + agentRadiusMeters + kStandMarginMeters;
	}

	glm::vec2 WallStand::standPoint(float agentRadiusMeters, float alongMeters) const {
		const glm::vec2 along{outward.y, -outward.x};
		return midpoint + along * alongMeters + outward * standOffset(agentRadiusMeters);
	}

	std::array<glm::vec2, 5> WallStand::standPoints(float agentRadiusMeters) const {
		std::array<glm::vec2, 5> points{};
		for (std::size_t probe = 0; probe < points.size(); ++probe) {
			points[probe] = standPoint(agentRadiusMeters, kProbeFractions[probe] * halfLengthMeters);
		}
		return points;
	}

	bool WallStand::onSide(glm::vec2 position) const {
		return glm::dot(position - midpoint, outward) > 0.0F;
	}

	bool WallStand::serves(glm::vec2 point, glm::vec2 stand, float agentRadiusMeters) const {
		const glm::vec2 along{outward.y, -outward.x};
		return onSide(point) && glm::distance(point, stand) <= standOffset(agentRadiusMeters) + kServesSlackMeters &&
			   std::abs(glm::dot(point - midpoint, along)) <= halfLengthMeters + kServesSlackMeters;
	}

} // namespace construction
