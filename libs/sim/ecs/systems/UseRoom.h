#pragma once

// Whether a colonist can use an item from its use areas (rotatable-placed-items 4.2, 12): the room test with its
// placement and afterwards inputs. The ground is the nav mesh's (walkable ground, water, built walls, installed
// obstacles, the item's own collider once it is in); the pending install targets come from PlacedItemIndex, which
// nav never reads; placement adds the ghost's collider and the blueprint walls. Ground not explored or not meshed
// is never stood on: a use area with room only there reads Unexplored.

#include "../EntityID.h"
#include "../PlacedItemIndex.h"

#include <glm/vec2.hpp>

#include <vector>

namespace ecs {

	class NavigationSystem;
	class PlayerVisibilitySystem;

	struct UseVerdict {
		UseState			   use = UseState::Usable;
		assets::StandingRegion standing;
	};

	/// The use state and standing region of an item whose outlines in the world are `outlines`: its use areas less the
	/// ground off nav's open ground, the pending targets' colliders `items` holds (`self` and `ignore` aside), and
	/// `extraObstacles` (rings in metres), eroded by kLargestColonistRadiusMeters. `visibility` null takes every
	/// ground as explored (the dev verbs, a revealed map). Usable for an item with no use area.
	[[nodiscard]] UseVerdict judgeUse(
		const NavigationSystem& navigation, const PlacedItemIndex& items, const ItemOutlines& outlines, glm::vec2 position,
		const std::vector<std::vector<glm::vec2>>& extraObstacles, const PlayerVisibilitySystem* visibility, EntityID self, EntityID ignore
	);

} // namespace ecs
