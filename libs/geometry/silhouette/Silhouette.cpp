#include "Silhouette.h"

#include "../core/Vec2i64.h"
#include "../offset/WallOffset.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <queue>
#include <vector>

namespace geometry {

	namespace {

		// Exact integer winding number of p about ring r (Sunday's algorithm).
		int windingNumber(const Vec2i64& p, const Ring& r) {
			int				  wn = 0;
			const std::size_t n	 = r.size();
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64& a = r[i];
				const Vec2i64& b = r[(i + 1) % n];
				if (a.y <= p.y) {
					if (b.y > p.y && orientation(a, b, p) == Orientation::CounterClockwise) {
						++wn;
					}
				} else {
					if (b.y <= p.y && orientation(a, b, p) == Orientation::Clockwise) {
						--wn;
					}
				}
			}
			return wn;
		}

		// A directed unit boundary edge with the filled pixel on its left.
		struct Edge {
			Vec2i64 start;
			Vec2i64 end;
		};

		// Rank of turning from incoming direction din to outgoing dout, ordered
		// straight(0) < right/CW(1) < reverse(2) < left/CCW(3). At a checkerboard
		// saddle the two candidates are one left and one right turn; picking the max
		// keeps each diagonal cell on its own CCW loop, so saddles split into
		// disjoint rings matching the 4-connected hole fill.
		int turnRank(const Vec2i64& din, const Vec2i64& dout) {
			const int c = cross(din, dout).sign();
			if (c > 0) {
				return 3;
			}
			if (c < 0) {
				return 1;
			}
			return dot(din, dout).sign() < 0 ? 2 : 0;
		}

	} // namespace

	std::vector<Ring> silhouetteOfRings(const std::vector<Ring>& rings, std::int64_t targetResolution) {
		if (targetResolution < 1) {
			targetResolution = 1;
		}

		// 1. Integer bbox over rings with >= 3 vertices.
		bool		 any = false;
		std::int64_t minX = 0, minY = 0, maxX = 0, maxY = 0;
		for (const Ring& r : rings) {
			if (r.size() < 3) {
				continue;
			}
			for (const Vec2i64& v : r) {
				if (!any) {
					minX = maxX = v.x;
					minY = maxY = v.y;
					any			= true;
				} else {
					minX = std::min(minX, v.x);
					maxX = std::max(maxX, v.x);
					minY = std::min(minY, v.y);
					maxY = std::max(maxY, v.y);
				}
			}
		}
		if (!any) {
			return {};
		}

		const std::int64_t maxSide = std::max(maxX - minX, maxY - minY);
		if (maxSide <= 0) {
			return {};
		}

		// 2. Pixel size: ceil(maxSide / targetResolution), at least 1 mm.
		const std::int64_t ps = std::max<std::int64_t>(1, (maxSide + targetResolution - 1) / targetResolution);

		// 3. Grid padded by a 1-pixel border that is always outside every ring.
		const std::int64_t originX = minX - ps;
		const std::int64_t originY = minY - ps;
		const int		   gw	   = static_cast<int>((maxX - minX) / ps) + 3;
		const int		   gh	   = static_cast<int>((maxY - minY) / ps) + 3;

		auto X	 = [&](int i) { return originX + static_cast<std::int64_t>(i) * ps; };
		auto Y	 = [&](int j) { return originY + static_cast<std::int64_t>(j) * ps; };
		auto idx = [&](int i, int j) { return j * gw + i; };

		// 4. Coverage: nonzero total winding at each pixel center.
		std::vector<char> cov(static_cast<std::size_t>(gw) * gh, 0);
		for (int j = 0; j < gh; ++j) {
			const std::int64_t cy = Y(j) + ps / 2;
			for (int i = 0; i < gw; ++i) {
				const Vec2i64 c{X(i) + ps / 2, cy};
				int			  w = 0;
				for (const Ring& r : rings) {
					if (r.size() < 3) {
						continue;
					}
					w += windingNumber(c, r);
				}
				if (w != 0) {
					cov[idx(i, j)] = 1;
				}
			}
		}

		// 5. Flood-fill the exterior from the (always empty) border; anything the
		// flood cannot reach is either covered or an interior hole, i.e. filled.
		std::vector<char> exterior(static_cast<std::size_t>(gw) * gh, 0);
		std::queue<int>	  queue;
		auto			  seed = [&](int i, int j) {
			 const int id = idx(i, j);
			 if (!cov[id] && !exterior[id]) {
				 exterior[id] = 1;
				 queue.push(id);
			 }
		};
		for (int i = 0; i < gw; ++i) {
			seed(i, 0);
			seed(i, gh - 1);
		}
		for (int j = 0; j < gh; ++j) {
			seed(0, j);
			seed(gw - 1, j);
		}
		const int dx[4] = {1, -1, 0, 0};
		const int dy[4] = {0, 0, 1, -1};
		while (!queue.empty()) {
			const int id = queue.front();
			queue.pop();
			const int i = id % gw;
			const int j = id / gw;
			for (int k = 0; k < 4; ++k) {
				const int ni = i + dx[k];
				const int nj = j + dy[k];
				if (ni < 0 || ni >= gw || nj < 0 || nj >= gh) {
					continue;
				}
				seed(ni, nj);
			}
		}

		auto filled = [&](int i, int j) {
			if (i < 0 || i >= gw || j < 0 || j >= gh) {
				return false;
			}
			return exterior[idx(i, j)] == 0;
		};

		// 6. Emit directed boundary edges (filled pixel on the left).
		std::vector<Edge> edges;
		for (int j = 0; j < gh; ++j) {
			for (int i = 0; i < gw; ++i) {
				if (!filled(i, j)) {
					continue;
				}
				if (!filled(i + 1, j)) {
					edges.push_back({{X(i + 1), Y(j)}, {X(i + 1), Y(j + 1)}});
				}
				if (!filled(i - 1, j)) {
					edges.push_back({{X(i), Y(j + 1)}, {X(i), Y(j)}});
				}
				if (!filled(i, j + 1)) {
					edges.push_back({{X(i + 1), Y(j + 1)}, {X(i), Y(j + 1)}});
				}
				if (!filled(i, j - 1)) {
					edges.push_back({{X(i), Y(j)}, {X(i + 1), Y(j)}});
				}
			}
		}

		std::map<Vec2i64, std::vector<std::size_t>> outgoing;
		for (std::size_t e = 0; e < edges.size(); ++e) {
			outgoing[edges[e].start].push_back(e);
		}

		// Chain edges head->tail into closed loops, splitting saddles by turnRank.
		std::vector<char> used(edges.size(), 0);
		std::vector<Ring> result;
		for (std::size_t s = 0; s < edges.size(); ++s) {
			if (used[s]) {
				continue;
			}
			Ring		  ring;
			const Vec2i64 startCorner = edges[s].start;
			std::size_t	  cur		  = s;
			for (std::size_t guard = 0; guard <= edges.size(); ++guard) {
				used[cur] = 1;
				ring.push_back(edges[cur].start);
				const Vec2i64 tail = edges[cur].end;
				if (tail == startCorner) {
					break;
				}
				const Vec2i64 din  = edges[cur].end - edges[cur].start;
				std::size_t	  best = edges.size();
				int			  rank = -1;
				const auto	  it   = outgoing.find(tail);
				if (it != outgoing.end()) {
					for (std::size_t e : it->second) {
						if (used[e]) {
							continue;
						}
						const int r = turnRank(din, edges[e].end - edges[e].start);
						if (r > rank || (r == rank && e < best)) {
							rank = r;
							best = e;
						}
					}
				}
				if (best == edges.size()) {
					break;
				}
				cur = best;
			}

			simplifyRing(ring, std::max<std::int64_t>(1, ps / 2));
			if (ring.size() < 3 || signedAreaDoubled(ring).sign() == 0) {
				continue;
			}
			ensureCounterClockwise(ring);
			result.push_back(std::move(ring));
		}
		return result;
	}

} // namespace geometry
