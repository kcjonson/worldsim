#include "UseRoom.h"

#include "NavigationSystem.h"
#include "PlayerVisibilitySystem.h"

#include <glm/common.hpp>

#include <algorithm>

namespace ecs {

	UseVerdict judgeUse(
		const NavigationSystem& navigation, const PlacedItemIndex& items, const ItemOutlines& outlines, glm::vec2 position,
		const std::vector<std::vector<glm::vec2>>& extraObstacles, const PlayerVisibilitySystem* visibility, EntityID self, EntityID ignore
	) {
		UseVerdict verdict;
		if (outlines.useAreas.empty()) {
			return verdict;
		}
		glm::vec2				lower = position;
		glm::vec2				upper = position;
		std::vector<glm::vec2>	seeds{position};
		bool					unexplored = false;
		for (const std::vector<assets::AreaPolygon>& area : outlines.useAreas) {
			for (const assets::AreaPolygon& polygon : area) {
				for (const glm::vec2 point : polygon.outer) {
					lower = glm::min(lower, point);
					upper = glm::max(upper, point);
				}
				seeds.insert(seeds.end(), polygon.outer.begin(), polygon.outer.end());
				unexplored = unexplored || (visibility != nullptr && !visibility->covers(polygon.outer));
			}
		}
		const glm::vec2						  reach{assets::kLargestColonistRadiusMeters, assets::kLargestColonistRadiusMeters};
		const NavigationSystem::UseGround	  ground = navigation.openGround(lower - reach, upper + reach, seeds);
		std::vector<std::vector<glm::vec2>> obstacles = extraObstacles;
		std::vector<const PlacedItemIndex::Entry*> near;
		items.query(lower - reach, upper + reach, near);
		for (const PlacedItemIndex::Entry* other : near) {
			const bool itself = other->entity != kInvalidEntity && (other->entity == self || other->entity == ignore);
			if (other->pending && !itself) {
				obstacles.insert(obstacles.end(), other->world.collider.begin(), other->world.collider.end());
			}
		}

		for (const std::vector<assets::AreaPolygon>& area : outlines.useAreas) {
			assets::StandingRegion region = assets::standingRegion(area, obstacles, &ground.ground, assets::kLargestColonistRadiusMeters);
			for (geometry::RegionPolygon& piece : region.pieces) {
				// Ground the player hasn't seen is no place to send a colonist.
				if (visibility == nullptr || visibility->isSeen(assets::pointInside(piece))) {
					verdict.standing.pieces.push_back(std::move(piece));
				}
			}
		}
		if (verdict.standing.empty()) {
			verdict.use = unexplored || ground.reachesUnmeshed ? UseState::Unexplored : UseState::Blocked;
		}
		return verdict;
	}

} // namespace ecs
