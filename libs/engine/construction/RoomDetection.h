#pragma once

#include "ConstructionWorld.h"

#include <core/Int128.h>
#include <core/Vec2i64.h>
#include <polygon/Polygon.h>

#include <cstdint>
#include <vector>

// Room detection (building-construction D6): derive the enclosed regions of the
// BUILT wall graph. Pure geometry, no ECS dependency -- it reads the topology
// store and returns faces, leaving entity spawning and identity persistence to
// RoomDetectionSystem. Built wall centerlines are the only enclosure input;
// foundations and blueprint walls play no part.

namespace engine::construction {

	// One enclosed face of the built-wall arrangement: a room's geometry plus the
	// wall segments bounding it. Bounded (interior) faces only; the unbounded
	// outer face is excluded. Ring is CCW integer mm.
	struct RoomFace {
		float			 area = 0.0f;				// m^2 (for display)
		geometry::Int128 areaDoubled;				// exact 2x area in mm^2 (>= 0 for bounded faces);
													// the deterministic key for identity tiebreaks
		geometry::Vec2i64	   representativePoint; // a point strictly inside (mm)
		geometry::Ring		   ring;				// boundary vertices (mm), CCW loop order
		std::vector<SegmentId> boundingSegmentIds;	// from face provenance (the wall ids)
	};

	// Enclosed regions of the built-wall graph, in the arrangement's canonical
	// face order (deterministic across platforms; safe for multiplayer). An open
	// chain or a loop with no built segments yields an empty result.
	std::vector<RoomFace> detectRooms(const ConstructionWorld& world);

} // namespace engine::construction
