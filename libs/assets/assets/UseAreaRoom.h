#pragma once

// Room in a use area (docs/technical/rendering/rotatable-placed-items.md 4.2, 12). A use area has room when one
// colonist fits in it: its standing region, the area less the ground no one may stand on, eroded by the colonist's
// radius, is not empty. One test, asked at validation (the def's own collider, no world), at placement, and afterwards
// (the nav mesh's open ground and the obstacles it doesn't hold). Exact vector geometry: the erosion is the area less
// every boundary edge and every obstacle swept by a disc, by the geometry library's booleans, in micrometres.

#include "assets/PartsAsset.h"

#include <boolean/RingBoolean.h>
#include <core/Vec2i64.h>

#include <glm/vec2.hpp>

#include <optional>
#include <utility>
#include <vector>

namespace assets {

	/// Ground a colonist may stand on, from the nav mesh, in millimetres: its faces and the edges where it ends (against
	/// water, walls, placed obstacles, or ground not meshed).
	struct OpenGround {
		std::vector<geometry::Ring>										faces;
		std::vector<std::pair<geometry::Vec2i64, geometry::Vec2i64>> edges;
	};

	/// Where a colonist may stand to use an item, in micrometres: disjoint pieces, each with its holes.
	struct StandingRegion {
		std::vector<geometry::RegionPolygon> pieces;

		[[nodiscard]] bool empty() const { return pieces.empty(); }
	};

	/// `area` (metres) less the `obstacles` (rings in metres, either winding) and, with `ground`, less everything off
	/// it, eroded by `radiusMeters`. Without `ground` all ground is open (validation).
	[[nodiscard]] StandingRegion
	standingRegion(const std::vector<AreaPolygon>& area, const std::vector<std::vector<glm::vec2>>& obstacles, const OpenGround* ground, float radiusMeters);

	/// The point of `piece` nearest `from`, in metres: `from` itself when it lies inside, else the nearest boundary point
	/// taken a few millimetres in where the piece is wide enough.
	[[nodiscard]] glm::vec2 nearestPointIn(const geometry::RegionPolygon& piece, glm::vec2 from);

	/// A point inside `piece`, in metres.
	[[nodiscard]] glm::vec2 pointInside(const geometry::RegionPolygon& piece);

	/// Whether `meters` lies inside `piece`.
	[[nodiscard]] bool pieceHolds(const geometry::RegionPolygon& piece, glm::vec2 meters);

} // namespace assets
