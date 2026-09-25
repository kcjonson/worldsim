#include "RingPins.h"

#include "ContourDetail.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <utility>

namespace geometry {

	namespace {

		// A crossing inserted into edge a->b at parameter num / den (den > 0).
		struct EdgeCrossing {
			Vec2i64		 point;
			std::int64_t num = 0;
			std::int64_t den = 1;
		};

		EdgeCrossing makeCrossing(Vec2i64 point, std::int64_t num, std::int64_t den) {
			if (den < 0) {
				num = -num;
				den = -den;
			}
			return {point, num, den};
		}

		// Point where segment a-b meets the line x = lineX (vertical) or y = lineY.
		// Computed from the lexicographically smaller endpoint so it does not depend
		// on the edge's direction.
		Vec2i64 pointOnVertical(Vec2i64 a, Vec2i64 b, std::int64_t lineX) {
			const Vec2i64 p = std::min(a, b);
			const Vec2i64 q = std::max(a, b);
			return {lineX, p.y + contour_detail::roundDivHalfUp(Int128::product(lineX - p.x, q.y - p.y), q.x - p.x)};
		}

		Vec2i64 pointOnHorizontal(Vec2i64 a, Vec2i64 b, std::int64_t lineY) {
			const Vec2i64 p	  = std::min(a, b);
			const Vec2i64 q	  = std::max(a, b);
			std::int64_t  num = lineY - p.y;
			std::int64_t  den = q.y - p.y;
			if (den < 0) {
				num = -num;
				den = -den;
			}
			return {p.x + contour_detail::roundDivHalfUp(Int128::product(num, q.x - p.x), den), lineY};
		}

		bool strictlyBetween(std::int64_t v, std::int64_t a, std::int64_t b) { return std::min(a, b) < v && v < std::max(a, b); }

		bool onAnyLine(Vec2i64 v, std::span<const std::int64_t> xLinesMm, std::span<const std::int64_t> yLinesMm) {
			return std::find(xLinesMm.begin(), xLinesMm.end(), v.x) != xLinesMm.end() ||
				   std::find(yLinesMm.begin(), yLinesMm.end(), v.y) != yLinesMm.end();
		}

		double edgeLength(Vec2i64 a, Vec2i64 b) {
			const double dx = static_cast<double>(b.x - a.x);
			const double dy = static_cast<double>(b.y - a.y);
			return std::sqrt(dx * dx + dy * dy);
		}

		Vec2i64 lerpRounded(Vec2i64 a, Vec2i64 b, double t) {
			return {
				std::llround(static_cast<double>(a.x) + static_cast<double>(b.x - a.x) * t),
				std::llround(static_cast<double>(a.y) + static_cast<double>(b.y - a.y) * t)
			};
		}

	} // namespace

	std::vector<bool>
	pinAxisLineCrossings(Ring& ring, std::span<const std::int64_t> xLinesMm, std::span<const std::int64_t> yLinesMm) {
		const std::size_t n = ring.size();
		Ring			  out;
		out.reserve(n);
		std::vector<EdgeCrossing> crossings;
		for (std::size_t i = 0; i < n; ++i) {
			const Vec2i64 a = ring[i];
			const Vec2i64 b = ring[(i + 1) % n];
			out.push_back(a);

			crossings.clear();
			for (const std::int64_t lineX : xLinesMm) {
				if (strictlyBetween(lineX, a.x, b.x)) {
					crossings.push_back(makeCrossing(pointOnVertical(a, b, lineX), lineX - a.x, b.x - a.x));
				}
			}
			for (const std::int64_t lineY : yLinesMm) {
				if (strictlyBetween(lineY, a.y, b.y)) {
					crossings.push_back(makeCrossing(pointOnHorizontal(a, b, lineY), lineY - a.y, b.y - a.y));
				}
			}
			// Order along a->b by exact parameter.
			std::sort(crossings.begin(), crossings.end(), [](const EdgeCrossing& l, const EdgeCrossing& r) {
				return Int128::product(l.num, r.den) < Int128::product(r.num, l.den);
			});
			for (const EdgeCrossing& c : crossings) {
				// A line pair crossed at their intersection yields one point twice.
				if (c.point != out.back()) {
					out.push_back(c.point);
				}
			}
		}
		ring = std::move(out);

		std::vector<bool> pinned(ring.size());
		for (std::size_t i = 0; i < ring.size(); ++i) {
			pinned[i] = onAnyLine(ring[i], xLinesMm, yLinesMm);
		}
		return pinned;
	}

	void resampleRing(Ring& ring, std::int64_t spacingMm, std::vector<bool>& pinned) {
		assert(pinned.size() == ring.size());
		assert(spacingMm > 0);
		const std::size_t n = ring.size();
		if (n < 3) {
			return;
		}

		std::vector<std::size_t> anchors;
		for (std::size_t i = 0; i < n; ++i) {
			if (pinned[i]) {
				anchors.push_back(i);
			}
		}
		const bool singleRun = anchors.size() <= 1;
		if (anchors.empty()) {
			anchors.push_back(0);
		}

		Ring			  out;
		std::vector<bool> outPinned;
		for (std::size_t r = 0; r < anchors.size(); ++r) {
			const std::size_t start = anchors[r];
			const std::size_t end	= anchors[(r + 1) % anchors.size()];
			// Run vertices start .. end walking forward; a single anchor runs the
			// whole ring back to itself.
			const std::size_t steps = singleRun ? n : (end + n - start) % n;

			double runLength = 0.0;
			for (std::size_t k = 0; k < steps; ++k) {
				runLength += edgeLength(ring[(start + k) % n], ring[(start + k + 1) % n]);
			}
			const std::int64_t rounded	= std::llround(runLength / static_cast<double>(spacingMm));
			const std::int64_t segments = std::max<std::int64_t>(singleRun ? 3 : 1, rounded);

			out.push_back(ring[start]);
			outPinned.push_back(pinned[start]);

			// Walk the run once, emitting the interior points at equal arc steps.
			std::size_t edge	  = 0;
			double		edgeStart = 0.0;
			double		edgeLen	  = edgeLength(ring[start], ring[(start + 1) % n]);
			for (std::int64_t s = 1; s < segments; ++s) {
				const double target = runLength * static_cast<double>(s) / static_cast<double>(segments);
				while (edge + 1 < steps && edgeStart + edgeLen < target) {
					edgeStart += edgeLen;
					++edge;
					edgeLen = edgeLength(ring[(start + edge) % n], ring[(start + edge + 1) % n]);
				}
				const double t = edgeLen > 0.0 ? std::clamp((target - edgeStart) / edgeLen, 0.0, 1.0) : 0.0;
				out.push_back(lerpRounded(ring[(start + edge) % n], ring[(start + edge + 1) % n], t));
				outPinned.push_back(false);
			}
		}

		contour_detail::dropConsecutiveDuplicates(out, &outPinned);
		ring   = std::move(out);
		pinned = std::move(outPinned);
	}

} // namespace geometry
