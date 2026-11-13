#pragma once

// Player visibility's covered test (player-visibility.md 3.3): whether a sight shape would add
// anything wider than kSeenCoverToleranceMm to a seen record held as a PatchableMesh's ground.
// It reads the record's edge near the shape, never the rings registered there, so its cost does
// not grow with how many rings overlap the spot.

#include "../core/Vec2i64.h"
#include "../visibility/Visibility.h"
#include "PatchableMesh.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace geometry::nav {

	// Along a wall, a new shape's side and the record's edge are both hits snapped to the millimetre
	// from different rays, so they lie within about a millimetre of each other and cross. The test
	// lets the record's edge reach this far into a shape before it calls the shape uncovered.
	constexpr std::int64_t kSeenCoverToleranceMm = 5;

	struct SightCoverScratch {
		std::vector<std::int32_t>				 edgeIds;
		std::vector<std::pair<Vec2i64, Vec2i64>> meshEdges;
		std::vector<Vec2i64>					 offsets;
		std::vector<double>						 sideLengths;
		std::vector<UnwrappedAngle>				 angles;
		std::vector<UnwrappedAngle>				 highestSoFar;	 // per side, the highest angle of it and every side before
		std::vector<UnwrappedAngle>				 lowestFromHere; // per side, the lowest angle of it and every side after
		std::vector<UnwrappedAngle>				 wedgeStart;	 // per side, where its core's wedge starts and ends
		std::vector<UnwrappedAngle>				 wedgeEnd;
		bool									 readRings = false; // the last answer was groundCovers'
	};

	// Fan `shape` from `centre` into one triangle per side. A counter-clockwise triangle's core is
	// the part of it farther than kSeenCoverToleranceMm from its side's line, strictly inside its
	// wedge, the wedge narrowed to leave out every angle the shape steps back over (a snapped
	// shadow edge does, by under a millimetre). The shape is covered when the centre is on the
	// record's ground and no edge of the ground meets any core. The cores and the centre are one
	// connected piece, so they then lie wholly in the ground: what the record can lack of a covered
	// shape lies within the tolerance of a side's line, or in the wedges the shape steps back
	// over. The mesh's edge is snapped to the millimetre, so where the shape's own side runs along
	// a ray from the centre (a shadow edge) and the record's edge runs along it too, the mesh edge
	// can stray a millimetre into a core. So where the mesh edge meets a core only within the
	// tolerance of a ray, the answer is groundCovers' instead, exact and costlier. A shape the
	// ground holds exactly is always covered. `shape` must be simple and wind once round
	// `centre` (simpleAround).
	bool sightShapeCovered(const PatchableMesh& record, const std::vector<Vec2i64>& shape, const Vec2i64& centre, SightCoverScratch& scratch);

} // namespace geometry::nav
