#include "WallStandPoint.h"

#include "NavigationSystem.h"

#include <algorithm>

namespace ecs {

	std::optional<glm::vec2> wallStandPoint(const construction::WallStand& stand, float agentRadius, const NavigationSystem& nav) {
		for (const glm::vec2 point : stand.standPoints(agentRadius)) {
			const std::optional<glm::vec2> snapped = nav.nearestPathablePoint(point, agentRadius);
			if (snapped.has_value() && stand.serves(*snapped, point, agentRadius)) {
				return snapped;
			}
		}
		return std::nullopt;
	}

	bool noGroundToBuildFrom(std::span<const construction::WallStand> stands, float agentRadius, const NavigationSystem& nav) {
		return !stands.empty() && nav.hasMesh() && std::none_of(stands.begin(), stands.end(), [&](const construction::WallStand& stand) {
				   return wallStandPoint(stand, agentRadius, nav).has_value();
			   });
	}

} // namespace ecs
