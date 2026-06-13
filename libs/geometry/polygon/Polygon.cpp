#include "Polygon.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace geometry {

	namespace {

		// Strict: is the point closer than thresholdMm to the segment? Exact,
		// no floating point. Mirrors withinDistanceOfSegment but uses strict <
		// so that a gap exactly equal to the threshold is permitted (consistent
		// with minVertexSpacing's at-threshold-passes rule).
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
			return Int128::compareSquareToProduct(cross(ab, ap), threshSq, denom) < 0;
		}

	} // namespace

	Int128 signedAreaDoubled(const Ring& ring) {
		const std::size_t n = ring.size();
		if (n < 3) {
			return Int128(0);
		}
		Int128 acc(0);
		for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
			acc = acc + cross(ring[j], ring[i]);
		}
		return acc;
	}

	double signedAreaSquareMeters(const Ring& ring) {
		const double areaMm2 = signedAreaDoubled(ring).toDouble() * 0.5;
		const double mmPerM	 = static_cast<double>(kMillimetersPerMeter);
		return areaMm2 / (mmPerM * mmPerM);
	}

	Winding windingOrder(const Ring& ring) {
		const int s = signedAreaDoubled(ring).sign();
		if (s > 0) {
			return Winding::CounterClockwise;
		}
		if (s < 0) {
			return Winding::Clockwise;
		}
		return Winding::Degenerate;
	}

	void ensureCounterClockwise(Ring& ring) {
		if (windingOrder(ring) == Winding::Clockwise) {
			std::reverse(ring.begin(), ring.end());
		}
	}

	ConstraintResult isSimple(const Ring& ring) {
		const std::size_t n = ring.size();
		ConstraintResult  result;
		if (n < 3) {
			result.pass		   = false;
			result.vertexIndex = 0;
			return result;
		}

		// Duplicate vertices make the ring non-simple regardless of edge tests.
		for (std::size_t i = 0; i < n; ++i) {
			for (std::size_t k = i + 1; k < n; ++k) {
				if (ring[i] == ring[k]) {
					result.pass		   = false;
					result.vertexIndex = i;
					result.otherIndex  = k;
					return result;
				}
			}
		}

		for (std::size_t i = 0; i < n; ++i) {
			const Vec2i64& a0 = ring[i];
			const Vec2i64& a1 = ring[(i + 1) % n];
			for (std::size_t k = i + 1; k < n; ++k) {
				// Skip the same edge and the two edges adjacent to edge i
				// (sharing a vertex is allowed for adjacent edges).
				const bool adjacent = (k == i) || ((i + 1) % n == k) || ((k + 1) % n == i);
				if (adjacent) {
					continue;
				}
				const Vec2i64& b0 = ring[k];
				const Vec2i64& b1 = ring[(k + 1) % n];

				const SegmentRelation rel = intersectSegments(a0, a1, b0, b1).relation;
				if (rel != SegmentRelation::Disjoint) {
					result.pass		   = false;
					result.vertexIndex = i;
					result.otherIndex  = k;
					return result;
				}
			}
		}

		return result;
	}

	ConstraintResult minInteriorAngle(const Ring& ring, double thresholdDegrees) {
		const std::size_t n = ring.size();
		ConstraintResult  result;
		if (n < 3) {
			result.pass = false;
			return result;
		}

		double minAngle = 360.0;
		for (std::size_t i = 0; i < n; ++i) {
			const Vec2i64& prev = ring[(i + n - 1) % n];
			const Vec2i64& cur	= ring[i];
			const Vec2i64& next = ring[(i + 1) % n];

			const Vec2i64 e0 = prev - cur;
			const Vec2i64 e1 = next - cur;

			// Degenerate zero-length edge: angle is undefined, treat as failing.
			if ((e0.x == 0 && e0.y == 0) || (e1.x == 0 && e1.y == 0)) {
				result.pass		   = false;
				result.vertexIndex = i;
				result.measuredValue = 0.0;
				return result;
			}

			const double dotv	= dot(e0, e1).toDouble();
			const double crossv = cross(e0, e1).toDouble();
			const double angle	= std::abs(std::atan2(crossv, dotv)) * 180.0 / std::numbers::pi;
			if (angle < minAngle) {
				minAngle		   = angle;
				result.vertexIndex = i;
			}
		}

		result.measuredValue = minAngle;
		result.pass			 = minAngle >= thresholdDegrees;
		return result;
	}

	ConstraintResult minVertexSpacing(const Ring& ring, std::int64_t thresholdMm) {
		const std::size_t n = ring.size();
		ConstraintResult  result;
		if (n < 2) {
			result.pass = false;
			return result;
		}

		const Int128 threshSq = Int128::product(thresholdMm, thresholdMm);
		const double mmPerM	  = static_cast<double>(kMillimetersPerMeter);

		for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
			const Vec2i64 d	  = ring[i] - ring[j];
			const Int128  sq = dot(d, d);
			if (sq < threshSq) {
				result.pass			 = false;
				result.vertexIndex	 = j;
				result.otherIndex	 = i;
				result.measuredValue = std::sqrt(sq.toDouble()) / mmPerM;
				return result;
			}
		}

		return result;
	}

	ConstraintResult minEdgeClearance(const Ring& ring, std::int64_t thresholdMm) {
		const std::size_t n = ring.size();
		ConstraintResult  result;
		if (n < 4) {
			return result; // fewer than 4 vertices has no non-adjacent edge pair
		}

		const double mmPerM = static_cast<double>(kMillimetersPerMeter);

		for (std::size_t i = 0; i < n; ++i) {
			const Vec2i64& a0 = ring[i];
			const Vec2i64& a1 = ring[(i + 1) % n];
			for (std::size_t k = i + 1; k < n; ++k) {
				const bool adjacent = ((i + 1) % n == k) || ((k + 1) % n == i);
				if (adjacent) {
					continue;
				}
				const Vec2i64& b0 = ring[k];
				const Vec2i64& b1 = ring[(k + 1) % n];

				if (closerThanToSegment(a0, b0, b1, thresholdMm) || closerThanToSegment(a1, b0, b1, thresholdMm) ||
					closerThanToSegment(b0, a0, a1, thresholdMm) || closerThanToSegment(b1, a0, a1, thresholdMm)) {
					result.pass		   = false;
					result.vertexIndex = i;
					result.otherIndex  = k;
					// Report the float min distance of the four endpoint-edge pairs.
					const double d0		 = distanceToSegment(a0, b0, b1);
					const double d1		 = distanceToSegment(a1, b0, b1);
					const double d2		 = distanceToSegment(b0, a0, a1);
					const double d3		 = distanceToSegment(b1, a0, a1);
					result.measuredValue = std::min({d0, d1, d2, d3}) / mmPerM;
					return result;
				}
			}
		}

		return result;
	}

} // namespace geometry
