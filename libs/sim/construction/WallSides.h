#pragma once

#include "ConstructionWorld.h"
#include "RoomDetection.h"
#include "WallStand.h"

#include <glm/vec2.hpp>

#include <cstddef>
#include <optional>
#include <vector>

// Which sides of a wall segment its builders may stand on, and whom completing it would shut in
// (building-construction: Walls / Lifecycle). Geometry over the topology store and the construction
// config: no ECS, no nav.

namespace construction {

	// nullopt when a vertex is missing or the segment's thickness preset is unknown or has no thickness.
	[[nodiscard]] std::optional<WallBand> wallBand(const ConstructionWorld& world, const WallSegment& segment);

	// What completing a segment closes. Nothing, unless its two ends are already joined through built walls;
	// then each side is judged by the region it opens onto once the segment is built: open ground, or a room
	// joined to open ground through built doors (pathable openings) room to room, is a way out; a room whose
	// only openings are windows or door blueprints is not.
	class WallClosure {
	  public:
		WallClosure(const ConstructionWorld& world, SegmentId segment);

		// The sides a builder may stand on: the sides with a way out, or both when both or neither have one
		// (the builder takes the nearer). Left (+90 degrees of v0 to v1) comes first. Empty for an unknown
		// segment.
		[[nodiscard]] const std::vector<WallStand>& stands() const { return sides; }
		// Whether `position` lies in the region completing the segment would shut in: the side with no way out,
		// when the other side has one. Another sealed room elsewhere is not this wall's.
		[[nodiscard]] bool shutsIn(glm::vec2 position) const;

	  private:
		std::vector<WallStand>	 sides;
		bool					 oneSideOut = false;
		std::size_t				 shutRoot = 0; // the region root of the side with no way out, when oneSideOut
		std::vector<RoomFace>	 rooms;
		std::vector<std::size_t> regionRoots; // per room, then open ground last: the region it joins through doors
	};

	// WallClosure(world, segment).stands().
	[[nodiscard]] std::vector<WallStand> wallStands(const ConstructionWorld& world, SegmentId segment);

} // namespace construction
