#include "Triangulation.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace geometry {

	namespace {

		using Tri = std::array<std::uint32_t, 3>;

		// Signed doubled area of a ring of points, shoelace in 128-bit. CCW > 0.
		Int128 signedAreaDoubled(const std::vector<Vec2i64>& pts) {
			Int128 acc(0);
			const std::size_t n = pts.size();
			for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
				acc = acc + cross(pts[j], pts[i]);
			}
			return acc;
		}

		// Materialize a ring of points from an index ring.
		std::vector<Vec2i64> ringPoints(const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& ring) {
			std::vector<Vec2i64> pts;
			pts.reserve(ring.size());
			for (std::uint32_t idx : ring) {
				pts.push_back(vertices[idx]);
			}
			return pts;
		}

		// A simple ring is one where no two non-adjacent edges intersect and no two
		// adjacent edges overlap (collinear backtrack) or share more than the common
		// vertex. Adjacent edges may legitimately touch only at their shared
		// endpoint. O(n^2) exact check; n is small per navmesh chunk.
		bool ringIsSimple(const std::vector<Vec2i64>& pts) {
			const std::size_t n = pts.size();
			if (n < 3) {
				return false;
			}
			// No coincident consecutive vertices (zero-length edge).
			for (std::size_t i = 0; i < n; ++i) {
				if (pts[i] == pts[(i + 1) % n]) {
					return false;
				}
			}
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64& a0 = pts[i];
				const Vec2i64& a1 = pts[(i + 1) % n];
				for (std::size_t j = i + 1; j < n; ++j) {
					const Vec2i64&		   b0	= pts[j];
					const Vec2i64&		   b1	= pts[(j + 1) % n];
					const bool			   adj	= (j == i + 1) || (i == 0 && j == n - 1);
					SegmentIntersection	   r	= intersectSegments(a0, a1, b0, b1);
					if (adj) {
						// Adjacent edges share exactly one endpoint; anything beyond an
						// endpoint touch (a proper crossing, or a collinear overlap, i.e.
						// the polygon doubling back on itself) means non-simple.
						if (r.relation == SegmentRelation::ProperCrossing ||
							r.relation == SegmentRelation::CollinearOverlap) {
							return false;
						}
					} else {
						// Non-adjacent edges must not meet at all.
						if (r.relation != SegmentRelation::Disjoint) {
							return false;
						}
					}
				}
			}
			return true;
		}

		// Exact point-in-triangle for a CCW triangle (a,b,c). On-edge counts as
		// inside (boundary is "in"); callers that want strict interior filter the
		// triangle's own vertices out before testing.
		bool pointInCcwTriangle(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b, const Vec2i64& c) {
			return orientation(a, b, p) != Orientation::Clockwise && orientation(b, c, p) != Orientation::Clockwise &&
				   orientation(c, a, p) != Orientation::Clockwise;
		}

		// Strict interior of a CCW triangle (a,b,c): p must be strictly left of all
		// three directed edges. On-edge and on-vertex both return false. Used by the
		// ear-clip blocker test so a vertex lying on a candidate ear's edge does NOT
		// block it (distinct from pointInCcwTriangle, which counts the boundary in).
		bool strictlyInsideCcwTriangle(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b, const Vec2i64& c) {
			return orientation(a, b, p) == Orientation::CounterClockwise &&
				   orientation(b, c, p) == Orientation::CounterClockwise &&
				   orientation(c, a, p) == Orientation::CounterClockwise;
		}

		// Is p strictly between a and b on the open segment (a,b)? Collinear with a-b
		// and lexicographically strictly between the endpoints. Exact integer math.
		bool onOpenSegment(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
			if (orientation(a, b, p) != Orientation::Collinear) {
				return false;
			}
			// Collinear: p is interior to the segment iff it lies strictly within the
			// bounding box of a-b on the dominant axis (and is not an endpoint).
			if (a.x != b.x) {
				const std::int64_t lo = std::min(a.x, b.x);
				const std::int64_t hi = std::max(a.x, b.x);
				return p.x > lo && p.x < hi;
			}
			const std::int64_t lo = std::min(a.y, b.y);
			const std::int64_t hi = std::max(a.y, b.y);
			return p.y > lo && p.y < hi;
		}

		// ---- Hole merging (Eberly bridge technique) ------------------------------
		//
		// Splice each hole into the outer ring via a bridge edge so the whole region
		// becomes one simple polygon. Result is an index sequence into `vertices`
		// (bridge endpoints appear twice, as the bridge edge is traversed both ways).

		// Find the bridge vertex on `outer` visible from hole-vertex M (the hole's
		// rightmost vertex). Returns the position in `outer` to bridge to, or
		// SIZE_MAX if none (degenerate). Classic Eberly visible-vertex search, all
		// exact: a +x ray from M strikes the nearest outer edge; the bridge is that
		// edge's larger-x endpoint, refined past any reflex vertex that blocks it.
		std::size_t findBridgePosition(const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& outer,
									   const Vec2i64& M) {
			const std::size_t n = outer.size();

			// Cast a ray from M in +x; find the closest edge it strikes to the right.
			// The hit x is the rational  hitX = p0.x + (M.y - p0.y)*dx / dy. We track
			// the offset from M as a rational num/den (den > 0):
			//   num = (p0.x - M.x)*dy + (M.y - p0.y)*dx,  den = dy (sign-normalized).
			// At region-local scale (coords a few million mm) these products are well
			// under 2^63, so num and den fit int64; the only 128-bit step is the
			// cross-multiply that compares two such rationals, done via Int128::product.
			bool		 found	   = false;
			std::size_t	 edgeStart = SIZE_MAX; // position in `outer` of the edge's first endpoint
			std::int64_t bestNum   = 0;
			std::int64_t bestDen   = 1;
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64& p0 = vertices[outer[i]];
				const Vec2i64& p1 = vertices[outer[(i + 1) % n]];
				// An edge the +x ray from M can cross: one endpoint strictly above M.y
				// and the other at-or-below (half-open rule, as in point-in-polygon).
				const bool straddles = (p0.y > M.y) != (p1.y > M.y);
				if (!straddles) {
					continue;
				}
				std::int64_t dy	 = p1.y - p0.y;
				std::int64_t dx	 = p1.x - p0.x;
				std::int64_t num = (p0.x - M.x) * dy + (M.y - p0.y) * dx;
				std::int64_t den = dy;
				if (den < 0) {
					num = -num;
					den = -den;
				}
				if (num < 0) {
					continue; // hit is left of M; the ray goes +x only
				}
				// Keep the smallest hit x:  num/den < bestNum/bestDen  (both den > 0)
				// iff  num*bestDen < bestNum*den, compared exactly in 128-bit.
				if (!found || Int128::product(num, bestDen) < Int128::product(bestNum, den)) {
					found	  = true;
					edgeStart = i;
					bestNum	  = num;
					bestDen	  = den;
				}
			}

			if (!found) {
				return SIZE_MAX;
			}

			const std::size_t pPos = edgeStart;
			const std::size_t qPos = (edgeStart + 1) % n;
			const Vec2i64&	  P	   = vertices[outer[pPos]];
			const Vec2i64&	  Q	   = vertices[outer[qPos]];

			// If the hit lands exactly on an outer vertex (hitX == vertex.x at M.y),
			// bridge straight to it. hitX - M.x == bestNum/bestDen, so the hit is at
			// column vx iff bestNum == (vx - M.x)*bestDen.
			if (P.y == M.y && bestNum == (P.x - M.x) * bestDen) {
				return pPos;
			}
			if (Q.y == M.y && bestNum == (Q.x - M.x) * bestDen) {
				return qPos;
			}

			// Provisional bridge: the struck edge's larger-x endpoint (Eberly).
			const bool	   pIsBridge = (P.x >= Q.x);
			std::size_t	   bridgePos = pIsBridge ? pPos : qPos;
			const Vec2i64& bridgeV	 = vertices[outer[bridgePos]];
			const Vec2i64& otherV	 = pIsBridge ? Q : P; // struck edge's other endpoint

			// Eberly's visibility wedge has apex M and the closed triangle
			// (M, I, bridgeV), where I is the exact ray/edge intersection at
			// (M.x + t, M.y), t > 0. A reflex outer vertex inside that triangle blocks
			// the direct bridge. We test containment exactly, without ever
			// constructing the (rational) point I, by checking R against each of the
			// triangle's three edges with orientation/sign tests only:
			//
			//   * Edge M->I lies along the +x ray, so the side of R relative to it is
			//     just sign(R.y - M.y). The opposite vertex bridgeV sits on the side
			//     sign(bridgeV.y - M.y) =: sQ (nonzero here: a hit exactly at bridgeV's
			//     column was already returned above, so bridgeV is off the ray line).
			//   * Edge I->bridgeV is a sub-segment of the struck edge P-Q, hence on the
			//     same line; its half-plane is that of the line through (otherV,
			//     bridgeV), with M on the interior side.
			//   * Edge bridgeV->M uses only exact vertices; I sits at sign sQ of it
			//     (cross(M - bridgeV, I - M) = (bridgeV.y - M.y)*t, t > 0).
			//
			// On-edge counts as inside, matching the prior closed-triangle test, so the
			// axis-aligned cases (where I was exact anyway) are unchanged.
			const int sQ = (bridgeV.y > M.y) ? 1 : -1;
			const int sBridgeToM_M =
				orientation(otherV, bridgeV, M) == Orientation::CounterClockwise ? 1 : -1;
			auto inWedge = [&](const Vec2i64& R) {
				// Side 1: R must be between the +x ray and bridgeV's side of it.
				const int sRay = (R.y > M.y) ? 1 : (R.y < M.y ? -1 : 0);
				if (sRay != 0 && sRay != sQ) {
					return false;
				}
				// Side 2: R must be on M's side of the struck-edge line (or on it).
				const Orientation oEdge = orientation(otherV, bridgeV, R);
				if (oEdge != Orientation::Collinear) {
					const int sEdge = (oEdge == Orientation::CounterClockwise) ? 1 : -1;
					if (sEdge != sBridgeToM_M) {
						return false;
					}
				}
				// Side 3: R must be on I's side of the bridge segment bridgeV->M (or on it).
				const Orientation oBridge = orientation(bridgeV, M, R);
				if (oBridge != Orientation::Collinear) {
					const int sBridge = (oBridge == Orientation::CounterClockwise) ? 1 : -1;
					if (sBridge != sQ) {
						return false;
					}
				}
				return true;
			};

			// Eberly refinement: a reflex outer vertex inside the wedge triangle blocks
			// visibility. Among blockers choose the one whose direction from M is most
			// aligned with +x (smallest angle to the ray); ties by nearer x.
			std::size_t best	   = bridgePos;
			bool		hasBlocker = false;
			Vec2i64		bestR	   = bridgeV;
			for (std::size_t cur = 0; cur < n; ++cur) {
				const Vec2i64& R = vertices[outer[cur]];
				if (R == bridgeV) {
					continue;
				}
				const Vec2i64& rp = vertices[outer[(cur + n - 1) % n]];
				const Vec2i64& rn = vertices[outer[(cur + 1) % n]];
				if (orientation(rp, R, rn) != Orientation::Clockwise) {
					continue; // only reflex vertices can block
				}
				if (!inWedge(R)) {
					continue;
				}
				if (!hasBlocker) {
					hasBlocker = true;
					best	   = cur;
					bestR	   = R;
				} else {
					// R is "more +x aligned" than bestR when the turn from (M->bestR)
					// to (M->R) is clockwise; collinear ties go to the nearer one.
					const int turn = cross(bestR - M, R - M).sign();
					if (turn < 0 || (turn == 0 && (R.x - M.x) < (bestR.x - M.x))) {
						best  = cur;
						bestR = R;
					}
				}
			}

			return best;
		}

		// Is the bridge segment from hole-apex M to loop position `target` clear of the
		// current loop boundary? A valid bridge touches the loop only at its two
		// endpoints (M, which is about to be spliced in, and loop[target]); it must not
		// properly cross any loop edge nor pass through any other loop vertex. The
		// Eberly +x-ray bridge picks a horizontally-visible target, but once several
		// holes are merged the chosen target sits OFF the ray, and that slanted bridge
		// can slice through a hole the horizontal ray never straddled. We validate
		// exactly here so mergeHoles can reject a fouled target and pick another.
		bool bridgeIsClear(const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& loop,
						   const Vec2i64& M, std::size_t target) {
			const std::size_t n = loop.size();
			const Vec2i64&	  T = vertices[loop[target]];
			if (M == T) {
				return false;
			}
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64& e0 = vertices[loop[i]];
				const Vec2i64& e1 = vertices[loop[(i + 1) % n]];
				const SegmentIntersection r = intersectSegments(M, T, e0, e1);
				if (r.relation == SegmentRelation::Disjoint) {
					continue;
				}
				if (r.relation == SegmentRelation::ProperCrossing || r.relation == SegmentRelation::CollinearOverlap) {
					return false;
				}
				// An endpoint touch is allowed ONLY at the bridge's own endpoint T (the
				// edges incident to loop[target]); any other touch means the bridge grazes
				// the boundary, which would foul the merged simple polygon.
				const bool incidentToTarget = (i == target) || ((i + 1) % n == target);
				if (!incidentToTarget) {
					return false;
				}
			}
			return true;
		}

		// Squared length of M->loop[target], for choosing the nearest valid bridge.
		Int128 bridgeLenSq(const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& loop, const Vec2i64& M,
						   std::size_t target) {
			const Vec2i64 d = vertices[loop[target]] - M;
			return dot(d, d);
		}

		// Does the bridge segment (M, T) cross or touch any edge of `hole`? M is a vertex
		// of the hole being bridged, so its incident hole edges are allowed to share M;
		// any other contact (a proper crossing, a collinear overlap, or an endpoint touch
		// elsewhere) fouls the bridge. Used to keep a bridge from slicing through a hole
		// that has not been merged into the loop yet -- Eberly's right-to-left order makes
		// the +x ray safe, but the brute-force fallback can aim in any direction.
		bool bridgeHitsHole(const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& hole,
							const Vec2i64& M, const Vec2i64& T) {
			const std::size_t n = hole.size();
			for (std::size_t i = 0; i < n; ++i) {
				const Vec2i64& e0 = vertices[hole[i]];
				const Vec2i64& e1 = vertices[hole[(i + 1) % n]];
				const SegmentIntersection r = intersectSegments(M, T, e0, e1);
				if (r.relation == SegmentRelation::Disjoint) {
					continue;
				}
				if (r.relation != SegmentRelation::EndpointTouch) {
					return true; // proper crossing or collinear overlap
				}
				// Endpoint touch is fine only where the bridge starts at M on M's own
				// incident hole edges; a touch at any other hole vertex fouls it.
				if (!(e0 == M || e1 == M)) {
					return true;
				}
			}
			return false;
		}

		// Merge holes into the outer ring, returning a single index loop. Holes are
		// processed by descending rightmost-x. Returns empty on failure.
		std::vector<std::uint32_t> mergeHoles(const std::vector<Vec2i64>& vertices,
											  const std::vector<std::uint32_t>& outer,
											  const std::vector<std::vector<std::uint32_t>>& holes) {
			std::vector<std::uint32_t> loop = outer;

			// Order holes by descending max-x of their vertices (Eberly): the
			// rightmost hole bridges first so later bridges see a consistent polygon.
			struct HoleInfo {
				std::size_t  holeIndex;
				std::size_t  maxXVert; // position within the hole of its rightmost vertex
				std::int64_t maxX;
			};
			std::vector<HoleInfo> order;
			order.reserve(holes.size());
			for (std::size_t h = 0; h < holes.size(); ++h) {
				const std::vector<std::uint32_t>& hole = holes[h];
				std::size_t	 mv	  = 0;
				std::int64_t mx	  = vertices[hole[0]].x;
				for (std::size_t k = 1; k < hole.size(); ++k) {
					const Vec2i64& v = vertices[hole[k]];
					if (v.x > mx || (v.x == mx && v.y < vertices[hole[mv]].y)) {
						mx = v.x;
						mv = k;
					}
				}
				order.push_back({h, mv, mx});
			}
			std::sort(order.begin(), order.end(), [](const HoleInfo& a, const HoleInfo& b) {
				if (a.maxX != b.maxX) {
					return a.maxX > b.maxX;
				}
				return a.holeIndex < b.holeIndex; // deterministic tie-break
			});

			for (std::size_t oi = 0; oi < order.size(); ++oi) {
				const HoleInfo&					  hi   = order[oi];
				const std::vector<std::uint32_t>& hole = holes[hi.holeIndex];
				const Vec2i64&					  M	   = vertices[hole[hi.maxXVert]];

				// A candidate bridge to loop position `t` is acceptable iff it is clear of
				// the current loop, of every not-yet-merged hole, AND of THIS hole's own
				// body. The +x ray bridges rightward from the hole's rightmost vertex, so
				// it never re-enters the hole; but the brute-force fallback may aim in any
				// direction, and a leftward bridge from the rightmost vertex can slice back
				// across the hole's own (possibly non-convex) outline -- that self-crossing
				// is what was leaving a non-simple merged loop.
				auto acceptable = [&](std::size_t t) -> bool {
					if (!bridgeIsClear(vertices, loop, M, t)) {
						return false;
					}
					const Vec2i64& T = vertices[loop[t]];
					if (bridgeHitsHole(vertices, hole, M, T)) {
						return false; // bridge re-crosses the hole it is merging
					}
					for (std::size_t oj = oi + 1; oj < order.size(); ++oj) {
						if (bridgeHitsHole(vertices, holes[order[oj].holeIndex], M, T)) {
							return false;
						}
					}
					return true;
				};

				// Fast path: the Eberly +x-ray bridge. Validate it does not foul the
				// current loop or an unmerged hole (a slanted bridge can slice a hole the
				// horizontal ray never straddled).
				std::size_t bridgePos = findBridgePosition(vertices, loop, M);
				if (bridgePos != SIZE_MAX && !acceptable(bridgePos)) {
					bridgePos = SIZE_MAX; // fouled: fall through to the exact search
				}
				// Robust fallback: bridge to the NEAREST loop vertex the bridge can reach
				// without crossing the loop, this hole, or any unmerged hole. Guarantees a
				// valid bridge whenever the hole is genuinely interior (inputIsValid already
				// verified containment and disjointness), so a many-hole face never strands
				// a hole and leaves earClip a non-simple polygon. Candidates are tried in
				// ascending distance and we stop at the first acceptable one, so the
				// expensive unmerged-hole scan runs only until a valid bridge is found
				// rather than for every loop vertex.
				if (bridgePos == SIZE_MAX) {
					std::vector<std::size_t> cand(loop.size());
					for (std::size_t i = 0; i < loop.size(); ++i) {
						cand[i] = i;
					}
					std::sort(cand.begin(), cand.end(), [&](std::size_t a, std::size_t b) {
						return bridgeLenSq(vertices, loop, M, a) < bridgeLenSq(vertices, loop, M, b);
					});
					for (std::size_t i : cand) {
						if (acceptable(i)) {
							bridgePos = i;
							break;
						}
					}
					if (bridgePos == SIZE_MAX) {
						return {}; // no visible bridge at all: invalid containment
					}
				}

				// Splice: outer[..bridgePos], then the hole starting at its rightmost
				// vertex walked in its own (CW) order, back to that vertex, then the
				// bridge vertex again, then the rest of outer. The bridge edge
				// (loop[bridgePos] <-> M) is thus traversed in both directions.
				std::vector<std::uint32_t> spliced;
				spliced.reserve(loop.size() + hole.size() + 2);
				for (std::size_t i = 0; i <= bridgePos; ++i) {
					spliced.push_back(loop[i]);
				}
				const std::size_t hn = hole.size();
				for (std::size_t k = 0; k < hn; ++k) {
					spliced.push_back(hole[(hi.maxXVert + k) % hn]);
				}
				spliced.push_back(hole[hi.maxXVert]);	  // close the hole walk
				spliced.push_back(loop[bridgePos]);		  // return across the bridge
				for (std::size_t i = bridgePos + 1; i < loop.size(); ++i) {
					spliced.push_back(loop[i]);
				}
				loop = std::move(spliced);
			}

			return loop;
		}

		// ---- Ear clipping --------------------------------------------------------
		//
		// Operates on the merged loop (positions 0..L-1; each position references a
		// vertex index, possibly repeated at bridges). Triangles are emitted in
		// vertex-index space.

		std::vector<Tri> earClip(const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& loop) {
			const std::size_t L = loop.size();
			if (L < 3) {
				return {};
			}

			// Doubly-linked ring over loop positions.
			std::vector<std::size_t> next(L);
			std::vector<std::size_t> prev(L);
			for (std::size_t i = 0; i < L; ++i) {
				next[i] = (i + 1) % L;
				prev[i] = (i + L - 1) % L;
			}
			std::vector<bool> removed(L, false);

			auto pos = [&](std::size_t loopPos) -> const Vec2i64& { return vertices[loop[loopPos]]; };

			// Turn direction at loop position p, using its current neighbors.
			// CCW = convex, CW = reflex, Collinear = a straight (180-degree) corner.
			auto turnAt = [&](std::size_t p) {
				return orientation(pos(prev[p]), pos(p), pos(next[p]));
			};

			// Is the cut at p obstructed? p is an ear candidate only if NOT reflex
			// (handled by the caller via turnAt). The blocker test is STRICT: a vertex
			// on the triangle's boundary does not block, EXCEPT one strictly interior
			// to the prospective diagonal (prev,next), which would leave that diagonal
			// cutting through the boundary. We test every other vertex (not just reflex
			// ones) so repeated bridge vertices and collinear features stay correct;
			// coincident-corner hits are skipped. For a zero-area (collinear) candidate
			// the diagonal is the existing straight edge, so nothing can be strictly
			// inside and only the on-diagonal guard applies (a valid simple ring has no
			// such vertex).
			auto cutIsClear = [&](std::size_t p) {
				const std::size_t a = prev[p];
				const std::size_t c = next[p];
				const Vec2i64&	  A = pos(a);
				const Vec2i64&	  B = pos(p);
				const Vec2i64&	  C = pos(c);
				const bool		  degenerate = (orientation(A, B, C) == Orientation::Collinear);
				for (std::size_t q = next[c]; q != a; q = next[q]) {
					if (q == p) {
						continue;
					}
					const Vec2i64& Pq = pos(q);
					// A query point coincident with a corner (bridge duplicates) does
					// not block: it sits exactly on the ear and shares the corner.
					if (Pq == A || Pq == B || Pq == C) {
						continue;
					}
					if (!degenerate && strictlyInsideCcwTriangle(Pq, A, B, C)) {
						return false;
					}
					if (onOpenSegment(Pq, A, C)) {
						return false;
					}
				}
				return true;
			};

			std::vector<Tri> tris;
			tris.reserve(L);

			auto clip = [&](std::size_t p, bool emit) {
				const std::size_t a = prev[p];
				const std::size_t c = next[p];
				if (emit) {
					tris.push_back({loop[a], loop[p], loop[c]});
				}
				next[a]	   = c;
				prev[c]	   = a;
				removed[p] = true;
			};

			// A live (not-yet-removed) ring node to start each scan from.
			std::size_t liveStart = 0;
			while (removed[liveStart]) {
				++liveStart;
			}

			// Find a clippable ear of the requested turn type among the live nodes,
			// or L if none. Removed nodes are unlinked, so walking `next` visits only
			// live nodes; `remaining` bounds the walk.
			std::size_t remaining = L;
			auto		findEar	  = [&](Orientation want) -> std::size_t {
				std::size_t cur = liveStart;
				for (std::size_t k = 0; k < remaining; ++k, cur = next[cur]) {
					if (turnAt(cur) == want && cutIsClear(cur)) {
						return cur;
					}
				}
				return L;
			};

			// Clip strictly-convex ears first (emitting a positive-area triangle);
			// only when a full sweep finds none, absorb a collinear (straight, zero-
			// area) corner -- unlink it WITHOUT emitting its degenerate triangle. This
			// keeps the n-2 triangle count for rings without forced collinear
			// absorption and absorbs a collinear vertex only when it is the sole way
			// forward (the T-junction split-point case). The two-ears theorem
			// guarantees a strictly-convex ear exists whenever a strictly-convex vertex
			// remains; a collinear corner is always clippable since nothing can lie
			// strictly inside a zero-area triangle. Each iteration removes one node, so
			// the loop terminates.
			while (remaining > 3) {
				std::size_t ear	 = findEar(Orientation::CounterClockwise);
				bool		emit = true;
				if (ear == L) {
					ear	 = findEar(Orientation::Collinear);
					emit = false;
				}
				if (ear == L) {
					return {}; // no clippable ear at all: degenerate, reject
				}
				if (liveStart == ear) {
					liveStart = next[ear]; // ear's successor stays live across the clip
				}
				clip(ear, emit);
				--remaining;
			}

			// Final triangle from the three survivors. Skip it if collinear (zero
			// area): a valid positive-area ring never reduces to a degenerate final
			// triple, so this only guards against emitting a bad triangle.
			std::size_t s0 = 0;
			while (removed[s0]) {
				++s0;
			}
			std::size_t s1 = next[s0];
			std::size_t s2 = next[s1];
			if (orientation(pos(s0), pos(s1), pos(s2)) != Orientation::Collinear) {
				tris.push_back({loop[s0], loop[s1], loop[s2]});
			}

			return tris;
		}

		// ---- Delaunay (Lawson) flips --------------------------------------------
		//
		// Treat the emitted triangles as a mesh. For every interior edge (shared by
		// two triangles and not a constraint edge), if the opposite vertex of one
		// triangle lies inside the circumcircle of the other, flip the diagonal.
		// Constraint edges (outer + hole boundary) are locked. Termination is
		// guaranteed because each flip strictly increases the (finite) sum over
		// triangles of the min-angle-ordering; OnCircle never flips.

		std::uint64_t edgeKey(std::uint32_t a, std::uint32_t b) {
			if (a > b) {
				std::swap(a, b);
			}
			return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
		}

		// Ensure triangle is CCW (swap two vertices if not). Returns false if
		// degenerate (collinear) -- shouldn't happen for a valid clip.
		bool makeCcw(Tri& t, const std::vector<Vec2i64>& vertices) {
			const Orientation o = orientation(vertices[t[0]], vertices[t[1]], vertices[t[2]]);
			if (o == Orientation::Collinear) {
				return false;
			}
			if (o == Orientation::Clockwise) {
				std::swap(t[1], t[2]);
			}
			return true;
		}

		// The vertex of triangle t that is not u or v.
		std::uint32_t apex(const Tri& t, std::uint32_t u, std::uint32_t v) {
			for (std::uint32_t x : t) {
				if (x != u && x != v) {
					return x;
				}
			}
			return t[0]; // unreachable for a valid (u,v) edge of t
		}

		void delaunayFlip(std::vector<Tri>& tris, const std::vector<Vec2i64>& vertices,
						   const std::unordered_map<std::uint64_t, int>& constraintEdges) {
			const std::size_t triCount = tris.size();

			// Edge -> the (at most two) triangle indices sharing it.
			std::unordered_map<std::uint64_t, std::array<int, 2>> edgeTris;
			edgeTris.reserve(triCount * 3);
			auto addEdge = [&](std::uint32_t a, std::uint32_t b, int ti) {
				const std::uint64_t k	= edgeKey(a, b);
				auto				it	= edgeTris.find(k);
				if (it == edgeTris.end()) {
					edgeTris.emplace(k, std::array<int, 2>{ti, -1});
				} else {
					if (it->second[1] == -1 && it->second[0] != ti) {
						it->second[1] = ti;
					}
				}
			};
			auto removeEdgeTri = [&](std::uint32_t a, std::uint32_t b, int ti) {
				auto it = edgeTris.find(edgeKey(a, b));
				if (it == edgeTris.end()) {
					return;
				}
				if (it->second[0] == ti) {
					it->second[0] = it->second[1];
					it->second[1] = -1;
				} else if (it->second[1] == ti) {
					it->second[1] = -1;
				}
			};
			auto registerTri = [&](int ti) {
				addEdge(tris[ti][0], tris[ti][1], ti);
				addEdge(tris[ti][1], tris[ti][2], ti);
				addEdge(tris[ti][2], tris[ti][0], ti);
			};
			auto unregisterTri = [&](int ti) {
				removeEdgeTri(tris[ti][0], tris[ti][1], ti);
				removeEdgeTri(tris[ti][1], tris[ti][2], ti);
				removeEdgeTri(tris[ti][2], tris[ti][0], ti);
			};

			for (int ti = 0; ti < static_cast<int>(triCount); ++ti) {
				registerTri(ti);
			}

			auto isConstraint = [&](std::uint32_t a, std::uint32_t b) {
				return constraintEdges.find(edgeKey(a, b)) != constraintEdges.end();
			};

			// Work queue of edges to test. Seed with every interior edge.
			std::vector<std::uint64_t> queue;
			queue.reserve(edgeTris.size());
			for (const auto& kv : edgeTris) {
				if (kv.second[1] != -1) {
					queue.push_back(kv.first);
				}
			}

			std::size_t qi	  = 0;
			std::size_t guard = 0;
			// Each flip strictly improves the triangulation toward Delaunay and the
			// configuration count is finite; a generous cap guards against any
			// bookkeeping slip without affecting correctness (the clip is already a
			// valid covering).
			const std::size_t guardMax = triCount * triCount * 4 + 64;

			while (qi < queue.size()) {
				if (guard++ > guardMax) {
					break; // safety: stop flipping, keep the valid mesh
				}
				const std::uint64_t k = queue[qi++];
				auto				it = edgeTris.find(k);
				if (it == edgeTris.end()) {
					continue;
				}
				const int t0 = it->second[0];
				const int t1 = it->second[1];
				if (t0 == -1 || t1 == -1) {
					continue; // boundary now (or stale)
				}

				const std::uint32_t a = static_cast<std::uint32_t>(k >> 32);
				const std::uint32_t b = static_cast<std::uint32_t>(k & 0xffffffffULL);
				if (isConstraint(a, b)) {
					continue; // locked edge
				}

				const std::uint32_t c = apex(tris[t0], a, b);
				const std::uint32_t d = apex(tris[t1], a, b);
				if (c == d) {
					continue; // degenerate adjacency
				}

				// Orient (a,b,c) CCW so inCircle's precondition holds, then test d.
				Vec2i64 A = vertices[a];
				Vec2i64 B = vertices[b];
				Vec2i64 C = vertices[c];
				if (orientation(A, B, C) == Orientation::Clockwise) {
					std::swap(A, B);
				}
				if (orientation(A, B, C) != Orientation::CounterClockwise) {
					continue; // collinear triangle: nothing sensible to flip
				}

				if (inCircle(A, B, C, vertices[d]) != InCircle::Inside) {
					continue; // already locally Delaunay (Outside or OnCircle)
				}

				// Flip: the new diagonal is c-d. New triangles: (a,c,d) and (b,d,c)
				// (orientation fixed up below). The quad a-c-b-d must be convex for
				// the flip to be valid; inCircle Inside on a valid clip implies a
				// convex quad here, but guard with an orientation check to be safe.
				if (orientation(vertices[c], vertices[d], vertices[a]) == Orientation::Collinear ||
					orientation(vertices[c], vertices[d], vertices[b]) == Orientation::Collinear) {
					continue;
				}

				// Unregister the four boundary edges of the old pair plus the diagonal.
				unregisterTri(t0);
				unregisterTri(t1);

				tris[t0] = {a, c, d};
				tris[t1] = {b, d, c};
				makeCcw(tris[t0], vertices);
				makeCcw(tris[t1], vertices);

				registerTri(t0);
				registerTri(t1);

				// Re-queue the four outer edges of the flipped quad; their local
				// Delaunay status may have changed.
				queue.push_back(edgeKey(a, c));
				queue.push_back(edgeKey(c, b));
				queue.push_back(edgeKey(b, d));
				queue.push_back(edgeKey(d, a));
				queue.push_back(edgeKey(c, d));
			}
		}

		// Build the constraint-edge set (undirected) from the outer ring and holes.
		std::unordered_map<std::uint64_t, int> buildConstraintEdges(const std::vector<std::uint32_t>& outer,
																	const std::vector<std::vector<std::uint32_t>>& holes) {
			std::unordered_map<std::uint64_t, int> edges;
			auto addRing = [&](const std::vector<std::uint32_t>& ring) {
				const std::size_t n = ring.size();
				for (std::size_t i = 0; i < n; ++i) {
					edges[edgeKey(ring[i], ring[(i + 1) % n])] = 1;
				}
			};
			addRing(outer);
			for (const auto& h : holes) {
				addRing(h);
			}
			return edges;
		}

		// Validate the structural preconditions cheaply (exactly) before any work.
		// Wrong winding, non-simple rings, out-of-range indices, holes not strictly
		// inside the outer ring, or holes/outer touching are all rejected.
		bool inputIsValid(const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& outer,
						  const std::vector<std::vector<std::uint32_t>>& holes) {
			if (outer.size() < 3) {
				return false;
			}
			const std::size_t vn = vertices.size();
			auto inRange = [&](const std::vector<std::uint32_t>& r) {
				for (std::uint32_t i : r) {
					if (i >= vn) {
						return false;
					}
				}
				return true;
			};
			if (!inRange(outer)) {
				return false;
			}
			for (const auto& h : holes) {
				if (h.size() < 3 || !inRange(h)) {
					return false;
				}
			}

			const std::vector<Vec2i64> outerPts = ringPoints(vertices, outer);
			if (!ringIsSimple(outerPts)) {
				return false;
			}
			if (signedAreaDoubled(outerPts).sign() <= 0) {
				return false; // outer must be CCW (positive area)
			}

			std::vector<std::vector<Vec2i64>> holePts;
			holePts.reserve(holes.size());
			for (const auto& h : holes) {
				std::vector<Vec2i64> pts = ringPoints(vertices, h);
				if (!ringIsSimple(pts)) {
					return false;
				}
				if (signedAreaDoubled(pts).sign() >= 0) {
					return false; // holes must be CW (negative area)
				}
				// Every hole vertex must be strictly inside the outer ring.
				for (const Vec2i64& p : pts) {
					if (pointInPolygon(p, outerPts) != PointInPolygon::Inside) {
						return false;
					}
				}
				holePts.push_back(std::move(pts));
			}

			// Holes must be mutually disjoint and not nested: no hole vertex inside
			// another hole, and no hole edges crossing. The edge-cross check via the
			// merged-loop simplicity would also catch this, but rejecting up front
			// keeps the failure mode "return empty" rather than a bad clip.
			for (std::size_t i = 0; i < holePts.size(); ++i) {
				for (std::size_t j = 0; j < holePts.size(); ++j) {
					if (i == j) {
						continue;
					}
					for (const Vec2i64& p : holePts[i]) {
						if (pointInPolygon(p, holePts[j]) != PointInPolygon::Outside) {
							return false;
						}
					}
				}
			}

			// Vertex containment alone misses two cases: a concave outer ring lets a
			// hole EDGE bow out past the boundary with all its vertices still inside,
			// and two holes can interleave so an edge of one crosses an edge of the
			// other with no vertex contained. Holes are disjoint from the outer ring
			// and from each other, so any non-Disjoint relation between a hole edge
			// and an outer edge (or an edge of a different hole) is a rejection: a
			// proper crossing, a collinear overlap, or even an endpoint touch (a hole
			// legitimately shares nothing with the outer or another hole). O(n*m)
			// exact; rings are small per navmesh chunk.
			auto ringsTouch = [](const std::vector<Vec2i64>& ra, const std::vector<Vec2i64>& rb) {
				const std::size_t na = ra.size();
				const std::size_t nb = rb.size();
				for (std::size_t i = 0; i < na; ++i) {
					const Vec2i64& a0 = ra[i];
					const Vec2i64& a1 = ra[(i + 1) % na];
					for (std::size_t j = 0; j < nb; ++j) {
						const Vec2i64& b0 = rb[j];
						const Vec2i64& b1 = rb[(j + 1) % nb];
						if (intersectSegments(a0, a1, b0, b1).relation != SegmentRelation::Disjoint) {
							return true;
						}
					}
				}
				return false;
			};

			for (std::size_t i = 0; i < holePts.size(); ++i) {
				if (ringsTouch(holePts[i], outerPts)) {
					return false; // hole edge meets the outer boundary
				}
				for (std::size_t j = i + 1; j < holePts.size(); ++j) {
					if (ringsTouch(holePts[i], holePts[j])) {
						return false; // two holes' edges meet
					}
				}
			}

			return true;
		}

	} // namespace

	std::vector<std::array<std::uint32_t, 3>> triangulateWithHoles(
		const std::vector<Vec2i64>& vertices, const std::vector<std::uint32_t>& outer,
		const std::vector<std::vector<std::uint32_t>>& holes) {
		if (!inputIsValid(vertices, outer, holes)) {
			return {};
		}

		std::vector<std::uint32_t> loop;
		if (holes.empty()) {
			loop = outer;
		} else {
			loop = mergeHoles(vertices, outer, holes);
			if (loop.empty()) {
				return {};
			}
		}

		std::vector<Tri> tris = earClip(vertices, loop);
		if (tris.empty()) {
			return {};
		}

		// Normalize every emitted triangle to CCW before the flip pass.
		for (Tri& t : tris) {
			if (!makeCcw(t, vertices)) {
				return {}; // a degenerate (collinear) triangle means a bad clip
			}
		}

		const std::unordered_map<std::uint64_t, int> constraints = buildConstraintEdges(outer, holes);
		delaunayFlip(tris, vertices, constraints);

		// Final orientation guarantee.
		for (Tri& t : tris) {
			makeCcw(t, vertices);
		}

		return tris;
	}

	std::vector<std::array<std::uint32_t, 3>> triangulateSimple(const std::vector<Vec2i64>& vertices,
																const std::vector<std::uint32_t>& ring) {
		return triangulateWithHoles(vertices, ring, {});
	}

} // namespace geometry
