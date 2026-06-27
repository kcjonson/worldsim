#include "NavMesh.h"

#include "../arrangement/Arrangement.h"
#include "../arrangement/HalfEdge.h"
#include "../core/Int128.h"
#include "../predicates/Predicates.h"
#include "../triangulation/Triangulation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace geometry::nav {

	namespace {

		// Canonical undirected edge key over vertex indices (min, max).
		using EdgeKey = std::uint64_t;

		EdgeKey makeEdgeKey(std::uint32_t a, std::uint32_t b) {
			if (a > b) {
				std::swap(a, b);
			}
			return (static_cast<std::uint64_t>(a) << 32) | static_cast<std::uint64_t>(b);
		}

		// A bounded face awaiting triangulation: its CCW outer ring (vertex indices
		// into mesh.vertices) plus the CW hole rings nested inside it. `blocker` and
		// `opening` carry the per-face belief tags onto every triangle the face emits:
		// floor faces get kNoBlocker/kNoOpening, faces inside a blocked ring get that
		// ring's provenanceId/openingId. Wall interiors are kept (not discarded) so a
		// belief query can optimistically path through an unseen wall.
		struct WalkableFace {
			std::vector<std::uint32_t>				outer;
			std::vector<std::vector<std::uint32_t>> holes;
			Int128									areaDoubled; // positive (CCW); innermost-container key
			Vec2i64									repPoint;	 // interior point used to classify the face
			std::int64_t							blocker = kNoBlocker;
			std::int64_t							opening = kNoOpening;
		};

		std::vector<std::uint32_t> faceRingIndices(const HalfEdgeMesh& mesh, const Face& f) {
			std::vector<std::uint32_t> ring;
			ring.reserve(f.halfEdges.size());
			for (std::size_t he : f.halfEdges) {
				ring.push_back(static_cast<std::uint32_t>(mesh.halfEdges[he].origin));
			}
			return ring;
		}

		std::vector<Vec2i64> ringPoints(const HalfEdgeMesh& mesh, const std::vector<std::uint32_t>& ring) {
			std::vector<Vec2i64> pts;
			pts.reserve(ring.size());
			for (std::uint32_t idx : ring) {
				pts.push_back(mesh.vertices[idx]);
			}
			return pts;
		}

		// True when `p` lies inside the outer ring and strictly outside every hole, i.e.
		// in the face's actual (annular) interior.
		bool inFaceInterior(const Vec2i64& p, const std::vector<Vec2i64>& outerPts,
							const std::vector<std::vector<Vec2i64>>& holePts) {
			if (pointInPolygon(p, outerPts) != PointInPolygon::Inside) {
				return false;
			}
			for (const std::vector<Vec2i64>& h : holePts) {
				if (pointInPolygon(p, h) != PointInPolygon::Outside) {
					return false; // inside or on the boundary of a hole
				}
			}
			return true;
		}

		// A representative point in the face's TRUE interior (inside `outer`, strictly
		// outside every hole). The face-extraction rep point only considers the outer
		// cycle, so for a face with holes it can land in a hole; classifying from there
		// reads the hole region's depth, not the face's. We keep that point when it is
		// already hole-free, else search for one that clears all holes.
		//
		// The search samples points near the OUTER boundary, where the annular interior
		// is always present (the holes are strict sub-regions, so the band just inside
		// the outer cycle is hole-free). For each outer vertex we step a short way toward
		// the outer centroid at a few fractions; a point hugging the boundary lands in
		// the annulus even when the outer-cycle ear centroids all cluster inside a
		// central hole (e.g. a square area whose dry land is a centered island). Falls
		// back to the original rep point if no integer sample lands clear, leaving prior
		// behavior unchanged.
		Vec2i64 representativeOutsideHoles(const HalfEdgeMesh& mesh, const std::vector<std::uint32_t>& outer,
										   const std::vector<std::vector<std::uint32_t>>& holes, const Vec2i64& fallback) {
			const std::vector<Vec2i64> outerPts = ringPoints(mesh, outer);
			std::vector<std::vector<Vec2i64>> holePts;
			holePts.reserve(holes.size());
			for (const std::vector<std::uint32_t>& h : holes) {
				holePts.push_back(ringPoints(mesh, h));
			}
			if (holePts.empty() || inFaceInterior(fallback, outerPts, holePts)) {
				return fallback;
			}

			const std::size_t n = outerPts.size();
			if (n == 0) {
				return fallback;
			}
			Vec2i64 c{0, 0};
			for (const Vec2i64& p : outerPts) {
				c.x += p.x;
				c.y += p.y;
			}
			c.x /= static_cast<std::int64_t>(n);
			c.y /= static_cast<std::int64_t>(n);

			// Step each outer vertex toward the centroid by num/den; small fractions hug
			// the boundary (hole-free band), larger ones reach a centered interior if the
			// boundary band is itself blocked. Numerators chosen low-to-high.
			constexpr std::int64_t den = 16;
			for (std::int64_t num : {1, 2, 3, 4, 6, 8}) {
				for (const Vec2i64& v : outerPts) {
					const Vec2i64 sample{v.x + (c.x - v.x) * num / den, v.y + (c.y - v.y) * num / den};
					if (inFaceInterior(sample, outerPts, holePts)) {
						return sample;
					}
				}
			}
			return fallback;
		}

		// |2*area| of a polygon ring (shoelace), exact in 128-bit. Used only to rank
		// containing blocked rings by size (smallest-containing wins); sign irrelevant.
		Int128 signedAreaDoubledAbs(const std::vector<Vec2i64>& ring) {
			Int128 acc(0);
			const std::size_t n = ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				acc = acc + cross(ring[i], ring[(i + 1) % n]);
			}
			return acc.sign() < 0 ? Int128(0) - acc : acc;
		}

		// --- Demyen-Buro corridor width (exact integer) ------------------------------
		// Grounded in Demyen 2006, "Efficient Triangulation-Based Pathfinding", sec. 4.1
		// (Algorithms 1-3). The width between two edges of a triangle that share apex C
		// is the diameter of the largest disc that can pass between them, which equals
		// the distance from C to the nearest obstacle in the wedge between the edges.
		//
		// Two deliberate departures from the paper, both forced by our merged-mesh model
		// (floor + obstacle interiors triangulated as one graph, not "every vertex is an
		// obstacle"):
		//   1. A triangle vertex is an obstacle ONLY if it is an endpoint of a common-
		//      knowledge obstacle edge; a bare floor/border vertex is not. So the width
		//      is seeded UNCONSTRAINED, and the base vertices contribute a bound only
		//      when they are obstacle vertices. This is what keeps an open-floor sliver
		//      (no incident obstacle edges) unconstrained instead of falsely narrow.
		//   2. "Constrained edge" in the paper == "common-knowledge obstacle edge" here:
		//      a triangle edge whose two incident faces differ in common-knowledge-
		//      blockedness (the boundary of a water/flora region). Belief-dependent walls
		//      (faceBlocker > 0) are NOT obstacles for this pass (see NavMesh.h).
		// All obstacles in our model are filled polygons, so every obstacle vertex has
		// incident obstacle edges (no bare point obstacles); the paper's edge-based
		// search therefore captures every obstacle the disc must round.

		bool isCommonKnowledgeBlocked(std::int64_t faceBlocker) {
			return faceBlocker < 0 && faceBlocker != kNoBlocker;
		}

		// Edge e of triangle ti is an obstacle edge iff exactly one of the two faces it
		// separates is common-knowledge-blocked (the boundary of a water/flora region).
		// A mesh-boundary edge (neighbor == -1) has no obstacle face outside it, so a
		// floor border is not an obstacle edge (scope: terrain/flora only).
		bool isObstacleEdge(const NavMesh& mesh, std::int32_t ti, int e) {
			const bool self	 = isCommonKnowledgeBlocked(mesh.triangles[ti].faceBlocker);
			const std::int32_t nb = mesh.triangles[ti].neighbor[e];
			const bool other = nb < 0 ? false : isCommonKnowledgeBlocked(mesh.triangles[nb].faceBlocker);
			return self != other;
		}

		// Is the angle of triangle (C, U, V) at vertex U strictly obtuse? True iff the
		// foot of the perpendicular from C to line UV falls strictly beyond U (so U is
		// the closest point of segment UV to C). Exact: sign of dot(U->C, U->V).
		bool obtuseAt(const Vec2i64& c, const Vec2i64& u, const Vec2i64& v) {
			return dot(c - u, v - u).sign() < 0;
		}

		// floor(sqrt(S)) for an exact non-negative squared length S (mm^2). Binary
		// search on k with k*k <= S, all in Int128; no floating point.
		std::int64_t floorSqrtSquared(const Int128& s) {
			if (s.sign() <= 0) {
				return 0;
			}
			std::int64_t lo = 0;
			// hi = floor(sqrt(INT64_MAX)); k*k stays in int64 range as Int128. Safe because
			// the only inputs are squared distances between mesh vertices, i.e. coordinate
			// DELTAS bounded by the loaded-region span (a few chunks, a few million mm),
			// squared ~1e13, far under INT64_MAX -- not because coordinates are small (they
			// are world-absolute mm). A navmesh spanning >~2,000 km would break this.
			std::int64_t hi = 3037000499;
			while (lo < hi) {
				const std::int64_t mid = lo + (hi - lo + 1) / 2;
				if (Int128::product(mid, mid) <= s) {
					lo = mid;
				} else {
					hi = mid - 1;
				}
			}
			return lo;
		}

		// floor(sqrt(crossVal^2 / den)) for the perpendicular distance from a point to a
		// line: the squared distance is the rational crossVal^2 / den (den = |edge|^2 > 0).
		// Largest k with k*k*den <= crossVal^2, decided exactly by compareSquareToProduct
		// (sign of crossVal^2 - (k*k)*den). No division, no floating point.
		std::int64_t floorSqrtRational(const Int128& crossVal, const Int128& den) {
			if (crossVal.sign() == 0 || den.sign() <= 0) {
				return 0;
			}
			std::int64_t lo = 0;
			std::int64_t hi = 3037000499;
			while (lo < hi) {
				const std::int64_t mid	= lo + (hi - lo + 1) / 2;
				const Int128	   kSq	= Int128::product(mid, mid);
				// admissible iff crossVal^2 - kSq*den >= 0
				if (Int128::compareSquareToProduct(crossVal, kSq, den) >= 0) {
					lo = mid;
				} else {
					hi = mid - 1;
				}
			}
			return lo;
		}

		// Distance (floored to mm) from point c to the segment [u,v], as a clearance:
		// the perpendicular distance when the foot lies on the segment, else the nearer
		// endpoint distance. Caller guarantees the foot is interior (acute at both ends)
		// for the perpendicular branch; the endpoint branches stay correct regardless.
		std::int64_t floorDistancePointToSegment(const Vec2i64& c, const Vec2i64& u, const Vec2i64& v) {
			const Vec2i64 uv = v - u;
			const Vec2i64 uc = c - u;
			if (uv.x == 0 && uv.y == 0) {
				return floorSqrtSquared(dot(uc, uc));
			}
			const Int128 t = dot(uc, uv);
			if (t.sign() <= 0) {
				return floorSqrtSquared(dot(uc, uc)); // foot before u
			}
			const Int128 den = dot(uv, uv);
			if (t >= den) {
				const Vec2i64 vc = c - v;
				return floorSqrtSquared(dot(vc, vc)); // foot past v
			}
			return floorSqrtRational(cross(uv, uc), den); // perpendicular, foot interior
		}

		std::int64_t minWidth(std::int64_t a, std::int64_t b) {
			return a < b ? a : b;
		}

		// Per-query scratch for the width search: a generation stamp per triangle so the
		// recursion never re-enters a triangle within one CalculateWidth call. Demyen
		// proves (Thm 4.3.4/4.3.6) a CDT width search never revisits a triangle, so this
		// only fires as a defensive guard against a non-Delaunay sliver, where it
		// terminates safely without dropping any reachable obstacle (an already-visited
		// triangle was fully explored from this apex). One buffer is reused across the
		// whole build via an incrementing stamp, so there is no per-call clearing.
		struct WidthScratch {
			std::vector<std::int32_t> visited; // visited[ti] == stamp -> seen this query
			std::int32_t			  stamp = 0;
		};

		// Demyen Algorithm 2: SearchWidth. Walk across non-obstacle edges from apex C,
		// bounded above by the closest obstacle found so far (`bound`, a floored mm
		// distance), returning the tightened bound. `ti`/`e` is the edge being entered.
		std::int64_t searchWidth(
			const NavMesh& mesh, WidthScratch& scratch, const Vec2i64& c, std::int32_t ti, int e, std::int64_t bound) {
			const NavTriangle& tri = mesh.triangles[ti];
			const Vec2i64&	   u   = mesh.vertices[tri.v[e]];
			const Vec2i64&	   v   = mesh.vertices[tri.v[(e + 1) % 3]];

			// Prune: if the foot of the perpendicular from C to line UV is outside the
			// segment, no obstacle reachable across this edge is in the wedge (Demyen
			// Fig. 4.13-4.14); the bound already reflects the nearer base vertex.
			if (obtuseAt(c, u, v) || obtuseAt(c, v, u)) {
				return bound;
			}
			const std::int64_t d = floorDistancePointToSegment(c, u, v);
			if (d >= bound) {
				return bound; // nothing closer this way
			}
			if (isObstacleEdge(mesh, ti, e)) {
				return d; // closest obstacle is this constrained edge
			}
			// Unconstrained: recurse into the opposite triangle's two other edges.
			const std::int32_t nb = tri.neighbor[e];
			if (nb < 0) {
				return bound; // no opposite triangle (mesh boundary), nothing to cross
			}
			if (scratch.visited[nb] == scratch.stamp) {
				return bound; // already explored from this apex (loop guard)
			}
			scratch.visited[nb] = scratch.stamp;
			const NavTriangle& opp = mesh.triangles[nb];
			int back = -1;
			for (int k = 0; k < 3; ++k) {
				if (opp.neighbor[k] == ti) {
					back = k;
					break;
				}
			}
			if (back < 0) {
				return bound; // adjacency inconsistency; bail safely
			}
			std::int64_t out = bound;
			out = searchWidth(mesh, scratch, c, nb, (back + 1) % 3, out);
			out = searchWidth(mesh, scratch, c, nb, (back + 2) % 3, out);
			return out;
		}

		// Demyen Algorithm 3: CalculateWidth, for the edge pair meeting at apex v[apex]
		// of triangle ti. Edges incident to the apex are `apex` and `(apex+2)%3`; the
		// opposite edge c is `(apex+1)%3`, between base vertices A=v[(apex+1)%3] and
		// B=v[(apex+2)%3]. Returns the floored max disc diameter (mm), or
		// kUnconstrainedWidth when no common-knowledge obstacle pinches the passage.
		std::int64_t calculateEdgePairWidth(const NavMesh& mesh, WidthScratch& scratch, std::int32_t ti, int apex) {
			const NavTriangle& tri = mesh.triangles[ti];
			const int		   cEdge = (apex + 1) % 3; // edge opposite the apex
			const Vec2i64&	   c	 = mesh.vertices[tri.v[apex]];
			const Vec2i64&	   a	 = mesh.vertices[tri.v[cEdge]];			// base vertex A
			const Vec2i64&	   b	 = mesh.vertices[tri.v[(apex + 2) % 3]]; // base vertex B

			// A base vertex bounds the width only if it is an obstacle vertex, i.e. an
			// endpoint of an incident obstacle edge. Edge cEdge = (A,B); edge `apex` =
			// (C,A)? No: edge `apex` is (v[apex], v[apex+1]) = (C, A); edge (apex+2)%3 is
			// (v[apex+2], v[apex]) = (B, C). So A is shared by edges cEdge and apex; B is
			// shared by edges cEdge and (apex+2)%3.
			const bool aObstacle = isObstacleEdge(mesh, ti, cEdge) || isObstacleEdge(mesh, ti, apex);
			const bool bObstacle = isObstacleEdge(mesh, ti, cEdge) || isObstacleEdge(mesh, ti, (apex + 2) % 3);

			std::int64_t d = kUnconstrainedWidth;
			if (aObstacle) {
				const Vec2i64 ca = a - c;
				d = minWidth(d, floorSqrtSquared(dot(ca, ca)));
			}
			if (bObstacle) {
				const Vec2i64 cb = b - c;
				d = minWidth(d, floorSqrtSquared(dot(cb, cb)));
			}

			// Case 1: an obtuse base angle means the nearer base vertex is the closest
			// obstacle in the wedge and the search must not cross edge c (it would leave
			// the wedge). The bound above already captured that vertex.
			if (obtuseAt(c, a, b) || obtuseAt(c, b, a)) {
				return d;
			}
			// Case 2: opposite edge constrained -> perpendicular distance to it (which is
			// <= either base-vertex distance, the foot being interior here).
			if (isObstacleEdge(mesh, ti, cEdge)) {
				return minWidth(d, floorDistancePointToSegment(c, a, b));
			}
			// Case 3: opposite edge unconstrained -> search across it (the start triangle
			// is marked visited so the search never re-enters it).
			++scratch.stamp;
			scratch.visited[ti] = scratch.stamp;
			return searchWidth(mesh, scratch, c, ti, cEdge, d);
		}

		// --- Width-aware reachability forest (P3.3) ----------------------------------
		// Two max-spanning-forests over the triangle dual graph, sharing one set of
		// geometric edge capacities; they differ only in which triangles are nodes (the
		// traversability predicate). See NavMesh.h ReachabilityForest for the contract.

		// A portal between two triangles, with its sound disc-diameter capacity. Built
		// once over all interior edges, then filtered per forest by the node predicate.
		struct PortalEdge {
			std::int32_t triA = -1; // lower triangle index (canonical owner of the edge)
			std::int32_t triB = -1; // higher triangle index
			std::int64_t cap  = kUnconstrainedWidth;
		};

		// T-side capacity of portal edge `e` in triangle `ti`: the widest disc that can
		// cross the portal THROUGH ti. The disc enters via one of ti's OTHER two edges
		// and pivots about one of the portal's two endpoints (local vertices e and
		// (e+1)%3), so it may use the wider of the two apex widths; the max is the sound
		// upper bound (Demyen edge-pair width is keyed by apex).
		std::int64_t portalSideCap(const NavMesh& mesh, std::int32_t ti, int e) {
			const NavTriangle& t  = mesh.triangles[ti];
			const std::int64_t w0 = t.edgePairWidthMm[e];
			const std::int64_t w1 = t.edgePairWidthMm[(e + 1) % 3];
			return w0 > w1 ? w0 : w1;
		}

		// Capacity of the portal shared by T1 (edge eInT1) and T2: the min of each
		// side's capacity and the door clear width when the edge is a door portal. A
		// door span has unconstrained Demyen widths (walls are not common-knowledge
		// obstacles), so the door clear width is what binds there -- hence in the min.
		std::int64_t portalCapacity(const NavMesh& mesh, std::int32_t t1, int eInT1, std::int32_t t2) {
			const NavTriangle& tri1	   = mesh.triangles[t1];
			const std::int32_t backEdge = [&]() {
				for (int k = 0; k < 3; ++k) {
					if (mesh.triangles[t2].neighbor[k] == t1) {
						return k;
					}
				}
				return -1;
			}();
			std::int64_t cap = portalSideCap(mesh, t1, eInT1);
			if (backEdge >= 0) {
				cap = std::min(cap, portalSideCap(mesh, t2, backEdge));
			}
			if (tri1.edgeOpening[eInT1] != kNoOpening) {
				cap = std::min(cap, tri1.edgeClearWidthMm[eInT1]);
			}
			return cap;
		}

		// Union-find over triangle leaves, with the merge-tree node id carried on each
		// set so a union can wire the new internal node's children. find() uses path
		// compression; union is by the caller's deterministic edge order (no rank), so
		// the tree shape is reproducible.
		struct ForestBuilder {
			std::vector<std::int32_t> parent;	 // DSU parent over leaves [0,n)
			std::vector<std::int32_t> setNode;	 // current merge-tree root node for each DSU set
			std::vector<std::int64_t> nodeCap;	 // capacity stored at each merge-tree node
			std::vector<std::int32_t> nodeLeft;	 // left child of each internal node (-1 for leaves)
			std::vector<std::int32_t> nodeRight; // right child of each internal node (-1 for leaves)

			std::int32_t find(std::int32_t x) {
				while (parent[x] != x) {
					parent[x] = parent[parent[x]];
					x		  = parent[x];
				}
				return x;
			}
		};

		// Build one forest from the node predicate and the shared portal list. Edges are
		// processed in DESCENDING capacity (Kruskal max-spanning-forest); ties break by
		// (cap desc, lower triA, lower triB) for a reproducible tree. Only edges whose
		// BOTH endpoints are nodes participate. Returns the populated ReachabilityForest.
		template <typename NodePredicate>
		ReachabilityForest buildForest(
			const NavMesh& mesh, const std::vector<PortalEdge>& portals, NodePredicate isNode) {
			const std::int32_t n = static_cast<std::int32_t>(mesh.triangles.size());

			ForestBuilder fb;
			fb.parent.resize(n);
			fb.setNode.resize(n);
			// Leaves first; internal nodes appended on each union. At most n-1 unions, so
			// reserve 2n-1 to avoid reallocation invalidating nothing (we store indices).
			fb.nodeCap.assign(n, kUnconstrainedWidth);
			fb.nodeLeft.assign(n, -1);
			fb.nodeRight.assign(n, -1);
			fb.nodeCap.reserve(static_cast<std::size_t>(2) * n);
			fb.nodeLeft.reserve(static_cast<std::size_t>(2) * n);
			fb.nodeRight.reserve(static_cast<std::size_t>(2) * n);
			for (std::int32_t i = 0; i < n; ++i) {
				fb.parent[i]  = i;
				fb.setNode[i] = i; // each traversable leaf starts as its own merge-tree root
			}

			// Filter to participating edges (both endpoints are nodes), then sort by the
			// deterministic Kruskal order. Non-node triangles never appear, so they stay
			// singleton sets and get component -1 below.
			std::vector<PortalEdge> edges;
			edges.reserve(portals.size());
			for (const PortalEdge& pe : portals) {
				if (isNode(mesh.triangles[pe.triA]) && isNode(mesh.triangles[pe.triB])) {
					edges.push_back(pe);
				}
			}
			std::sort(edges.begin(), edges.end(), [](const PortalEdge& a, const PortalEdge& b) {
				if (a.cap != b.cap) {
					return a.cap > b.cap; // descending capacity
				}
				if (a.triA != b.triA) {
					return a.triA < b.triA; // then lower triangle index
				}
				return a.triB < b.triB; // then lower neighbor index
			});

			for (const PortalEdge& pe : edges) {
				const std::int32_t ra = fb.find(pe.triA);
				const std::int32_t rb = fb.find(pe.triB);
				if (ra == rb) {
					continue; // already connected; this edge closes a cycle
				}
				// New internal node merging the two subtree roots at this edge's capacity.
				const std::int32_t node = static_cast<std::int32_t>(fb.nodeCap.size());
				fb.nodeCap.push_back(pe.cap);
				fb.nodeLeft.push_back(fb.setNode[ra]);
				fb.nodeRight.push_back(fb.setNode[rb]);
				// Union (attach rb's set under ra) and route the set's merge-tree root to
				// the new internal node.
				fb.parent[rb]  = ra;
				fb.setNode[ra] = node;
			}

			// Assemble the ReachabilityForest: node capacities, then component ids and
			// binary-lifting LCA tables over the reconstruction-tree forest.
			ReachabilityForest forest;
			forest.nodeCap = std::move(fb.nodeCap);
			const std::int32_t nodeCount = static_cast<std::int32_t>(forest.nodeCap.size());

			// Parent of each merge-tree node (roots point to themselves), from the
			// child links recorded during the unions. depth via a root-down pass.
			std::vector<std::int32_t> nodeParent(nodeCount);
			for (std::int32_t i = 0; i < nodeCount; ++i) {
				nodeParent[i] = i; // default: root
			}
			for (std::int32_t i = n; i < nodeCount; ++i) {
				nodeParent[fb.nodeLeft[i]]  = i;
				nodeParent[fb.nodeRight[i]] = i;
			}

			// depth[]: roots have depth 0; a child is parent depth + 1. Processing nodes
			// in DESCENDING id is a valid topological order because every internal node
			// has a HIGHER id than both its children (created after them), so a parent's
			// depth is finalized before any child reads it.
			forest.depth.assign(nodeCount, 0);
			for (std::int32_t i = nodeCount - 1; i >= 0; --i) {
				if (nodeParent[i] != i) {
					forest.depth[i] = forest.depth[nodeParent[i]] + 1;
				}
			}

			// Binary-lifting table. levels = ceil(log2(maxDepth+1)); at least 1 so up[0]
			// always exists. up[0] is the direct parent (root -> itself).
			std::int32_t maxDepth = 0;
			for (std::int32_t i = 0; i < nodeCount; ++i) {
				maxDepth = std::max(maxDepth, forest.depth[i]);
			}
			std::int32_t levels = 1;
			while ((1 << levels) <= maxDepth) {
				++levels;
			}
			forest.levels = levels;
			forest.up.assign(static_cast<std::size_t>(levels), std::vector<std::int32_t>(nodeCount));
			for (std::int32_t i = 0; i < nodeCount; ++i) {
				forest.up[0][i] = nodeParent[i];
			}
			for (std::int32_t k = 1; k < levels; ++k) {
				for (std::int32_t i = 0; i < nodeCount; ++i) {
					forest.up[k][i] = forest.up[k - 1][forest.up[k - 1][i]];
				}
			}

			// component[]: a traversable leaf's component is its reconstruction-tree
			// root; a non-traversable leaf (never a node) is -1 (isolated/unreachable).
			forest.component.assign(n, -1);
			for (std::int32_t i = 0; i < n; ++i) {
				if (!isNode(mesh.triangles[i])) {
					continue;
				}
				std::int32_t r = i;
				for (std::int32_t k = levels - 1; k >= 0; --k) {
					r = forest.up[k][r]; // climb to the root (overshoot stops at root via self-loop)
				}
				forest.component[i] = r;
			}

			return forest;
		}

	} // namespace

	// LCA of two reconstruction-tree nodes a, b. Caller guarantees both are in the
	// SAME tree (same root); otherwise the climb meets at a shared root only if one
	// exists, so callers must check components first. Lift the deeper node up to the
	// shallower's depth, then climb both together.
	static std::int32_t forestLca(const ReachabilityForest& f, std::int32_t a, std::int32_t b) {
		if (f.depth[a] < f.depth[b]) {
			std::swap(a, b);
		}
		std::int32_t diff = f.depth[a] - f.depth[b];
		for (std::int32_t k = 0; k < f.levels; ++k) {
			if ((diff >> k) & 1) {
				a = f.up[k][a];
			}
		}
		if (a == b) {
			return a;
		}
		for (std::int32_t k = f.levels - 1; k >= 0; --k) {
			if (f.up[k][a] != f.up[k][b]) {
				a = f.up[k][a];
				b = f.up[k][b];
			}
		}
		return f.up[0][a]; // common parent
	}

	bool reachableInForest(const ReachabilityForest& f, std::int32_t triA, std::int32_t triB) {
		if (triA < 0 || triB < 0 || triA >= static_cast<std::int32_t>(f.component.size()) ||
			triB >= static_cast<std::int32_t>(f.component.size())) {
			return false;
		}
		if (f.component[triA] < 0 || f.component[triB] < 0) {
			return false; // a non-node endpoint is unreachable for this predicate
		}
		return f.component[triA] == f.component[triB];
	}

	std::int64_t bottleneckInForest(const ReachabilityForest& f, std::int32_t triA, std::int32_t triB) {
		if (!reachableInForest(f, triA, triB)) {
			return 0; // disconnected (or a non-node endpoint): admits no disc
		}
		if (triA == triB) {
			return kUnconstrainedWidth; // a triangle reaches itself with no squeeze
		}
		return f.nodeCap[forestLca(f, triA, triB)];
	}

	NavMesh buildNavMesh(const NavMeshInput& input) {
		NavMesh result;

		// Gather the walkable-bounds (unblocked) rings and the blocked rings. One
		// border is the common case, but multiple disjoint walkable regions are
		// supported: a face is in-bounds iff it lies inside ANY unblocked bound. Each
		// blocked ring carries its belief tags (provenanceId/openingId) so a face
		// landing inside it inherits them.
		struct BlockedRing {
			const std::vector<Vec2i64>* ring		  = nullptr;
			std::int64_t				provenance	  = kNoProvenance;
			std::int64_t				opening		  = kNoOpening;
			bool						holeCapable	  = false;	  // water: even-odd containment parity
			Int128						areaDoubledAbs = Int128(0); // |2*area|, cached for smallest-containing ranking
		};
		std::vector<const std::vector<Vec2i64>*> borderRings;
		std::vector<BlockedRing>				 blockedRings;
		for (const NavInputPolygon& poly : input.polygons) {
			if (poly.ring.size() < 3) {
				continue;
			}
			if (poly.blocked) {
				// Cache the ring's |2*area| now (static input) so the per-face
				// smallest-containing-ring search below is a compare, not a reshoelace.
				blockedRings.push_back(
					{&poly.ring, poly.provenanceId, poly.openingId, poly.holeCapable, signedAreaDoubledAbs(poly.ring)});
			} else {
				borderRings.push_back(&poly.ring);
			}
		}
		if (borderRings.empty()) {
			return result; // no walkable bounds -> empty mesh
		}

		// 1. Arrange every ring edge, tagging each segment with its source polygon's
		// provenanceId so extracted edges carry it back.
		std::vector<InputSegment> segments;
		for (const NavInputPolygon& poly : input.polygons) {
			const std::size_t n = poly.ring.size();
			if (n < 3) {
				continue;
			}
			for (std::size_t i = 0; i < n; ++i) {
				segments.push_back({poly.ring[i], poly.ring[(i + 1) % n], poly.provenanceId});
			}
		}

		const Arrangement	arrangement = buildArrangement(segments);
		const HalfEdgeMesh	mesh		= extractFaces(arrangement);
		result.vertices					= mesh.vertices;

		// 2. Collect bounded CCW faces that are IN-BOUNDS (rep point inside ANY
		// unblocked border ring); faces outside every border (exterior) are discarded.
		// In-bounds faces are KEPT whether or not they sit inside a blocked ring -- the
		// whole region is triangulated, wall interiors included, so a belief query can
		// optimistically path through an unseen wall. Classification (which blocked ring
		// tags the face) is DEFERRED to step 4b, after holes are attached: a face that
		// has holes spans more than one even-odd water-depth region, and its outer-cycle
		// representative point can fall inside a hole, so we must classify from a point
		// that lies in the face's actual interior (outer minus holes).
		std::vector<WalkableFace>	walkable;
		for (std::size_t fi = 0; fi < mesh.faces.size(); ++fi) {
			const Face& f = mesh.faces[fi];
			if (f.signedAreaDoubled.sign() <= 0 || !f.representativePoint.has_value()) {
				continue; // CW cycle or degenerate bounded face
			}
			const Vec2i64& rep = *f.representativePoint;
			bool insideBorder = false;
			for (const std::vector<Vec2i64>* ring : borderRings) {
				if (pointInPolygon(rep, *ring) == PointInPolygon::Inside) {
					insideBorder = true;
					break;
				}
			}
			if (!insideBorder) {
				continue; // outside every walkable bound
			}

			WalkableFace wf;
			wf.outer	   = faceRingIndices(mesh, f);
			wf.areaDoubled = f.signedAreaDoubled;
			wf.repPoint	   = rep; // outer-cycle rep; replaced by a hole-aware point in 4b
			walkable.push_back(std::move(wf));
		}

		// 4. Nesting. Every CW cycle (signedAreaDoubled < 0) is either a hole
		// boundary as seen from the walkable region around it, or the unbounded
		// outer cycle of a connected component. Attach each CW cycle as a hole of
		// the smallest-area walkable face that strictly contains ALL of its
		// vertices. A hole is strictly interior to its container, so the all-
		// vertices-Inside test is exact and unambiguous; the unbounded outer cycle
		// shares its ring with a walkable face (same vertices, on the boundary,
		// never strictly Inside), so it matches no container and is ignored.
		for (std::size_t fi = 0; fi < mesh.faces.size(); ++fi) {
			const Face& f = mesh.faces[fi];
			if (f.signedAreaDoubled.sign() >= 0) {
				continue; // bounded CCW face or zero-area; handled above
			}
			const std::vector<std::uint32_t> cwRing	   = faceRingIndices(mesh, f);
			const std::vector<Vec2i64>		 cwPoints  = ringPoints(mesh, cwRing);

			std::size_t	best	  = walkable.size();
			Int128		bestArea(0);
			for (std::size_t w = 0; w < walkable.size(); ++w) {
				const std::vector<Vec2i64> outerPts = ringPoints(mesh, walkable[w].outer);
				bool allInside = true;
				for (const Vec2i64& p : cwPoints) {
					if (pointInPolygon(p, outerPts) != PointInPolygon::Inside) {
						allInside = false;
						break;
					}
				}
				if (!allInside) {
					continue;
				}
				if (best == walkable.size() || walkable[w].areaDoubled < bestArea) {
					best	 = w;
					bestArea = walkable[w].areaDoubled;
				}
			}
			if (best != walkable.size()) {
				// The CW cycle is already CW (negative area), matching the hole
				// contract of triangulateWithHoles directly.
				walkable[best].holes.push_back(cwRing);
			}
		}

		// 4b. Classify each in-bounds face against the blocked rings, using a
		// representative point in the face's TRUE interior (inside its outer cycle and
		// OUTSIDE all of its holes). The outer-cycle rep point from face extraction can
		// land inside a hole -- e.g. a water body whose outer boundary surrounds the
		// whole area (a river exiting on every side) leaves the dry land as a CW hole
		// ring, and the surrounding water face's outer-cycle centroid falls in that
		// land hole. Classifying from the hole-unaware point then reads the hole's
		// even-odd depth (land) for the whole water face, mistagging the water as floor.
		// A hole-aware point gets the annular region's own depth.
		//
		// Splits SOLID rings (flora, walls: solid containment) from HOLE-CAPABLE rings
		// (water: even-odd containment parity):
		//   SOLID: the smallest-area containing ring wins (a door-span footprint is
		//   strictly smaller than its flank bands, so it wins over the enclosing wall).
		//   HOLE-CAPABLE: count how many contain the point (depth). A water body emits a
		//   CCW outer boundary plus CW land-island holes as SEPARATE blocked rings; a
		//   point inside an even number of them is land (outer+island = 2), inside an odd
		//   number is genuine water (open water = 1, a pond on the island = 3). Disjoint
		//   water bodies never nest, so only one body's rings ever contain a given point.
		//   A solid obstacle always blocks, even sitting over water.
		for (WalkableFace& wf : walkable) {
			const Vec2i64 rep = representativeOutsideHoles(mesh, wf.outer, wf.holes, wf.repPoint);

			Int128 bestSolidArea(0);
			bool   solidTagged = false;
			std::int64_t solidBlocker = kNoBlocker;
			std::int64_t solidOpening = kNoOpening;

			Int128 bestWaterArea(0);
			bool   waterTagged = false;
			int	   waterDepth  = 0;
			std::int64_t waterBlocker = kNoBlocker;
			std::int64_t waterOpening = kNoOpening;

			for (const BlockedRing& br : blockedRings) {
				if (pointInPolygon(rep, *br.ring) != PointInPolygon::Inside) {
					continue;
				}
				const Int128& area = br.areaDoubledAbs;
				if (br.holeCapable) {
					++waterDepth;
					if (!waterTagged || area < bestWaterArea) {
						bestWaterArea = area;
						waterBlocker  = br.provenance;
						waterOpening  = br.opening;
						waterTagged	  = true;
					}
				} else {
					if (!solidTagged || area < bestSolidArea) {
						bestSolidArea = area;
						solidBlocker  = br.provenance;
						solidOpening  = br.opening;
						solidTagged	  = true;
					}
				}
			}

			if (solidTagged) {
				wf.blocker = solidBlocker; // a real obstacle always blocks, even over water
				wf.opening = solidOpening;
			} else if (waterDepth % 2 == 1) {
				wf.blocker = waterBlocker; // odd depth: genuine water
				wf.opening = waterOpening;
			}
			// else even depth (incl. 0): floor -- leave kNoBlocker/kNoOpening.
		}

		// 5. Triangulate each face (floor AND wall interiors) with its holes, copying
		// the face's belief tags onto every triangle. A degenerate result for one face
		// is skipped (its triangles omitted) rather than aborting the whole mesh -- a
		// single bad region yields a partial mesh.
		for (const WalkableFace& wf : walkable) {
			std::vector<std::array<std::uint32_t, 3>> tris =
				triangulateWithHoles(mesh.vertices, wf.outer, wf.holes);
			for (const std::array<std::uint32_t, 3>& t : tris) {
				NavTriangle nt;
				nt.v			 = t;
				nt.neighbor		 = {-1, -1, -1};
				nt.edgeProvenance = {kNoProvenance, kNoProvenance, kNoProvenance};
				nt.edgeOpening	 = {kNoOpening, kNoOpening, kNoOpening};
				nt.faceBlocker	 = wf.blocker;
				nt.faceOpening	 = wf.opening;
				// triangulateWithHoles guarantees CCW; assert the invariant cheaply
				// by reorienting any stray triangle so downstream queries can rely on
				// it unconditionally.
				if (orientation(mesh.vertices[t[0]], mesh.vertices[t[1]], mesh.vertices[t[2]]) ==
					Orientation::Clockwise) {
					std::swap(nt.v[1], nt.v[2]);
				}
				result.triangles.push_back(nt);
			}
		}

		// 6. Adjacency. Hash every triangle's three edges; an undirected edge shared
		// by two triangles links them as neighbors. Because wall interiors are now
		// triangulated too, floor and wall faces share their band edges and so get
		// linked here -- that link is what lets a belief query traverse INTO an unseen
		// wall. Truth/belief gating then happens in the path query, not the topology:
		// the mesh is one connected graph, the filter decides which edges to cross.
		std::unordered_map<EdgeKey, std::pair<std::int32_t, int>> firstOwner;
		firstOwner.reserve(result.triangles.size() * 3);
		for (std::size_t ti = 0; ti < result.triangles.size(); ++ti) {
			NavTriangle& tri = result.triangles[ti];
			for (int e = 0; e < 3; ++e) {
				const EdgeKey key = makeEdgeKey(tri.v[e], tri.v[(e + 1) % 3]);
				auto		  it  = firstOwner.find(key);
				if (it == firstOwner.end()) {
					firstOwner.emplace(key, std::make_pair(static_cast<std::int32_t>(ti), e));
				} else {
					const std::int32_t otherTri  = it->second.first;
					const int		   otherEdge = it->second.second;
					tri.neighbor[e]						   = otherTri;
					result.triangles[otherTri].neighbor[otherEdge] = static_cast<std::int32_t>(ti);
				}
			}
		}

		// 7a. Edge provenance. Map each arrangement edge's canonical vertex pair to
		// its provenance (a single id per nav input ring; we take the first, since
		// nav rings do not overlap coincidentally the way wall centerlines can).
		std::unordered_map<EdgeKey, std::int64_t> edgeProvenance;
		edgeProvenance.reserve(arrangement.edges.size());
		for (const ArrangementEdge& ae : arrangement.edges) {
			if (ae.provenance.empty()) {
				continue;
			}
			const EdgeKey key =
				makeEdgeKey(static_cast<std::uint32_t>(ae.from), static_cast<std::uint32_t>(ae.to));
			edgeProvenance.emplace(key, ae.provenance.front());
		}
		for (NavTriangle& tri : result.triangles) {
			for (int e = 0; e < 3; ++e) {
				const EdgeKey key = makeEdgeKey(tri.v[e], tri.v[(e + 1) % 3]);
				auto		  it  = edgeProvenance.find(key);
				if (it != edgeProvenance.end()) {
					tri.edgeProvenance[e] = it->second;
				}
			}
		}

		// 7b. Door tagging. Each portal's (a, b) endpoints are exact mesh vertices;
		// map a position to its vertex index, then tag the triangle edge(s) whose
		// canonical vertex pair matches. Both triangles sharing an interior portal
		// edge get the tag (openingId and clearWidthMm: the width filter gates a door
		// crossing by the latter, since a door span is faceBlocker>0, not a common-
		// knowledge obstacle, so the Demyen widths below never see the door gap).
		if (!input.doors.empty()) {
			std::map<Vec2i64, std::uint32_t> vertexIndex;
			for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
				vertexIndex.emplace(mesh.vertices[i], static_cast<std::uint32_t>(i));
			}
			struct PortalTag {
				std::int64_t openingId	  = kNoOpening;
				std::int64_t clearWidthMm = kUnconstrainedWidth;
			};
			std::unordered_map<EdgeKey, PortalTag> portalEdges;
			portalEdges.reserve(input.doors.size());
			for (const DoorPortal& door : input.doors) {
				auto ia = vertexIndex.find(door.a);
				auto ib = vertexIndex.find(door.b);
				if (ia == vertexIndex.end() || ib == vertexIndex.end()) {
					continue; // portal endpoint not a mesh vertex; cannot tag
				}
				portalEdges.emplace(makeEdgeKey(ia->second, ib->second), PortalTag{door.openingId, door.clearWidthMm});
			}
			for (NavTriangle& tri : result.triangles) {
				for (int e = 0; e < 3; ++e) {
					const EdgeKey key = makeEdgeKey(tri.v[e], tri.v[(e + 1) % 3]);
					auto		  it  = portalEdges.find(key);
					if (it != portalEdges.end()) {
						tri.edgeOpening[e]	  = it->second.openingId;
						tri.edgeClearWidthMm[e] = it->second.clearWidthMm;
					}
				}
			}
		}

		// 8. Corridor widths. For every triangle and every apex vertex, compute the
		// Demyen-Buro width of the passage between the two edges meeting at that apex
		// (max disc diameter vs common-knowledge obstacles). This needs the finished
		// adjacency and face tags, so it runs last. Belief-dependent walls are excluded
		// (see calculateEdgePairWidth / NavMesh.h).
		WidthScratch widthScratch;
		widthScratch.visited.assign(result.triangles.size(), 0);
		widthScratch.stamp = 0; // first ++stamp makes 1; initial visited[] of 0 never matches
		for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(result.triangles.size()); ++ti) {
			for (int apex = 0; apex < 3; ++apex) {
				result.triangles[ti].edgePairWidthMm[apex] = calculateEdgePairWidth(result, widthScratch, ti, apex);
			}
		}

		// 9. Uniform-grid spatial index for locateTriangle.
		// Cell size heuristic: target roughly one triangle per cell by setting
		//   cellSize = max(1, floor(sqrt(area / max(1, triangleCount))))
		// where area = AABB area of the whole mesh. This keeps the grid from being
		// either too coarse (many candidates per cell) or so fine it wastes memory.
		// Clamped to [1, 1e9] so the cell count stays sane for any reasonable mesh.
		if (!result.triangles.empty()) {
			// Compute mesh AABB over all vertices.
			Vec2i64 mn = result.vertices[0];
			Vec2i64 mx = result.vertices[0];
			for (const Vec2i64& v : result.vertices) {
				if (v.x < mn.x) mn.x = v.x;
				if (v.y < mn.y) mn.y = v.y;
				if (v.x > mx.x) mx.x = v.x;
				if (v.y > mx.y) mx.y = v.y;
			}

			const std::int64_t spanX = mx.x - mn.x;
			const std::int64_t spanY = mx.y - mn.y;
			// Area as double; exact int would overflow for large mm coordinates.
			const double area = static_cast<double>(spanX) * static_cast<double>(spanY);
			const double n	  = static_cast<double>(result.triangles.size());
			// cellSize >= 1 (integer mm), <= 1e9 (avoids grid of 1 cell being enormous).
			const std::int64_t cellSize = static_cast<std::int64_t>(
				std::max(1.0, std::min(1.0e9, std::floor(std::sqrt(area / std::max(1.0, n))))));

			const std::int32_t cols = static_cast<std::int32_t>((spanX + cellSize - 1) / cellSize) + 1;
			const std::int32_t rows = static_cast<std::int32_t>((spanY + cellSize - 1) / cellSize) + 1;

			// For each cell collect overlapping triangle indices, then sort ascending
			// (so within a cell the linear-scan tie-break is reproduced: lowest index wins).
			const std::int64_t ncells = static_cast<std::int64_t>(cols) * rows;
			std::vector<std::vector<std::int32_t>> perCell(static_cast<std::size_t>(ncells));

			for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(result.triangles.size()); ++ti) {
				const NavTriangle& tri = result.triangles[ti];
				// Triangle AABB in vertex-space.
				std::int64_t txMin = result.vertices[tri.v[0]].x;
				std::int64_t txMax = txMin;
				std::int64_t tyMin = result.vertices[tri.v[0]].y;
				std::int64_t tyMax = tyMin;
				for (int k = 1; k < 3; ++k) {
					const Vec2i64& vk = result.vertices[tri.v[k]];
					if (vk.x < txMin) txMin = vk.x;
					if (vk.x > txMax) txMax = vk.x;
					if (vk.y < tyMin) tyMin = vk.y;
					if (vk.y > tyMax) tyMax = vk.y;
				}
				// Map to cell range (clamp to grid bounds).
				const std::int32_t c0 = static_cast<std::int32_t>((txMin - mn.x) / cellSize);
				const std::int32_t c1 = static_cast<std::int32_t>((txMax - mn.x) / cellSize);
				const std::int32_t r0 = static_cast<std::int32_t>((tyMin - mn.y) / cellSize);
				const std::int32_t r1 = static_cast<std::int32_t>((tyMax - mn.y) / cellSize);
				for (std::int32_t r = r0; r <= r1 && r < rows; ++r) {
					for (std::int32_t c = c0; c <= c1 && c < cols; ++c) {
						perCell[static_cast<std::size_t>(r * cols + c)].push_back(ti);
					}
				}
			}

			// Sort each cell's candidates ascending and build CSR.
			NavGrid& g = result.grid;
			g.minPt	   = mn;
			g.maxPt	   = mx;
			g.cellSize = cellSize;
			g.cols	   = cols;
			g.rows	   = rows;
			g.cellStart.resize(static_cast<std::size_t>(ncells) + 1);
			g.cellStart[0] = 0;
			for (std::int64_t ci = 0; ci < ncells; ++ci) {
				std::vector<std::int32_t>& cell = perCell[static_cast<std::size_t>(ci)];
				std::sort(cell.begin(), cell.end());
				g.cellStart[static_cast<std::size_t>(ci) + 1] =
					g.cellStart[static_cast<std::size_t>(ci)] + static_cast<std::int32_t>(cell.size());
				g.candidates.insert(g.candidates.end(), cell.begin(), cell.end());
			}
		}

		// 10. Width-aware reachability forests. Collect every interior portal edge once
		// (canonical owner = the lower triangle index, so each shared edge is listed a
		// single time) with its sound disc-diameter capacity, then build the truth and
		// terrain forests over that one capacity set. This needs finished adjacency,
		// widths, and door tags, so it runs last.
		{
			std::vector<PortalEdge> portals;
			portals.reserve(result.triangles.size() * 3 / 2);
			for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(result.triangles.size()); ++ti) {
				const NavTriangle& tri = result.triangles[ti];
				for (int e = 0; e < 3; ++e) {
					const std::int32_t nb = tri.neighbor[e];
					if (nb < 0 || nb <= ti) {
						continue; // boundary, or already listed from the lower-index side
					}
					PortalEdge pe;
					pe.triA = ti;
					pe.triB = nb;
					pe.cap	= portalCapacity(result, ti, e, nb);
					portals.push_back(pe);
				}
			}
			result.truthForest	 = buildForest(result, portals, truthTraversable);
			result.terrainForest = buildForest(result, portals, terrainTraversable);
		}

		return result;
	}

} // namespace geometry::nav
