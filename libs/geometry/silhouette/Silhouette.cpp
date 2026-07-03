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

		// Exact point-in-triangle including the boundary. Inside iff no edge puts p
		// strictly CW while another puts it strictly CCW; collinear (on-edge) counts
		// toward neither, so triangles sharing an edge both claim the edge pixels and
		// tile with no gap. Winding of (a,b,c) is irrelevant.
		bool pointInTriangle(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c, const Vec2i64& p) {
			bool cw = false, ccw = false;
			for (const Orientation o : {orientation(a, b, p), orientation(b, c, p), orientation(c, a, p)}) {
				if (o == Orientation::CounterClockwise) {
					ccw = true;
				} else if (o == Orientation::Clockwise) {
					cw = true;
				}
			}
			return !(cw && ccw);
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

		// Padded pixel grid over an integer bbox.
		struct Grid {
			std::int64_t ps		 = 1; // pixel size (mm)
			std::int64_t originX = 0;
			std::int64_t originY = 0;
			int			 gw		 = 0;
			int			 gh		 = 0;
		};

		// bbox -> pixel size -> padded grid dims/origin. `padding` cells of always
		// empty border surround the geometry on every side. Returns false when the
		// bbox is a point or line (nothing to rasterize).
		bool makeGrid(std::int64_t minX, std::int64_t minY, std::int64_t maxX, std::int64_t maxY,
					  std::int64_t targetResolution, std::int64_t padding, Grid& out) {
			const std::int64_t maxSide = std::max(maxX - minX, maxY - minY);
			if (maxSide <= 0) {
				return false;
			}
			// ceil(maxSide / targetResolution), at least 1 mm.
			out.ps		= std::max<std::int64_t>(1, (maxSide + targetResolution - 1) / targetResolution);
			out.originX = minX - padding * out.ps;
			out.originY = minY - padding * out.ps;
			out.gw		= static_cast<int>((maxX - minX) / out.ps) + static_cast<int>(2 * padding) + 1;
			out.gh		= static_cast<int>((maxY - minY) / out.ps) + static_cast<int>(2 * padding) + 1;
			return true;
		}

		// In-place box (Chebyshev) dilation by r cells, separable H then V. Out of
		// bounds is background, so nothing grows in from the border.
		void dilate(std::vector<char>& m, int gw, int gh, std::int64_t r) {
			std::vector<char> tmp(m.size(), 0);
			for (int j = 0; j < gh; ++j) {
				for (int i = 0; i < gw; ++i) {
					char v = 0;
					for (std::int64_t k = -r; k <= r && !v; ++k) {
						const int ii = i + static_cast<int>(k);
						if (ii >= 0 && ii < gw && m[static_cast<std::size_t>(j) * gw + ii]) {
							v = 1;
						}
					}
					tmp[static_cast<std::size_t>(j) * gw + i] = v;
				}
			}
			for (int j = 0; j < gh; ++j) {
				for (int i = 0; i < gw; ++i) {
					char v = 0;
					for (std::int64_t k = -r; k <= r && !v; ++k) {
						const int jj = j + static_cast<int>(k);
						if (jj >= 0 && jj < gh && tmp[static_cast<std::size_t>(jj) * gw + i]) {
							v = 1;
						}
					}
					m[static_cast<std::size_t>(j) * gw + i] = v;
				}
			}
		}

		// In-place box (Chebyshev) erosion by r cells, separable H then V. Out of
		// bounds is background, so the erosion pulls the dilated boundary back to its
		// pre-dilation position (close never grows the outer silhouette).
		void erode(std::vector<char>& m, int gw, int gh, std::int64_t r) {
			std::vector<char> tmp(m.size(), 0);
			for (int j = 0; j < gh; ++j) {
				for (int i = 0; i < gw; ++i) {
					char v = 1;
					for (std::int64_t k = -r; k <= r && v; ++k) {
						const int ii = i + static_cast<int>(k);
						if (ii < 0 || ii >= gw || !m[static_cast<std::size_t>(j) * gw + ii]) {
							v = 0;
						}
					}
					tmp[static_cast<std::size_t>(j) * gw + i] = v;
				}
			}
			for (int j = 0; j < gh; ++j) {
				for (int i = 0; i < gw; ++i) {
					char v = 1;
					for (std::int64_t k = -r; k <= r && v; ++k) {
						const int jj = j + static_cast<int>(k);
						if (jj < 0 || jj >= gh || !tmp[static_cast<std::size_t>(jj) * gw + i]) {
							v = 0;
						}
					}
					m[static_cast<std::size_t>(j) * gw + i] = v;
				}
			}
		}

		// Shared tail: a per-pixel coverage mask -> simplified CCW rings. Optionally
		// morphologically closes the mask (dilate then erode by closeRadiusPx) to
		// bridge gaps between disjoint covered blobs, then fills interior holes by
		// exterior flood-fill, traces directed boundary edges into CCW loops
		// (saddles split by turnRank), simplifies, and returns holes-filled rings.
		std::vector<Ring> maskToRings(std::vector<char>& cov, const Grid& g, std::int64_t closeRadiusPx) {
			const int gw = g.gw;
			const int gh = g.gh;

			auto X	 = [&](int i) { return g.originX + static_cast<std::int64_t>(i) * g.ps; };
			auto Y	 = [&](int j) { return g.originY + static_cast<std::int64_t>(j) * g.ps; };
			auto idx = [&](int i, int j) { return j * gw + i; };

			if (closeRadiusPx > 0) {
				dilate(cov, gw, gh, closeRadiusPx);
				erode(cov, gw, gh, closeRadiusPx);
			}

			// Flood-fill the exterior from the (always empty) border; anything the
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

			// Emit directed boundary edges (filled pixel on the left).
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

				simplifyRing(ring, std::max<std::int64_t>(1, g.ps / 2));
				if (ring.size() < 3 || signedAreaDoubled(ring).sign() == 0) {
					continue;
				}
				ensureCounterClockwise(ring);
				result.push_back(std::move(ring));
			}
			return result;
		}

	} // namespace

	std::vector<Ring> silhouetteOfRings(const std::vector<Ring>& rings, std::int64_t targetResolution) {
		if (targetResolution < 1) {
			targetResolution = 1;
		}

		// Integer bbox over rings with >= 3 vertices.
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

		Grid g;
		if (!makeGrid(minX, minY, maxX, maxY, targetResolution, 1, g)) {
			return {};
		}

		// Coverage: nonzero total winding at each pixel center.
		std::vector<char> cov(static_cast<std::size_t>(g.gw) * g.gh, 0);
		for (int j = 0; j < g.gh; ++j) {
			const std::int64_t cy = g.originY + static_cast<std::int64_t>(j) * g.ps + g.ps / 2;
			for (int i = 0; i < g.gw; ++i) {
				const Vec2i64 c{g.originX + static_cast<std::int64_t>(i) * g.ps + g.ps / 2, cy};
				int			  w = 0;
				for (const Ring& r : rings) {
					if (r.size() < 3) {
						continue;
					}
					w += windingNumber(c, r);
				}
				if (w != 0) {
					cov[static_cast<std::size_t>(j) * g.gw + i] = 1;
				}
			}
		}

		return maskToRings(cov, g, 0);
	}

	std::vector<Ring> silhouetteOfTriangles(const std::vector<Vec2i64>& triangleVerts, std::int64_t targetResolution,
											std::int64_t closeRadiusPx) {
		if (targetResolution < 1) {
			targetResolution = 1;
		}
		if (closeRadiusPx < 0) {
			closeRadiusPx = 0;
		}

		// Integer bbox over non-degenerate triangles.
		bool		 any = false;
		std::int64_t minX = 0, minY = 0, maxX = 0, maxY = 0;
		for (std::size_t t = 0; t + 3 <= triangleVerts.size(); t += 3) {
			const Vec2i64& a = triangleVerts[t];
			const Vec2i64& b = triangleVerts[t + 1];
			const Vec2i64& c = triangleVerts[t + 2];
			if (orientation(a, b, c) == Orientation::Collinear) {
				continue;
			}
			for (const Vec2i64& v : {a, b, c}) {
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

		// closeRadiusPx cells of dilation must not reach the border, so pad by one
		// more than that (padding 1 when closeRadiusPx == 0, matching rings).
		Grid g;
		if (!makeGrid(minX, minY, maxX, maxY, targetResolution, closeRadiusPx + 1, g)) {
			return {};
		}

		// Coverage: union of the triangles. Iterate each triangle over its own cell
		// bbox and mark pixels whose center is inside (boundary-inclusive), never
		// all pixels over all triangles.
		std::vector<char> cov(static_cast<std::size_t>(g.gw) * g.gh, 0);
		for (std::size_t t = 0; t + 3 <= triangleVerts.size(); t += 3) {
			const Vec2i64& a = triangleVerts[t];
			const Vec2i64& b = triangleVerts[t + 1];
			const Vec2i64& c = triangleVerts[t + 2];
			if (orientation(a, b, c) == Orientation::Collinear) {
				continue;
			}
			const std::int64_t tMinX = std::min({a.x, b.x, c.x});
			const std::int64_t tMaxX = std::max({a.x, b.x, c.x});
			const std::int64_t tMinY = std::min({a.y, b.y, c.y});
			const std::int64_t tMaxY = std::max({a.y, b.y, c.y});

			// Cell index range covering the triangle bbox (one-cell margin so a
			// center just inside an edge is never skipped). originX/Y <= tMin, so
			// the differences are non-negative and truncation is a floor.
			int i0 = static_cast<int>((tMinX - g.originX) / g.ps) - 1;
			int i1 = static_cast<int>((tMaxX - g.originX) / g.ps) + 1;
			int j0 = static_cast<int>((tMinY - g.originY) / g.ps) - 1;
			int j1 = static_cast<int>((tMaxY - g.originY) / g.ps) + 1;
			i0	   = std::max(i0, 0);
			j0	   = std::max(j0, 0);
			i1	   = std::min(i1, g.gw - 1);
			j1	   = std::min(j1, g.gh - 1);

			for (int j = j0; j <= j1; ++j) {
				const std::int64_t cy = g.originY + static_cast<std::int64_t>(j) * g.ps + g.ps / 2;
				for (int i = i0; i <= i1; ++i) {
					const Vec2i64 center{g.originX + static_cast<std::int64_t>(i) * g.ps + g.ps / 2, cy};
					if (pointInTriangle(a, b, c, center)) {
						cov[static_cast<std::size_t>(j) * g.gw + i] = 1;
					}
				}
			}
		}

		return maskToRings(cov, g, closeRadiusPx);
	}

} // namespace geometry
