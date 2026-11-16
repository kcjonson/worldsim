#pragma once

// RectangleSnap
//
// The rectangle tool's corner snap and the rectangle two corners span (building-
// construction D13). Pure logic. Rule order, first that applies wins: Alt returns the
// raw cursor; then, while adjacency snaps are on, an existing foundation corner, an
// existing foundation edge (on an axis-aligned edge the point also rounds to the grid
// along it); then the nearest grid point. The corner and edge queries are the snap
// engine's own (nearestFoundationCorner, nearestFoundationEdge), so this tool and the
// freeform tool agree on what an existing corner is.

#include "ConstructionWorld.h"
#include "DrawingAids.h"

#include <math/Types.h>
#include <polygon/Polygon.h>

#include <optional>
#include <string>

namespace assets {
	struct SnappingConfig;
}

namespace construction {

	enum class RectangleSnapKind {
		None,	// Alt: the raw cursor
		Vertex, // an existing foundation corner
		Edge,	// a point on an existing foundation edge
		Grid,	// the nearest grid point
	};

	struct RectangleCornerSnap {
		::Foundation::Vec2 point{0.0F, 0.0F};
		RectangleSnapKind  kind = RectangleSnapKind::None;
	};

	/// Snaps one corner of the rectangle. Radii and `rectangleAdjacencySnaps` come from
	/// `snapping`; the grid spacing is `aids.rectangleGridSpacingMeters` (a spacing of
	/// zero or less leaves the coordinate unrounded).
	[[nodiscard]] RectangleCornerSnap snapRectangleCorner(
		const ConstructionWorld&	  world,
		const assets::SnappingConfig& snapping,
		const DrawingAids&			  aids,
		::Foundation::Vec2			  cursor,
		bool						  freeform
	);

	struct RectangleShape {
		geometry::Ring ring;				 // (left, bottom) first, counter-clockwise, millimetres
		float		   widthMeters = 0.0F;	 // along x
		float		   depthMeters = 0.0F;	 // along y
		std::string	   label;				 // "4 m x 3 m"
	};

	/// The axis-aligned rectangle spanned by two corners (world metres), or nothing when
	/// either side is not longer than 1 mm. The label rounds each side to the millimetre
	/// and prints it without trailing zeros.
	[[nodiscard]] std::optional<RectangleShape>
	rectangleFromCorners(::Foundation::Vec2 firstCorner, ::Foundation::Vec2 secondCorner);

} // namespace construction
