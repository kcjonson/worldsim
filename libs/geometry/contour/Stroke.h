#pragma once

#include "../core/Vec2d.h"
#include "../polygon/Polygon.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Ribbon stroking for river channels (terrain-polygons D7 step 6). Like
// sampleCatmullRom, every bank point depends only on its own centerline point
// and that point's two neighbors, so two chunks stroking overlapping pieces of
// one centerline get bit-identical bank vertices wherever both pieces have the
// point and both of its neighbors.

namespace geometry {

	enum class StrokeCap : std::uint8_t {
		Round, // the channel's true end
		Butt,  // a cut (fordability split): a straight edge between the two bank points
	};

	struct StrokeArgs {
		std::span<const Vec2d>	centerline;	  // world meters, >= 2 points, consecutive points distinct
		std::span<const double> leftOffsetM;  // per point, >= 0: left bank's distance from the centerline
		std::span<const double> rightOffsetM; // per point, >= 0
		StrokeCap				startCap	= StrokeCap::Round;
		StrokeCap				endCap		= StrokeCap::Round;
		double					capSpacingM = 0.5; // arc spacing of round-cap vertices, > 0
		// Unit left normal to offset the first / last point along instead of its
		// single segment's. Two pieces of one centerline cut at a shared point pass
		// that point's strokeNormal on the full centerline to both, so their butt
		// edges land on bit-identical vertices.
		std::optional<Vec2d> startNormal;
		std::optional<Vec2d> endNormal;
	};

	// The ribbon around a centerline as a CCW ring in integer mm (each coordinate
	// llround(meters * 1000)). The offset direction at a point is the unit normal
	// of the bisector of its two adjacent segment directions (an end point uses its
	// single segment); an exact reversal, where the bisector vanishes, takes the
	// incoming segment's normal.
	//
	// Layout: the right bank forward, the end cap, the left bank backward, then the
	// start cap. ring[0] is always the right bank's first point, and at a Butt end
	// the cut is the edge between the two bank points (at the start, the closing
	// edge from the last vertex back to ring[0]).
	//
	// A Round cap is two quarter-ellipses sharing the end point as center: the
	// forward semi-axis is the mean of the two offsets there, and each side's
	// sideways semi-axis is that side's offset, so the cap meets both banks exactly
	// and is a half-disc when they are equal. Its vertices sit at equal angle steps
	// of about capSpacingM of arc (at least two steps).
	//
	// Consecutive duplicates left by quantization are collapsed. Self-intersections
	// are not repaired: the ring is simple only if each offset stays under the
	// local radius of curvature (localRadiusOfCurvatureM) on its bank's inner side
	// and the centerline itself does not come back within the ribbon's width. The
	// caller keeps that invariant and validates with isSimple.
	Ring strokePolyline(const StrokeArgs& args);

	// The unit left normal strokePolyline offsets point i along: the normal of the
	// bisector of its two adjacent segment directions, from centerline[i - 1],
	// centerline[i], and centerline[i + 1] only.
	Vec2d strokeNormal(std::span<const Vec2d> centerline, std::size_t i);

	// The quantized bank point strokePolyline places at `center`, offset along
	// the unit left normal `normal` by `leftwardM` (negative for the right bank).
	// A caller that cuts a centerline names the cut's vertices with it, bit for bit.
	Vec2i64 strokeBankPoint(const Vec2d& center, const Vec2d& normal, double leftwardM);

	// Circumradius of three points in meters: the radius of curvature a polyline
	// has at `cur`. +infinity for a straight run (collinear, cur between the
	// others), 0 for a collinear reversal, where the polyline doubles back.
	double localRadiusOfCurvatureM(const Vec2d& prev, const Vec2d& cur, const Vec2d& next);

} // namespace geometry
