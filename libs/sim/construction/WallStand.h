#pragma once

#include <glm/vec2.hpp>

// A wall segment's band and the sides a builder stands on to work it, in meters (building-construction:
// Walls / Lifecycle). Plain values; WallSides.h works them out from the topology store.

namespace construction {

	// A wall segment's band: its centreline and half its thickness. WallCollisionSystem pushes colonists out
	// of it once the wall is built, and the wall does not complete while a colonist the player controls
	// overlaps it.
	struct WallBand {
		glm::vec2 start{0.0F, 0.0F};
		glm::vec2 end{0.0F, 0.0F};
		float	  halfThicknessMeters = 0.0F;

		// The point on the centreline nearest `position`.
		[[nodiscard]] glm::vec2 closestPoint(glm::vec2 position) const;
		// Whether a disc of `radiusMeters` at `position` overlaps the band.
		[[nodiscard]] bool overlaps(glm::vec2 position, float radiusMeters) const;
		// Where a disc of `radiusMeters` at `position` stands just clear of the band toward `outward` (a unit
		// normal of the segment), level with where it was along the wall.
		[[nodiscard]] glm::vec2 stepOut(glm::vec2 position, float radiusMeters, glm::vec2 outward) const;
	};

	// One side of a wall segment a builder may stand on, beside its midpoint; `outward` is the unit normal
	// pointing to that side.
	struct WallStand {
		glm::vec2 midpoint{0.0F, 0.0F};
		glm::vec2 outward{0.0F, 0.0F};
		float	  halfThicknessMeters = 0.0F;
		float	  halfLengthMeters = 0.0F;

		// How far off the centreline a builder of `agentRadiusMeters` stands: half the wall's thickness, his
		// radius, and kStandMarginMeters.
		[[nodiscard]] float standOffset(float agentRadiusMeters) const;
		// Where a builder of `agentRadiusMeters` stands, standOffset off the midpoint.
		[[nodiscard]] glm::vec2 standPoint(float agentRadiusMeters) const;
		// Whether `position` lies on this side of the centreline's line.
		[[nodiscard]] bool onSide(glm::vec2 position) const;
		// Whether `point`, where the mesh puts a builder of `agentRadiusMeters` for this side, serves it: on
		// this side, no farther from the stand point than standOffset, and level with the segment (not past
		// either end), so it is never across the wall's line or round a corner into the room it closes.
		[[nodiscard]] bool serves(glm::vec2 point, float agentRadiusMeters) const;

		static constexpr float kStandMarginMeters = 0.1F;
	};

} // namespace construction
