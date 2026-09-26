#include "Polygon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <numeric>
#include <optional>
#include <utility>
#include <vector>

namespace geometry {

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

	namespace {

		using IndexPair = std::pair<std::size_t, std::size_t>;

		// The lexicographically first (i, k), i < k, with ring[i] == ring[k].
		std::optional<IndexPair> firstDuplicateVertex(const Ring& ring) {
			const std::size_t		 n = ring.size();
			std::vector<std::size_t> order(n);
			std::iota(order.begin(), order.end(), std::size_t{0});
			std::sort(order.begin(), order.end(), [&ring](std::size_t a, std::size_t b) {
				return ring[a] != ring[b] ? ring[a] < ring[b] : a < b;
			});
			std::optional<IndexPair> first;
			for (std::size_t g = 0; g < n;) {
				std::size_t end = g + 1;
				while (end < n && ring[order[end]] == ring[order[g]]) {
					++end;
				}
				if (end - g >= 2 && (!first || order[g] < first->first)) {
					first = IndexPair{order[g], order[g + 1]};
				}
				g = end;
			}
			return first;
		}

		struct EdgeBox {
			std::int64_t minX;
			std::int64_t minY;
			std::int64_t maxX;
			std::int64_t maxY;
		};

		// The lexicographically first pair of non-adjacent edges (i, k), i < k, that
		// intersect or touch: what an all-pairs scan in index order reports, found
		// from a uniform grid over the edges' bounding boxes. A pair is tested once,
		// in the cell holding the low corner of the overlap of its two boxes.
		std::optional<IndexPair> firstTouchingEdgePair(const Ring& ring) {
			const std::size_t	 n = ring.size();
			std::vector<EdgeBox> boxes(n);
			EdgeBox				 bounds{ring[0].x, ring[0].y, ring[0].x, ring[0].y};
			std::int64_t		 lengthSum = 0;
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64& a = ring[i];
				const Vec2i64& b = ring[(i + 1) % n];
				boxes[i]		 = {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
				bounds			 = {std::min(bounds.minX, boxes[i].minX), std::min(bounds.minY, boxes[i].minY),
									std::max(bounds.maxX, boxes[i].maxX), std::max(bounds.maxY, boxes[i].maxY)};
				lengthSum += std::max(boxes[i].maxX - boxes[i].minX, boxes[i].maxY - boxes[i].minY);
			}

			// Cells about two mean edges wide, but never so small that the grid
			// outgrows ~4 cells per edge over the ring's bounds.
			const double	   spanX	= static_cast<double>(bounds.maxX - bounds.minX + 1);
			const double	   spanY	= static_cast<double>(bounds.maxY - bounds.minY + 1);
			const auto		   areaCell = static_cast<std::int64_t>(std::ceil(std::sqrt(spanX * spanY / (4.0 * static_cast<double>(n)))));
			const std::int64_t cell		= std::max({std::int64_t{1}, 2 * lengthSum / static_cast<std::int64_t>(n), areaCell});
			const auto		   cols		= static_cast<std::uint64_t>((bounds.maxX - bounds.minX) / cell + 1);
			auto			   cellKey	= [&](std::int64_t cx, std::int64_t cy) {
				   return static_cast<std::uint64_t>(cy) * cols + static_cast<std::uint64_t>(cx);
			};
			auto cellOf = [&](std::int64_t v, std::int64_t lo) { return (v - lo) / cell; };

			std::vector<std::pair<std::uint64_t, std::size_t>> entries;
			entries.reserve(n * 4);
			for (std::size_t i = 0; i < n; ++i) {
				const EdgeBox& box = boxes[i];
				for (std::int64_t cy = cellOf(box.minY, bounds.minY); cy <= cellOf(box.maxY, bounds.minY); ++cy) {
					for (std::int64_t cx = cellOf(box.minX, bounds.minX); cx <= cellOf(box.maxX, bounds.minX); ++cx) {
						entries.emplace_back(cellKey(cx, cy), i);
					}
				}
			}
			std::sort(entries.begin(), entries.end());

			std::optional<IndexPair> first;
			for (std::size_t g = 0; g < entries.size();) {
				std::size_t end = g + 1;
				while (end < entries.size() && entries[end].first == entries[g].first) {
					++end;
				}
				for (std::size_t p = g; p < end; ++p) {
					for (std::size_t q = p + 1; q < end; ++q) {
						const std::size_t i = entries[p].second; // entries sort by edge within a cell: i < k
						const std::size_t k = entries[q].second;
						// Adjacent edges share a vertex by construction.
						if (k == i + 1 || (i == 0 && k == n - 1)) {
							continue;
						}
						if (first && IndexPair{i, k} >= *first) {
							continue;
						}
						const EdgeBox& a = boxes[i];
						const EdgeBox& b = boxes[k];
						if (a.maxX < b.minX || b.maxX < a.minX || a.maxY < b.minY || b.maxY < a.minY) {
							continue;
						}
						const std::uint64_t home =
							cellKey(cellOf(std::max(a.minX, b.minX), bounds.minX), cellOf(std::max(a.minY, b.minY), bounds.minY));
						if (home != entries[g].first) {
							continue;
						}
						if (intersectSegments(ring[i], ring[(i + 1) % n], ring[k], ring[(k + 1) % n]).relation != SegmentRelation::Disjoint) {
							first = IndexPair{i, k};
						}
					}
				}
				g = end;
			}
			return first;
		}

	} // namespace

	ConstraintResult isSimple(const Ring& ring) {
		const std::size_t n = ring.size();
		ConstraintResult  result;
		if (n < 3) {
			result.pass		   = false;
			result.vertexIndex = 0;
			return result;
		}

		// Duplicate vertices make the ring non-simple regardless of edge tests.
		if (const std::optional<IndexPair> dup = firstDuplicateVertex(ring)) {
			result.pass		   = false;
			result.vertexIndex = dup->first;
			result.otherIndex  = dup->second;
			return result;
		}

		if (const std::optional<IndexPair> hit = firstTouchingEdgePair(ring)) {
			result.pass		   = false;
			result.vertexIndex = hit->first;
			result.otherIndex  = hit->second;
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
