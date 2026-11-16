#pragma once

#include "ConstructionWorld.h"

#include <core/Int128.h>
#include <core/Vec2i64.h>
#include <polygon/Polygon.h>
#include <predicates/Predicates.h>

#include <cstdint>
#include <vector>

// Room detection (building-construction D6): derive the enclosed regions of the
// BUILT wall graph. Pure geometry, no ECS dependency -- it reads the topology
// store and returns faces, leaving entity spawning and identity persistence to
// RoomDetectionSystem. Built wall centerlines are the only enclosure input;
// foundations and blueprint walls play no part.

namespace construction {

	// One enclosed face of the built-wall arrangement: a room's geometry plus the
	// wall segments bounding it. Bounded (interior) faces only; the unbounded
	// outer face is excluded. A room is its outer ring minus its holes: each hole
	// is the outline of a free-standing wall group inside the room (a room in a
	// room), attached to the innermost room around it only.
	struct RoomFace {
		float			 area = 0.0f;				// m^2 net of holes (for display)
		geometry::Int128 areaDoubled;				// exact 2x net area in mm^2 (>= 0 for bounded faces);
													// the deterministic key for identity tiebreaks
		geometry::Vec2i64	   representativePoint; // strictly inside the ring, outside every hole (mm); unverified if the scan found none
		geometry::Ring		   ring;				// outer boundary vertices (mm), CCW loop order
		std::vector<geometry::Ring> holes;			// hole boundaries (mm), each CW, in canonical face order
		std::vector<SegmentId> boundingSegmentIds;	// from the outer ring's face provenance (the wall ids)
	};

	// Enclosed regions of the built-wall graph, in the arrangement's canonical
	// face order (deterministic across platforms; safe for multiplayer). An open
	// chain or a loop with no built segments yields an empty result.
	std::vector<RoomFace> detectRooms(const ConstructionWorld& world);

	// Exact containment of `point` in a room: Inside when strictly inside `ring` and
	// strictly outside every hole, OnBoundary when on the ring or on any hole's
	// boundary, Outside otherwise (including strictly inside a hole).
	[[nodiscard]] geometry::PointInPolygon pointInRoom(const geometry::Vec2i64&		point,
													   const geometry::Ring&			ring,
													   const std::vector<geometry::Ring>& holes);

} // namespace construction
