#pragma once

// Opening footprint geometry shared by the interim opening render (DrawingSystem),
// opening selection (SelectionSystem), and the navmesh input builder (NavInputBuilder).
// They all need the same oriented rectangle an opening occupies on its host wall, so
// the math lives here once: the band over the opening's centerline sub-span
// [t - halfW/L, t + halfW/L], offset by the wall's half-thickness. Integer
// millimeters throughout (the topology's space). The placement range, where an
// opening may sit on its wall, is here too, so the opening tool's snap, the
// validator, and the dev verb share one rule.

#include "construction/ConstructionWorld.h"

#include <polygon/Polygon.h>

#include <optional>

namespace construction {

	/// Where along a wall segment an opening of one width may have its centre: every
	/// part of it at least the margin clear of each end of the segment's drawn band,
	/// which a corner or junction cuts back from the vertex (far at an acute corner,
	/// not at all at a free end or a straight join). t is from v0 toward v1.
	struct OpeningRange {
		double lengthMeters = 0.0;
		double trimAtV0Meters = 0.0;
		double trimAtV1Meters = 0.0;
		float  minT = 0.0F;
		float  maxT = 0.0F;

		/// False when the trimmed band is too short to hold the opening and both margins.
		[[nodiscard]] bool fits() const { return minT <= maxT; }
	};

	/// The range for an opening `widthMeters` wide on `segment`, the band trims coming
	/// from geometry::resolveJunction at each of the segment's two vertices over every
	/// wall meeting there (blueprint or built, as the walls are drawn). Empty when the
	/// segment or one of its vertices is missing, or the segment has zero length.
	[[nodiscard]] std::optional<OpeningRange>
	openingRange(const ConstructionWorld& world, SegmentId segment, double widthMeters, double marginMeters);

	/// The oriented footprint of an opening as an integer-mm ring (CCW, 4 verts):
	/// the wall-thickness rectangle centered on the opening, sized to its clear
	/// width along the wall. Empty when geometry is missing or degenerate (unknown
	/// segment/type, zero-length segment, zero half-thickness).
	[[nodiscard]] geometry::Ring openingFootprint(const ConstructionWorld& world, const Opening& opening);

} // namespace construction
