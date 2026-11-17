#pragma once

// A placed entity's collider in world metres: the one transform the nav mesh's placed-obstacle rings, the
// clear-ground test and static-rect collision all read (NavInputBuilder.h's placedObstacleOutline builds its
// ring from it).

#include <assets/AssetDefinition.h>
#include <assets/placement/SpatialIndex.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>

namespace assets {
	class AssetRegistry;
}

namespace nav {

	// Pad added outward around a placed entity's collider, in mm, so an agent clears it rather than
	// grazing it.
	constexpr std::int64_t kPlacedObstaclePadMm = 50;

	// An oriented box in world metres. axisX and axisY are unit (rotation only); halfExtents are
	// non-negative and include the entity's scale and the pad.
	struct OrientedRect {
		glm::vec2 center{0.0F, 0.0F};
		glm::vec2 axisX{1.0F, 0.0F};
		glm::vec2 axisY{0.0F, 1.0F};
		glm::vec2 halfExtents{0.0F, 0.0F};
	};

	// The box of a placed entity's Rect collider: the rect, which is in its def's pivot frame, padded,
	// then scaled, rotated and moved to the entity's position, as its mesh is drawn. A negative scale
	// mirrors the offset through the entity's position and leaves the box's size alone. nullopt for
	// any other shape.
	[[nodiscard]] std::optional<OrientedRect> placedObstacleBox(const assets::PlacedEntity& entity, const assets::CollisionShape& collision);

	// The farthest any placed entity's padded collider can reach from its position, over the defs
	// `registry` holds: its recorded collider reach plus the pad's corner reach at the largest
	// placement scale of a colliding def. What finds placed obstacles by position (nav limits, static
	// collision) must look this far, so no collider is missed by construction.
	[[nodiscard]] float placedObstacleReachMeters(const assets::AssetRegistry& registry);

} // namespace nav
