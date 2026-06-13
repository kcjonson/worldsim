#pragma once

#include "../core/Vec2i64.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace geometry {

	enum class Orientation {
		CounterClockwise,
		Clockwise,
		Collinear,
	};

	// Exact orientation of the triple (a, b, c) via the sign of the cross
	// product of (b - a) and (c - a). 128-bit intermediates make this exact for
	// the full int64 coordinate range.
	Orientation orientation(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c);

	// Exact angular comparison of two direction vectors `u` and `v` measured
	// counter-clockwise from the +x axis. Returns true when u sorts strictly
	// before v. Half-plane classification plus an exact cross-product tiebreak;
	// no atan2, no floats, deterministic across platforms. Equal directions
	// (parallel, same sense) compare equivalent (neither precedes the other), so
	// callers that need a strict total order over coincident rays must add their
	// own deterministic tie-break. The zero vector is not a valid input. This is
	// the library's single angular comparator; arrangement face extraction and
	// wall-offset junction ordering both use it.
	bool angleLess(const Vec2i64& u, const Vec2i64& v);

	enum class SegmentRelation {
		Disjoint,
		ProperCrossing,	 // single interior crossing point
		EndpointTouch,	 // touch at one shared/incident endpoint, not collinear-overlapping
		CollinearOverlap // collinear and overlapping in more than a point
	};

	struct SegmentIntersection {
		SegmentRelation relation = SegmentRelation::Disjoint;

		// Set only for ProperCrossing. Computed from the rational parametric
		// solution and rounded to the nearest millimeter. This rounding is the
		// single place exactness is relinquished, matching the Clipper2-style
		// robustness model (D1/D2): classification stays exact integer math, the
		// reported point is snapped to the integer grid.
		Vec2i64 point;

		// Set only for CollinearOverlap: the shared subsegment endpoints,
		// ordered lexicographically. Both are exact (they are input vertices).
		Vec2i64 overlapStart;
		Vec2i64 overlapEnd;
	};

	// Classify how segment [a0,a1] meets [b0,b1]. Classification is exact.
	SegmentIntersection intersectSegments(const Vec2i64& a0, const Vec2i64& a1, const Vec2i64& b0, const Vec2i64& b1);

	enum class PointInPolygon {
		Inside,
		Outside,
		OnBoundary,
	};

	// Exact crossing-number point-in-polygon over a closed ring (implicit edge
	// from last vertex to first). Boundary (on a vertex or edge) is detected
	// exactly and reported as OnBoundary.
	PointInPolygon pointInPolygon(const Vec2i64& point, const std::vector<Vec2i64>& ring);

	// Exact squared distance from point p to segment [a,b], in mm^2. Returned as
	// Int128 because squared millimeter distances exceed int64 for large worlds.
	// Nullopt when the nearest point is interior to the segment: there the true
	// squared distance is rational, not an integer mm^2, so use
	// withinDistanceOfSegment for an exact threshold comparison instead.
	std::optional<Int128> squaredDistanceToSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b);

	// Exact comparison: is the point within `threshold` mm of the segment?
	// No floating point on the comparison path; the threshold is squared in
	// 128-bit and compared against the exact squared distance.
	bool withinDistanceOfSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b, std::int64_t thresholdMm);

	// Float convenience for UI readouts only. Not for exact comparisons.
	double distanceToSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b);

} // namespace geometry
