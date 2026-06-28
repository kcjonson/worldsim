#pragma once

#include <glm/vec2.hpp>

namespace ecs {

	/// The colonist settlement: a reusable colony-wide anchor, set once when the
	/// colonists land. originPosition is the validated, entity-cleared walkable
	/// clearing the first colonist spawned into, in 2D world meters.
	///
	/// This is the single source of truth for "where home is". It is deliberately
	/// general, not a nav-specific landing field, so future mechanics can read it
	/// without touching navigation: respawn, camera-home, return-to-base, the
	/// base-building anchor, raid spawn origin, distance-from-home difficulty, etc.
	///
	/// The off-mesh recovery fallback in AIDecisionSystem reads this value (pushed
	/// across the engine/app boundary via setColonyOrigin) as the last-resort snap
	/// target for a colonist stranded off the navmesh.
	struct Colony {
		glm::vec2 originPosition{0.0F, 0.0F};
	};

} // namespace ecs
