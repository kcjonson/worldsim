#pragma once

// What finds a placed entity's collider by position. The collider's outline itself, the one ring the
// nav mesh cuts, the clear-ground test reads and placed-obstacle collision pushes against, is
// NavInputBuilder.h's placedObstacleOutline.

#include <cstdint>

namespace assets {
	class AssetRegistry;
}

namespace nav {

	// Pad added outward around a placed entity's Rect collider, in mm, in its local frame before
	// scale, so an agent clears it rather than grazing it. A Polygon collider is its points as
	// authored.
	constexpr std::int64_t kPlacedObstaclePadMm = 50;

	// The farthest any placed entity's padded collider can reach from its position, over the defs
	// `registry` holds: its recorded collider reach plus the pad's corner reach at the largest
	// placement scale of a colliding def. What finds placed obstacles by position (nav limits, static
	// collision) must look this far, so no collider is missed by construction.
	[[nodiscard]] float placedObstacleReachMeters(const assets::AssetRegistry& registry);

} // namespace nav
