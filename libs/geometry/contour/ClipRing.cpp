#include "ClipRing.h"

#include "ContourDetail.h"
#include "RingPins.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace geometry {

	namespace {

		bool inClosedRect(const Vec2i64& p, const RectMm& rect) {
			return p.x >= rect.min.x && p.x <= rect.max.x && p.y >= rect.min.y && p.y <= rect.max.y;
		}

		// Whether edge a->b of a CCW ring bounds the intersection. After border
		// crossings are inserted every edge lies either inside the closed rect or
		// in a closed half-plane outside it, so the endpoints decide. An edge along
		// the boundary counts only when the rect is on its left, i.e. it runs
		// counter-clockwise around the rect.
		bool keepsEdge(const Vec2i64& a, const Vec2i64& b, const RectMm& rect) {
			if (!inClosedRect(a, rect) || !inClosedRect(b, rect)) {
				return false;
			}
			if (a.y == b.y && a.y == rect.min.y) {
				return b.x > a.x;
			}
			if (a.y == b.y && a.y == rect.max.y) {
				return b.x < a.x;
			}
			if (a.x == b.x && a.x == rect.max.x) {
				return b.y > a.y;
			}
			if (a.x == b.x && a.x == rect.min.x) {
				return b.y < a.y;
			}
			return true;
		}

		// Counter-clockwise arc length from rect.min to a point on the boundary.
		std::int64_t boundaryParam(const Vec2i64& p, const RectMm& rect) {
			const std::int64_t w = rect.max.x - rect.min.x;
			const std::int64_t h = rect.max.y - rect.min.y;
			if (p.y == rect.min.y && p.x < rect.max.x) {
				return p.x - rect.min.x;
			}
			if (p.x == rect.max.x && p.y < rect.max.y) {
				return w + (p.y - rect.min.y);
			}
			if (p.y == rect.max.y && p.x > rect.min.x) {
				return w + h + (rect.max.x - p.x);
			}
			return 2 * w + h + (rect.max.y - p.y);
		}

		Ring rectRing(const RectMm& rect) {
			return {rect.min, {rect.max.x, rect.min.y}, rect.max, {rect.min.x, rect.max.y}};
		}

		// A maximal run of kept edges: ring vertices first .. first + edges.
		struct Chain {
			std::size_t	 first = 0;
			std::size_t	 edges = 0;
			std::int64_t entryParam = 0;
			std::int64_t exitParam	= 0;
		};

		std::vector<Ring> clipCounterClockwise(Ring ring, const RectMm& rect) {
			const std::array<std::int64_t, 2> xLines = {rect.min.x, rect.max.x};
			const std::array<std::int64_t, 2> yLines = {rect.min.y, rect.max.y};
			pinAxisLineCrossings(ring, xLines, yLines);

			const std::size_t		  n = ring.size();
			std::vector<std::uint8_t> kept(n); // bytes: see contour_detail::dropConsecutiveDuplicates
			std::size_t				  keptCount = 0;
			for (std::size_t i = 0; i < n; ++i) {
				kept[i] = keepsEdge(ring[i], ring[(i + 1) % n], rect) ? 1 : 0;
				keptCount += kept[i];
			}

			if (keptCount == n) {
				return {std::move(ring)};
			}
			if (keptCount == 0) {
				// No edge enters the open rect, so the rect is wholly inside or wholly
				// outside. Test its center in doubled coordinates to stay integral.
				Ring doubled;
				doubled.reserve(n);
				for (const Vec2i64& v : ring) {
					doubled.push_back(v * 2);
				}
				if (pointInPolygon(rect.min + rect.max, doubled) == PointInPolygon::Inside) {
					return {rectRing(rect)};
				}
				return {};
			}

			// Chains in ring order. Each starts and ends on the rect boundary: a
			// chain endpoint touches a dropped edge, and a dropped edge never reaches
			// the open interior.
			std::vector<Chain> chains;
			for (std::size_t i = 0; i < n; ++i) {
				if (kept[i] == 0 || kept[(i + n - 1) % n] != 0) {
					continue;
				}
				Chain chain;
				chain.first = i;
				while (kept[(i + chain.edges) % n] != 0) {
					++chain.edges;
				}
				chain.entryParam = boundaryParam(ring[i], rect);
				chain.exitParam	 = boundaryParam(ring[(i + chain.edges) % n], rect);
				chains.push_back(chain);
			}

			const Ring						  corners	   = rectRing(rect);
			const std::int64_t				  perimeter	   = 2 * ((rect.max.x - rect.min.x) + (rect.max.y - rect.min.y));
			const std::array<std::int64_t, 4> cornerParams = {
				0, rect.max.x - rect.min.x, perimeter / 2, perimeter / 2 + (rect.max.x - rect.min.x)
			};
			auto ccwDistance = [perimeter](std::int64_t from, std::int64_t to) {
				return ((to - from) % perimeter + perimeter) % perimeter;
			};

			std::vector<Ring> pieces;
			std::vector<std::uint8_t> used(chains.size(), 0);
			for (std::size_t c0 = 0; c0 < chains.size(); ++c0) {
				if (used[c0] != 0) {
					continue;
				}
				Ring		piece;
				std::size_t c = c0;
				while (used[c] == 0) {
					used[c]			   = 1;
					const Chain& chain = chains[c];
					for (std::size_t k = 0; k <= chain.edges; ++k) {
						piece.push_back(ring[(chain.first + k) % n]);
					}

					// Inside the ring, the intersection boundary follows the rect
					// counter-clockwise from this exit to the nearest entry.
					std::size_t	 next	  = c;
					std::int64_t nextDist = perimeter;
					for (std::size_t e = 0; e < chains.size(); ++e) {
						const std::int64_t d = ccwDistance(chain.exitParam, chains[e].entryParam);
						if (d < nextDist) {
							nextDist = d;
							next	 = e;
						}
					}
					std::array<std::pair<std::int64_t, std::size_t>, 4> passed{};
					std::size_t											passedCount = 0;
					for (std::size_t k = 0; k < 4; ++k) {
						const std::int64_t d = ccwDistance(chain.exitParam, cornerParams[k]);
						if (d > 0 && d < nextDist) {
							passed[passedCount++] = {d, k};
						}
					}
					std::sort(passed.begin(), passed.begin() + static_cast<std::ptrdiff_t>(passedCount));
					for (std::size_t k = 0; k < passedCount; ++k) {
						piece.push_back(corners[passed[k].second]);
					}
					assert((next == c0 || used[next] == 0) && "clipRingToRect: boundary walk reached a used chain");
					c = next;
				}

				contour_detail::dropConsecutiveDuplicates(piece);
				if (piece.size() >= 3 && signedAreaDoubled(piece).sign() != 0) {
					pieces.push_back(std::move(piece));
				}
			}
			return pieces;
		}

	} // namespace

	std::vector<Ring> clipRingToRect(const Ring& ring, const RectMm& rect) {
		assert(rect.min.x < rect.max.x && rect.min.y < rect.max.y);
		const Winding winding = windingOrder(ring);
		if (ring.size() < 3 || winding == Winding::Degenerate) {
			return {};
		}
		if (winding == Winding::CounterClockwise) {
			return clipCounterClockwise(ring, rect);
		}
		Ring reversed(ring.rbegin(), ring.rend());
		std::vector<Ring> pieces = clipCounterClockwise(std::move(reversed), rect);
		for (Ring& piece : pieces) {
			std::reverse(piece.begin(), piece.end());
		}
		return pieces;
	}

} // namespace geometry
