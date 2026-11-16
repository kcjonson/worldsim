#pragma once

// Opening footprint geometry shared by the interim opening render (DrawingSystem),
// opening selection (SelectionSystem), and the navmesh input builder (NavInputBuilder).
// They all need the same oriented rectangle an opening occupies on its host wall, so
// the math lives here once: the band over the opening's centerline sub-span
// [t - halfW/L, t + halfW/L], offset by the wall's half-thickness. Integer
// millimeters throughout (the topology's space). The placement range, where an
// opening may sit on its wall, is here too, so the opening tool's snap, the
// validator, and the dev verb share one rule, and so is the span each opening leaves
// open along its wall (openingGaps), which the wall render, nav, and sight all cut at.

#include "construction/ConstructionWorld.h"

#include <offset/WallOffset.h>
#include <polygon/Polygon.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace construction {

	/// Half the segment's thickness preset in mm, or 0 when the preset is unknown.
	[[nodiscard]] std::int64_t halfThicknessMm(const WallSegment& wall);

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
	/// from geometry::resolveWallBands over the segment's connected wall graph, blueprint
	/// and built walls alike, the trims the wall is drawn with (a near-straight join the
	/// offsetter redraws as a step trims far less than its miter would); a segment with
	/// no thickness preset has no band and no trims. Empty when the segment or one of its
	/// vertices is missing, it has zero length, or the offsetter rejects its graph: the
	/// renderer draws such a graph as bare centerlines and nav keeps every band in it
	/// whole, so a door there would not open. Resolves the whole graph on every call.
	[[nodiscard]] std::optional<OpeningRange>
	openingRange(const ConstructionWorld& world, SegmentId segment, double widthMeters, double marginMeters);

	/// The oriented footprint of an opening as an integer-mm ring (CCW, 4 verts):
	/// the wall-thickness rectangle centered on the opening, sized to its clear
	/// width along the wall. Empty when geometry is missing or degenerate (unknown
	/// segment/type, zero-length segment, zero half-thickness).
	[[nodiscard]] geometry::Ring openingFootprint(const ConstructionWorld& world, const Opening& opening);

	/// Which of a segment's openings openingGaps reads.
	struct OpeningGapFilter {
		bool doorsOnly = false; // pathable openings only, else windows too
		bool builtOnly = false; // Built openings only, else blueprints too
	};

	/// The centerline spans a segment's openings leave open, in whole mm from its v0 end,
	/// and in step with them the opening each belongs to, in the world's opening order. An
	/// opening at t with clear width w on a segment of length L spans t*L -/+ w/2, rounded;
	/// geometry::wallSpans and wallPiecesAndOutline clamp and merge them.
	struct OpeningGaps {
		std::vector<geometry::WallGap> gaps;
		std::vector<OpeningId>		   openings;
	};
	[[nodiscard]] OpeningGaps openingGaps(const ConstructionWorld& world, const WallSegment& segment, OpeningGapFilter filter);

} // namespace construction
