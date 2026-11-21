#pragma once

#include <polygon/Polygon.h>

#include <glm/vec2.hpp>

#include <array>
#include <vector>

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

	// One room of the region a closing wall would shut in (construction::RoomFace's ring and holes).
	struct ShutRoom {
		geometry::Ring				ring;
		std::vector<geometry::Ring> holes;
	};

	// One side of a wall segment a builder may stand on, beside its midpoint; `outward` is the unit normal
	// pointing to that side.
	struct WallStand {
		glm::vec2 midpoint{0.0F, 0.0F};
		glm::vec2 outward{0.0F, 0.0F};
		float	  halfThicknessMeters = 0.0F;
		float	  halfLengthMeters = 0.0F;
		// The rooms completing the wall would shut in, when it closes one with no way out; no stand point lies
		// in them. Empty otherwise.
		std::vector<ShutRoom> shutRegion;

		// How far off the centreline a builder of `agentRadiusMeters` stands: half the wall's thickness, his
		// radius, and kStandMarginMeters.
		[[nodiscard]] float standOffset(float agentRadiusMeters) const;
		// Where a builder of `agentRadiusMeters` stands, standOffset off the centreline at `alongMeters` from
		// the midpoint toward one end.
		[[nodiscard]] glm::vec2 standPoint(float agentRadiusMeters, float alongMeters = 0.0F) const;
		// The stand points a builder tries, in order: the middle, then toward each end (kProbeFractions of
		// halfLengthMeters, held to standOffset short of either end, so a short segment's are all its middle).
		[[nodiscard]] std::array<glm::vec2, 5> standPoints(float agentRadiusMeters) const;
		// Whether `position` lies on this side of the centreline's line.
		[[nodiscard]] bool onSide(glm::vec2 position) const;
		// Whether `point` lies in shutRegion.
		[[nodiscard]] bool shutIn(glm::vec2 point) const;
		// Whether `point`, where the mesh puts a builder of `agentRadiusMeters` for the stand point `stand`,
		// serves this side: on it, no farther from `stand` than standOffset, level with the segment (not past
		// either end), and not in the region the wall would shut in.
		[[nodiscard]] bool serves(glm::vec2 point, glm::vec2 stand, float agentRadiusMeters) const;

		static constexpr float				  kStandMarginMeters = 0.1F;
		static constexpr std::array<float, 5> kProbeFractions{0.0F, -0.5F, 0.5F, -0.85F, 0.85F};
	};

} // namespace construction
