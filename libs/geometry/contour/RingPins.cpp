#include "RingPins.h"

#include "ContourDetail.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <utility>

namespace geometry {

	namespace {

		using contour_detail::floorDiv;

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

		// The pin lines: two explicit lists plus, with latticeMm > 0, every multiple
		// of latticeMm on both axes.
		struct PinLines {
			std::span<const std::int64_t> xLinesMm;
			std::span<const std::int64_t> yLinesMm;
			std::int64_t				  latticeMm = 0;

			// Indices of the lattice lines strictly between a and b, first..last
			// (empty when first > last).
			std::pair<std::int64_t, std::int64_t> latticeBetween(std::int64_t a, std::int64_t b) const {
				return {floorDiv(std::min(a, b), latticeMm) + 1, floorDiv(std::max(a, b) - 1, latticeMm)};
			}

			bool onLattice(std::int64_t v) const { return latticeMm > 0 && v == floorDiv(v, latticeMm) * latticeMm; }

			bool onAny(Vec2i64 v) const {
				return onLattice(v.x) || onLattice(v.y) || std::find(xLinesMm.begin(), xLinesMm.end(), v.x) != xLinesMm.end() ||
					   std::find(yLinesMm.begin(), yLinesMm.end(), v.y) != yLinesMm.end();
			}

			bool crosses(Vec2i64 a, Vec2i64 b) const {
				if (latticeMm > 0) {
					const auto [x0, x1] = latticeBetween(a.x, b.x);
					const auto [y0, y1] = latticeBetween(a.y, b.y);
					if (x0 <= x1 || y0 <= y1) {
						return true;
					}
				}
				return std::any_of(xLinesMm.begin(), xLinesMm.end(), [&](std::int64_t x) { return strictlyBetween(x, a.x, b.x); }) ||
					   std::any_of(yLinesMm.begin(), yLinesMm.end(), [&](std::int64_t y) { return strictlyBetween(y, a.y, b.y); });
			}
		};

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

	// Output is sized up front and written by index throughout this file: see
	// contour_detail::dropConsecutiveDuplicates on why hot contour loops avoid
	// push_back and vector<bool>.
	std::vector<std::uint8_t> pinAxisLineCrossings(
		Ring& ring, std::span<const std::int64_t> xLinesMm, std::span<const std::int64_t> yLinesMm, std::int64_t latticeMm
	) {
		assert(latticeMm >= 0);
		const std::size_t n = ring.size();
		const PinLines	  lines{xLinesMm, yLinesMm, latticeMm};
		// The crossing points of edge a->b ordered along it by exact parameter, a
		// point where an x and a y line cross each other (or an explicit line lies
		// on the lattice) kept once. Never equal to a: a crossing is strictly inside
		// the edge on its line's axis.
		std::vector<EdgeCrossing> crossings;
		auto addVertical = [&](const Vec2i64& a, const Vec2i64& b, std::int64_t lineX) {
			crossings.push_back(makeCrossing(pointOnVertical(a, b, lineX), lineX - a.x, b.x - a.x));
		};
		auto addHorizontal = [&](const Vec2i64& a, const Vec2i64& b, std::int64_t lineY) {
			crossings.push_back(makeCrossing(pointOnHorizontal(a, b, lineY), lineY - a.y, b.y - a.y));
		};
		auto collect = [&](const Vec2i64& a, const Vec2i64& b) {
			crossings.clear();
			for (const std::int64_t lineX : xLinesMm) {
				if (strictlyBetween(lineX, a.x, b.x)) {
					addVertical(a, b, lineX);
				}
			}
			for (const std::int64_t lineY : yLinesMm) {
				if (strictlyBetween(lineY, a.y, b.y)) {
					addHorizontal(a, b, lineY);
				}
			}
			if (latticeMm > 0) {
				const auto [x0, x1] = lines.latticeBetween(a.x, b.x);
				for (std::int64_t k = x0; k <= x1; ++k) {
					addVertical(a, b, k * latticeMm);
				}
				const auto [y0, y1] = lines.latticeBetween(a.y, b.y);
				for (std::int64_t k = y0; k <= y1; ++k) {
					addHorizontal(a, b, k * latticeMm);
				}
			}
			std::sort(crossings.begin(), crossings.end(), [](const EdgeCrossing& l, const EdgeCrossing& r) {
				return Int128::product(l.num, r.den) < Int128::product(r.num, l.den);
			});
			crossings.erase(
				std::unique(crossings.begin(), crossings.end(), [](const EdgeCrossing& l, const EdgeCrossing& r) { return l.point == r.point; }),
				crossings.end()
			);
		};

		// Few edges cross a line, so count them first, then fill by index.
		std::size_t total = n;
		for (std::size_t i = 0; i < n; ++i) {
			if (lines.crosses(ring[i], ring[(i + 1) % n])) {
				collect(ring[i], ring[(i + 1) % n]);
				total += crossings.size();
			}
		}
		Ring		out(total);
		std::size_t w = 0;
		for (std::size_t i = 0; i < n; ++i) {
			const Vec2i64& a = ring[i];
			const Vec2i64& b = ring[(i + 1) % n];
			out[w++]		 = a;
			if (lines.crosses(a, b)) {
				collect(a, b);
				for (const EdgeCrossing& c : crossings) {
					out[w++] = c.point;
				}
			}
		}
		ring = std::move(out);

		std::vector<std::uint8_t> pinned(ring.size());
		for (std::size_t i = 0; i < ring.size(); ++i) {
			pinned[i] = lines.onAny(ring[i]) ? 1 : 0;
		}
		return pinned;
	}

	void resampleRing(Ring& ring, std::int64_t spacingMm, std::vector<std::uint8_t>& pinned) {
		assert(pinned.size() == ring.size());
		assert(spacingMm > 0);
		const std::size_t n = ring.size();
		if (n < 3) {
			return;
		}

		std::vector<std::size_t> anchors;
		for (std::size_t i = 0; i < n; ++i) {
			if (pinned[i] != 0) {
				anchors.push_back(i);
			}
		}
		const bool singleRun = anchors.size() <= 1;
		if (anchors.empty()) {
			anchors.push_back(0);
		}

		// Per run: vertices start .. end walking forward (a single anchor runs the
		// whole ring back to itself), its arc length, and its segment count.
		struct Run {
			std::size_t	 start	   = 0;
			std::size_t	 steps	   = 0;
			double		 length	   = 0.0;
			std::int64_t segments = 1;
		};
		std::vector<Run> runs(anchors.size());
		std::size_t		 total = 0;
		for (std::size_t r = 0; r < anchors.size(); ++r) {
			Run& run  = runs[r];
			run.start = anchors[r];
			run.steps = singleRun ? n : (anchors[(r + 1) % anchors.size()] + n - run.start) % n;
			for (std::size_t k = 0; k < run.steps; ++k) {
				run.length += edgeLength(ring[(run.start + k) % n], ring[(run.start + k + 1) % n]);
			}
			const std::int64_t rounded = std::llround(run.length / static_cast<double>(spacingMm));
			run.segments			   = std::max<std::int64_t>(singleRun ? 3 : 1, rounded);
			total += static_cast<std::size_t>(run.segments);
		}

		Ring					  out(total);
		std::vector<std::uint8_t> outPinned(total, 0);
		std::size_t				  w = 0;
		for (const Run& run : runs) {
			outPinned[w] = pinned[run.start];
			out[w++]	 = ring[run.start];

			// Walk the run once, emitting the interior points at equal arc steps.
			std::size_t edge	  = 0;
			double		edgeStart = 0.0;
			double		edgeLen	  = edgeLength(ring[run.start], ring[(run.start + 1) % n]);
			for (std::int64_t s = 1; s < run.segments; ++s) {
				const double target = run.length * static_cast<double>(s) / static_cast<double>(run.segments);
				while (edge + 1 < run.steps && edgeStart + edgeLen < target) {
					edgeStart += edgeLen;
					++edge;
					edgeLen = edgeLength(ring[(run.start + edge) % n], ring[(run.start + edge + 1) % n]);
				}
				const double t = edgeLen > 0.0 ? std::clamp((target - edgeStart) / edgeLen, 0.0, 1.0) : 0.0;
				out[w++]	   = lerpRounded(ring[(run.start + edge) % n], ring[(run.start + edge + 1) % n], t);
			}
		}

		contour_detail::dropConsecutiveDuplicates(out, &outPinned);
		ring   = std::move(out);
		pinned = std::move(outPinned);
	}

} // namespace geometry
