#pragma once

// A placed entity's collider in world metres: the one transform the nav mesh's placed-obstacle rings, the
// clear-ground test and static-rect collision all read (NavInputBuilder.h's placedObstacleOutline builds its
// ring from it).

#include <assets/AssetDefinition.h>
#include <assets/placement/SpatialIndex.h>

#include <glm/vec2.hpp>

#include <cstdint>
#include <optional>

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

	// The box of a placed entity's Rect collider: the local rect, padded, then scaled, rotated and
	// moved to the entity. A negative scale mirrors the offset through the entity's position and
	// leaves the box's size alone. nullopt for any other shape.
	[[nodiscard]] std::optional<OrientedRect> placedObstacleBox(const assets::PlacedEntity& entity, const assets::CollisionShape& collision);

} // namespace nav
