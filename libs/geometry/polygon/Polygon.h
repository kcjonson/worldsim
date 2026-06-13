#pragma once

#include "../core/Int128.h"
#include "../core/Vec2i64.h"
#include "../predicates/Predicates.h"

#include <cstddef>
#include <cstdint>
#include <vector>

// Polygon constraint primitives operating on closed rings stored as
// std::vector<Vec2i64> with the closing edge implicit (last vertex != first).
// These are the editor's draw-time validation checks (building-construction
// D4): every one reports both pass/fail and measured data for UI messages.

namespace geometry {

	using Ring = std::vector<Vec2i64>;

	enum class Winding {
		CounterClockwise,
		Clockwise,
		Degenerate, // zero area
	};

	// Exact signed area times two (the integer the shoelace sum produces).
	// Positive for CCW rings, negative for CW. Exact in 128-bit.
	Int128 signedAreaDoubled(const Ring& ring);

	// Signed area in square meters, for UI readouts. Lossy float.
	double signedAreaSquareMeters(const Ring& ring);

	Winding windingOrder(const Ring& ring);

	// Reverse the ring in place if it is clockwise, leaving CCW rings untouched.
	void ensureCounterClockwise(Ring& ring);

	// Result of a constraint check: pass/fail plus enough to build a violation
	// message. `vertexIndex` points at the offending vertex (or the first vertex
	// of the offending edge); `measuredValue` is the measured quantity in the
	// constraint's natural units (degrees, meters), populated on failure.
	struct ConstraintResult {
		bool		pass		 = true;
		std::size_t vertexIndex	 = 0;
		std::size_t otherIndex	 = 0; // second edge/vertex index where the check pairs two
		double		measuredValue = 0.0;
	};

	// No two non-adjacent edges intersect; adjacent edges share only their common
	// vertex. O(n^2), fine for editor-scale rings.
	ConstraintResult isSimple(const Ring& ring);

	// Smallest interior angle (degrees) is at least the threshold. Float angle
	// from integer edge vectors; thresholds are coarse (~30 deg). Degenerate
	// zero-length edges fail with measuredValue 0.
	ConstraintResult minInteriorAngle(const Ring& ring, double thresholdDegrees);

	// Every pair of consecutive vertices is at least thresholdMm apart. Exact
	// squared-distance comparison.
	ConstraintResult minVertexSpacing(const Ring& ring, std::int64_t thresholdMm);

	// Minimum distance between any pair of non-adjacent edges is at least
	// thresholdMm: the anti-sliver clearance constraint. Exact comparison path.
	ConstraintResult minEdgeClearance(const Ring& ring, std::int64_t thresholdMm);

	// Point-in-polygon is provided by Predicates.h (pointInPolygon), which takes
	// the same std::vector<Vec2i64> a Ring aliases; included above.

} // namespace geometry
