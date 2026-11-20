#pragma once

#include "ConstructionWorld.h"

#include <glm/vec2.hpp>

#include <optional>
#include <vector>

// Where a builder stands to work a wall segment, and the band a wall holds clear before it completes
// (building-construction: Walls / Lifecycle). Geometry over the topology store and the construction
// config: no ECS, no nav.

namespace construction {

	// A wall segment's band: its centreline in meters and half its thickness. WallCollisionSystem pushes
	// colonists out of it once the wall is built, and the wall does not complete while one overlaps it.
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

	// nullopt when a vertex is missing or the segment's thickness preset is unknown or has no thickness.
	[[nodiscard]] std::optional<WallBand> wallBand(const ConstructionWorld& world, const WallSegment& segment);

	// One side of a wall segment a builder may stand on, beside its midpoint; `outward` is the unit normal
	// pointing to that side.
	struct WallStand {
		glm::vec2 midpoint{0.0F, 0.0F};
		glm::vec2 outward{0.0F, 0.0F};
		float	  halfThicknessMeters = 0.0F;

		// Where a builder of `agentRadiusMeters` stands: off the centreline by half the wall's thickness, his
		// radius, and kStandMarginMeters.
		[[nodiscard]] glm::vec2 standPoint(float agentRadiusMeters) const;
		// Whether `position` lies on this side of the centreline.
		[[nodiscard]] bool onSide(glm::vec2 position) const;

		static constexpr float kStandMarginMeters = 0.1F;
	};

	// The sides of `segment` a builder may stand on so that finishing it leaves him a way out. Both sides,
	// unless its two ends are already joined through built walls, so that completing it closes a loop. Then
	// each side is judged by the region it opens onto once the segment is built: open ground, or a room
	// joined to open ground through built doors (pathable openings) room to room, is a way out; a room whose
	// only openings are windows or door blueprints is not. The sides with a way out are given; when neither
	// has one, both are, and the builder takes the nearer. Left (+90 degrees of v0 to v1) comes first.
	// Empty for an unknown segment.
	[[nodiscard]] std::vector<WallStand> wallStands(const ConstructionWorld& world, SegmentId segment);

} // namespace construction
