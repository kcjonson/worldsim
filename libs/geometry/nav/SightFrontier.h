#pragma once

// Player visibility's frontier (player-visibility.md 3.3, 6.3): the part of a sight shape that a
// seen record held as a PatchableMesh's ground does not already hold, as a few rings that overlap
// the record by kSeenPieceOverlapMm instead of abutting it, so the record registers what a shape
// adds rather than the whole shape.

#include "../core/Vec2i64.h"
#include "../visibility/Visibility.h"
#include "PatchableMesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace geometry::nav {

	// A piece's inner edge is pulled this far back inside the record, so the piece and the record
	// overlap rather than abut: nothing depends on two snapped edges coinciding.
	constexpr std::int64_t kSeenPieceOverlapMm = 100;

	enum class SightFrontier {
		Covered, // nothing to register
		Pieces,	 // register the pieces
		Whole,	 // register the whole shape (the one piece)
	};

	struct SightFrontierScratch {
		// A mesh edge touching side `side`'s triangle, relative to the centre.
		struct Contact {
			std::size_t side  = 0;
			double		fromX = 0.0;
			double		fromY = 0.0;
			double		toX	  = 0.0;
			double		toY	  = 0.0;
		};

		// A wedge of a side's triangle, between two rays from the centre, that adds ground: distances
		// along each ray to the nearest mesh edge (inner) and to the side (outer).
		struct Wedge {
			std::size_t side	   = 0;
			double		fromX	   = 0.0;
			double		fromY	   = 0.0;
			double		toX		   = 0.0;
			double		toY		   = 0.0;
			bool		fromVertex = false; // the ray runs to the shape's vertex
			bool		toVertex   = false;
			double		innerFrom  = 0.0;
			double		innerTo	   = 0.0;
			double		outerFrom  = 0.0;
			double		outerTo	   = 0.0;
		};

		// A ray of a run of wedges: its unit direction, the shape's vertex it runs to (the shape's
		// size when none), and the distances along it to the shape's side and to the nearest contact.
		struct Ray {
			double		unitX  = 0.0;
			double		unitY  = 0.0;
			std::size_t vertex = 0;
			double		outer  = 0.0;
			double		nearer = 0.0;
		};

		std::vector<std::int32_t>				 edgeIds;
		std::vector<std::pair<Vec2i64, Vec2i64>> meshEdges;
		std::vector<Vec2i64>					 offsets;
		std::vector<double>						 sideLengths;
		std::vector<UnwrappedAngle>				 angles;
		std::vector<UnwrappedAngle>				 highestSoFar;	 // per side, the highest angle of it and every side before
		std::vector<UnwrappedAngle>				 lowestFromHere; // per side, the lowest angle of it and every side after
		std::vector<UnwrappedAngle>				 wedgeStart;	 // per side, where its core's wedge starts and ends
		std::vector<UnwrappedAngle>				 wedgeEnd;
		std::vector<std::uint8_t>				 reached; // per side, the record's edge meets its core
		std::vector<Contact>					 contacts;
		std::vector<Wedge>						 wedges;
		std::vector<std::array<double, 2>>		 rays;
		std::vector<Ray>						 runRays;
		std::vector<Vec2i64>					 piece;
		std::vector<std::uint8_t>				 pinned;
		std::vector<Vec2i64>					 inner;
	};

	// Fan `shape` from `centre` into one triangle per side. A counter-clockwise triangle's core is
	// the part of it farther than kSeenCoverToleranceMm from its side's line, strictly inside its
	// wedge, the wedge narrowed to leave out every angle the shape steps back over (a snapped
	// shadow edge does, by under a millimetre). A core the record's edge does not meet lies wholly
	// in the record, being joined to the centre, which is on it.
	//
	// Covered: the centre is on the record and no core is met. What the record can lack of the
	// shape lies within the tolerance of a side's line, or in the wedges the shape steps back over;
	// the same answer as sightShapeCovered without its groundCovers fallback.
	//
	// Pieces: each met triangle's wedge is split by rays at every end of a record edge inside it
	// and wherever a record edge crosses the side's line or the tolerance line. Between two
	// neighbouring rays no record edge ends and record edges never cross, so the nearest one is the
	// same all the way across, and everything nearer the centre than it is on the record. A split
	// wedge adds ground where that nearest edge lies deeper than the tolerance. Each run of adding
	// wedges that share rays is one ring. Its two end rays turn outward by kSeenPieceOverlapMm at
	// the run's farthest reach; its outer chain is the shape's own boundary between them; its inner
	// chain stands kSeenPieceOverlapMm back from every record edge point near it, so the piece
	// overlaps the record on every side rather than abutting it. Inner points nearer the centre
	// than the overlap become the centre, and a run under the overlap wide is fanned from the
	// centre. Every piece is a simple ring inside the shape; the record and the pieces together
	// hold the shape, but for the tolerance strips above.
	//
	// Whole: the centre is off the record, an edge of the record passes through it, every core is
	// met, a run's rays do not meet the boundary in order, or a piece would not be a simple ring;
	// `pieces` holds the shape.
	//
	// `shape` must be simple and wind once round `centre` (simpleAround). `pieces` is cleared first.
	SightFrontier sightShapeFrontier(const PatchableMesh& record, const std::vector<Vec2i64>& shape, const Vec2i64& centre, SightFrontierScratch& scratch,
									 std::vector<std::vector<Vec2i64>>& pieces);

} // namespace geometry::nav
