#pragma once

#include "../core/Vec2i64.h"
#include "../polygon/Polygon.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Wall-band offsetting and junction trimming (building-construction D8: "Wall
// bands and junctions"). A wall centerline segment becomes a rectangular band
// offset by its half-thickness; where segments meet at a shared vertex the
// bands are trimmed back and the gap is filled by a junction polygon so the
// union tiles the wall area with no overlap and no gap. Overlap matters because
// translucent blueprint rendering double-draws any overlapping region (D8), so
// trimmed tiling is the plan of record, not naive band overlap.
//
// All geometry is integer millimeters (Vec2i64). The only inexactness is the
// rounding of perpendicular offsets and miter apexes to the nearest mm; the
// tiling guarantee is exact because the junction polygon reuses the exact
// rounded band corner points rather than recomputing them.

namespace geometry {

	// A wall centerline segment with its per-segment half-thickness. Thickness is
	// per segment (presets like 0.2 m / 0.4 m -> 100/200 mm half), so two segments
	// at one junction may differ. `a` and `b` are the centerline endpoints.
	struct WallSegment {
		Vec2i64		 a;
		Vec2i64		 b;
		std::int64_t halfThicknessMm = 0;
	};

	// Outcome of a band/junction build. `ok == false` means an input violated the
	// editor invariants the offsetter relies on (zero-length segment, a produced
	// ring that is degenerate or self-intersecting). Per the architecture's
	// reject-don't-repair rule (D4) the offsetter never tries to fix such input.
	enum class OffsetStatus {
		Ok,
		ZeroLengthSegment,
		DegenerateRing,	  // a produced ring had zero area or fewer than 3 vertices
		NonSimpleRing,	  // a produced ring self-intersected
		TrimOverrunsSegment // cutbacks from both ends exceed the segment length
	};

	// The unextended rectangular band of a centerline a->b offset by half
	// thickness on each side. CCW, 4 vertices, flat caps at a and b. The
	// perpendicular offset h * leftNormal(dir) / |dir| is rounded to the nearest
	// millimeter; for offsets that are not integer (most angles) this is the one
	// place exactness is given up (documented on the function).
	Ring band(const Vec2i64& a, const Vec2i64& b, std::int64_t halfThicknessMm);

	// The band of a->b with each end cut back along the centerline: trimAtA mm
	// removed from the `a` end, trimAtB mm from the `b` end. Cutback corners are
	// rounded to the nearest mm. Returns false if the two cutbacks together meet
	// or exceed the segment length (the band would vanish or invert).
	bool trimmedBand(const Vec2i64& a, const Vec2i64& b, std::int64_t halfThicknessMm, std::int64_t trimAtA,
					 std::int64_t trimAtB, Ring& out);

	// One segment incident to a junction, expressed from the junction vertex'
	// point of view: `direction` is the other endpoint (the centerline runs from
	// the junction vertex toward `direction`), `halfThicknessMm` its half
	// thickness. `index` is the caller's identifier carried through to the result
	// so callers can map trim distances back to their own segment list.
	struct IncidentSegment {
		Vec2i64		 direction;
		std::int64_t halfThicknessMm = 0;
		std::size_t	 index			 = 0;
	};

	// Per-incident-segment cutback distance, paired with the caller index.
	struct JunctionTrim {
		std::size_t	 index		= 0;
		std::int64_t trimMm = 0;
	};

	// Resolution of a single junction: the fill polygon over the junction area
	// and the cutback distance each incident segment's band needs so the bands
	// and this polygon tile with no overlap and no gap.
	struct JunctionResolution {
		OffsetStatus			  status = OffsetStatus::Ok;
		Ring					  polygon; // CCW; empty for degree 1 (free end) and degree 2 straight runs
		std::vector<JunctionTrim> trims;
	};

	// Resolve a junction. `vertex` is the shared point; `incidents` are its
	// segments with directions pointing away from `vertex`. Degree handling:
	//   1: free end, no polygon, no trim (flat cap is the band end itself).
	//   2 straight (~180 deg): no polygon, no trim; the two bands abut cleanly.
	//   2 corner: miter wedge within `miterLimit` x half-thickness, else a squared
	//             bevel (Clipper2 concept, D2). Trims clear the wedge.
	//   3+: bevel/fan polygon over the incident bands' trimmed corner points.
	// `miterLimit` is the max miter length as a multiple of the larger half
	// thickness; beyond it the join squares off. The 30 deg minimum corner angle
	// (design spec) bounds the natural miter at ~3.9x, so the limit only bites for
	// pathological-but-permitted inputs.
	JunctionResolution resolveJunction(const Vec2i64& vertex, const std::vector<IncidentSegment>& incidents,
									   double miterLimit);

	// Full result of resolving a small wall-graph fragment.
	struct WallBands {
		OffsetStatus	  status = OffsetStatus::Ok;
		std::vector<Ring> bands;	 // one trimmed band per input segment, same order
		std::vector<Ring> junctions; // one polygon per junction that produced one
	};

	// Orchestrate band + junction resolution over a list of segments that share
	// endpoint vertices. Junctions are derived by grouping segments with an equal
	// endpoint (exact Vec2i64 ==, the whole point of integer quantization).
	// Every output ring is validated: simple, CCW, non-zero area; any degenerate
	// result sets a failing status and returns (reject-don't-repair, D4). After
	// validation each ring is simplified (collinear-vertex collapse, sub-mm
	// sliver drop) per the Clipper2 post-op concept (D2).
	WallBands resolveWallBands(const std::vector<WallSegment>& segments, double miterLimit);

	// Simplification pass (Clipper2 concept, D2): drop a vertex that is collinear
	// with its neighbors, and drop a vertex within `epsilonMm` of the segment
	// between its neighbors (sub-mm slivers from rounding). In place.
	void simplifyRing(Ring& ring, std::int64_t epsilonMm);

	constexpr double	   kDefaultMiterLimit	 = 4.0;
	constexpr std::int64_t kDefaultSimplifyEpsMm = 1;

} // namespace geometry
