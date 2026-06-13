#include "Predicates.h"

#include <algorithm>
#include <cmath>

namespace geometry {

	namespace {

		int crossSign(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c) {
			return cross(b - a, c - a).sign();
		}

		// True when point p lies on segment [a,b], assuming p is already known to
		// be collinear with a and b. Tests the bounding-box containment exactly.
		bool onCollinearSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
			return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
				   p.y <= std::max(a.y, b.y);
		}

		// Half-plane of a direction measured CCW from +x: 0 for angles in
		// [0, pi) (the +x axis and the upper half), 1 for [pi, 2pi) (the -x axis
		// and the lower half). Splitting here lets a single cross-product sign
		// order everything within a half-plane.
		int angleHalf(const Vec2i64& d) {
			if (d.y > 0) {
				return 0;
			}
			if (d.y < 0) {
				return 1;
			}
			return d.x > 0 ? 0 : 1; // on the x-axis: +x is half 0, -x is half 1
		}

	} // namespace

	Orientation orientation(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c) {
		int s = crossSign(a, b, c);
		if (s > 0) {
			return Orientation::CounterClockwise;
		}
		if (s < 0) {
			return Orientation::Clockwise;
		}
		return Orientation::Collinear;
	}

	bool angleLess(const Vec2i64& u, const Vec2i64& v) {
		const int hu = angleHalf(u);
		const int hv = angleHalf(v);
		if (hu != hv) {
			return hu < hv;
		}
		// Same half-plane: u precedes v iff v is CCW from u, i.e. cross(u,v) > 0.
		return cross(u, v).sign() > 0;
	}

	SegmentIntersection intersectSegments(const Vec2i64& a0, const Vec2i64& a1, const Vec2i64& b0, const Vec2i64& b1) {
		SegmentIntersection result;

		const int d1 = crossSign(b0, b1, a0);
		const int d2 = crossSign(b0, b1, a1);
		const int d3 = crossSign(a0, a1, b0);
		const int d4 = crossSign(a0, a1, b1);

		// All four collinear: the segments lie on one line. Determine overlap.
		if (d1 == 0 && d2 == 0 && d3 == 0 && d4 == 0) {
			// Project onto the dominant axis to order points along the line.
			const Vec2i64 dir		 = a1 - a0;
			const bool	  useX		 = std::abs(dir.x) >= std::abs(dir.y);
			auto		  coordOf	 = [useX](const Vec2i64& p) { return useX ? p.x : p.y; };

			std::int64_t lo = std::max(std::min(coordOf(a0), coordOf(a1)), std::min(coordOf(b0), coordOf(b1)));
			std::int64_t hi = std::min(std::max(coordOf(a0), coordOf(a1)), std::max(coordOf(b0), coordOf(b1)));

			if (lo > hi) {
				result.relation = SegmentRelation::Disjoint;
				return result;
			}

			// Recover the full Vec2i64 endpoints of the overlap from the line.
			auto pointAt = [&](std::int64_t coord) -> Vec2i64 {
				if (coord == coordOf(a0)) {
					return a0;
				}
				if (coord == coordOf(a1)) {
					return a1;
				}
				if (coord == coordOf(b0)) {
					return b0;
				}
				return b1;
			};

			Vec2i64 p = pointAt(lo);
			Vec2i64 q = pointAt(hi);

			if (lo == hi) {
				result.relation = SegmentRelation::EndpointTouch;
				result.point	= p;
				return result;
			}

			result.relation		= SegmentRelation::CollinearOverlap;
			result.overlapStart = p < q ? p : q;
			result.overlapEnd	= p < q ? q : p;
			return result;
		}

		// Proper crossing: each segment straddles the other's supporting line.
		if (((d1 > 0) != (d2 > 0)) && d1 != 0 && d2 != 0 && ((d3 > 0) != (d4 > 0)) && d3 != 0 && d4 != 0) {
			// Rational parametric solution: a0 + t*(a1-a0), t = cross(b0-a0, b1-b0) / cross(a1-a0, b1-b0).
			// Numerator and denominator are exact 128-bit; the division and the
			// resulting point are rounded to the nearest millimeter. This is the
			// only inexact step (see header).
			const Vec2i64 a	   = a1 - a0;
			const Vec2i64 b	   = b1 - b0;
			const double  num  = cross(b0 - a0, b).toDouble();
			const double  den  = cross(a, b).toDouble();
			const double  t	   = num / den;

			result.relation = SegmentRelation::ProperCrossing;
			result.point	= {
				   std::llround(static_cast<double>(a0.x) + t * static_cast<double>(a.x)),
				   std::llround(static_cast<double>(a0.y) + t * static_cast<double>(a.y)),
			};
			return result;
		}

		// Touch at a single point: one endpoint lies on the other segment, but
		// the segments are not collinear-overlapping (handled above).
		if (d1 == 0 && onCollinearSegment(a0, b0, b1)) {
			result.relation = SegmentRelation::EndpointTouch;
			result.point	= a0;
			return result;
		}
		if (d2 == 0 && onCollinearSegment(a1, b0, b1)) {
			result.relation = SegmentRelation::EndpointTouch;
			result.point	= a1;
			return result;
		}
		if (d3 == 0 && onCollinearSegment(b0, a0, a1)) {
			result.relation = SegmentRelation::EndpointTouch;
			result.point	= b0;
			return result;
		}
		if (d4 == 0 && onCollinearSegment(b1, a0, a1)) {
			result.relation = SegmentRelation::EndpointTouch;
			result.point	= b1;
			return result;
		}

		result.relation = SegmentRelation::Disjoint;
		return result;
	}

	PointInPolygon pointInPolygon(const Vec2i64& point, const std::vector<Vec2i64>& ring) {
		const std::size_t n = ring.size();
		if (n < 3) {
			return PointInPolygon::Outside;
		}

		bool inside = false;
		for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
			const Vec2i64& vi = ring[i];
			const Vec2i64& vj = ring[j];

			// On-boundary: collinear with the edge and inside its bounding box.
			if (crossSign(vj, vi, point) == 0 && onCollinearSegment(point, vj, vi)) {
				return PointInPolygon::OnBoundary;
			}

			// Crossing-number ray cast along +x. A half-open vertical rule
			// (one endpoint strictly above, the other at-or-below) counts each
			// crossing once and handles rays that pass through vertices.
			const bool straddles = (vi.y > point.y) != (vj.y > point.y);
			if (straddles) {
				// Exact side test: is the edge crossing to the right of point?
				// Sign of cross(vj - vi, point - vi) gives the side; combine with
				// the upward/downward edge direction.
				const int  side		   = crossSign(vi, vj, point);
				const bool edgePointsUp = vj.y > vi.y; // vi -> vj rises in +y
				if ((side > 0) == edgePointsUp) {
					inside = !inside;
				}
			}
		}

		return inside ? PointInPolygon::Inside : PointInPolygon::Outside;
	}

	std::optional<Int128> squaredDistanceToSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
		const Vec2i64 ab = b - a;
		const Vec2i64 ap = p - a;

		// Degenerate segment: distance to the single point a.
		if (ab.x == 0 && ab.y == 0) {
			return dot(ap, ap);
		}

		const Int128 t = dot(ap, ab); // projection parameter numerator, scaled by |ab|^2
		if (t <= Int128(0)) {
			return dot(ap, ap);
		}

		const Int128 denom = dot(ab, ab); // |ab|^2
		if (t >= denom) {
			const Vec2i64 bp = p - b;
			return dot(bp, bp);
		}

		// Interior: the true squared distance is cross(ab, ap)^2 / |ab|^2, a
		// rational, not generally an exact integer mm^2. Returning nullopt keeps
		// the integer contract honest; callers that need the interior magnitude
		// use withinDistanceOfSegment, which compares exactly without dividing.
		return std::nullopt;
	}

	bool withinDistanceOfSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b, std::int64_t thresholdMm) {
		const Vec2i64 ab	   = b - a;
		const Vec2i64 ap	   = p - a;
		const Int128  threshSq = Int128::product(thresholdMm, thresholdMm);

		if (ab.x == 0 && ab.y == 0) {
			return dot(ap, ap) <= threshSq;
		}

		const Int128 t = dot(ap, ab);
		if (t <= Int128(0)) {
			return dot(ap, ap) <= threshSq;
		}

		const Int128 denom = dot(ab, ab);
		if (t >= denom) {
			const Vec2i64 bp = p - b;
			return dot(bp, bp) <= threshSq;
		}

		// Interior: perpendicular distance, dist^2 = cross(ab, ap)^2 / |ab|^2.
		// Compare exactly as cross^2 <= threshSq * |ab|^2. Both sides are products
		// of non-negative Int128 magnitudes (up to 256-bit), compared with exact
		// wide multiplication. No floating point on the comparison path.
		const Int128 crossComp = cross(ab, ap);
		return Int128::compareSquareToProduct(crossComp, threshSq, denom) <= 0;
	}

	bool closerThanToSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b, std::int64_t thresholdMm) {
		const Vec2i64 ab	   = b - a;
		const Vec2i64 ap	   = p - a;
		const Int128  threshSq = Int128::product(thresholdMm, thresholdMm);

		if (ab.x == 0 && ab.y == 0) {
			return dot(ap, ap) < threshSq;
		}

		const Int128 t = dot(ap, ab);
		if (t <= Int128(0)) {
			return dot(ap, ap) < threshSq;
		}

		const Int128 denom = dot(ab, ab);
		if (t >= denom) {
			const Vec2i64 bp = p - b;
			return dot(bp, bp) < threshSq;
		}

		const Int128 crossComp = cross(ab, ap);
		return Int128::compareSquareToProduct(crossComp, threshSq, denom) < 0;
	}

	double distanceToSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
		const Vec2i64 ab = b - a;
		const Vec2i64 ap = p - a;

		if (ab.x == 0 && ab.y == 0) {
			return std::sqrt(static_cast<double>(dot(ap, ap).toDouble()));
		}

		const double tNum	= dot(ap, ab).toDouble();
		const double denom	= dot(ab, ab).toDouble();
		const double t		= std::clamp(tNum / denom, 0.0, 1.0);
		const double projX	= static_cast<double>(a.x) + t * static_cast<double>(ab.x);
		const double projY	= static_cast<double>(a.y) + t * static_cast<double>(ab.y);
		const double dx		= static_cast<double>(p.x) - projX;
		const double dy		= static_cast<double>(p.y) - projY;
		return std::sqrt(dx * dx + dy * dy);
	}

} // namespace geometry
