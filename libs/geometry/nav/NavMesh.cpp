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
			std::int64_t hi = 3037000499; // floor(sqrt(INT64_MAX)); k*k stays in int64 range as Int128
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

	} // namespace

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
				blockedRings.push_back({&poly.ring, poly.provenanceId, poly.openingId, signedAreaDoubledAbs(poly.ring)});
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

		// 2-3. Classify bounded CCW faces. A face is IN-BOUNDS iff its representative
		// point lies inside ANY unblocked border ring; faces outside every border
		// (exterior) are discarded. In-bounds faces are KEPT whether or not they sit
		// inside a blocked ring -- the whole region is triangulated, wall interiors
		// included, so a belief query can optimistically path through an unseen wall.
		// A face inside a blocked ring is tagged with that ring's belief data (the
		// SMALLEST-area containing ring, so a door-span sub-region wins over an
		// enclosing wall band); a floor face keeps kNoBlocker/kNoOpening.
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
			// Pick the smallest blocked ring containing the rep, by polygon area. The
			// door-span footprint is strictly smaller than (and abuts, never nests in)
			// the flank bands, so this is unambiguous; the rule also degrades sanely if
			// obstacles ever nest. Floor faces match nothing and stay kNoBlocker.
			Int128 bestArea(0);
			bool   tagged = false;
			for (const BlockedRing& br : blockedRings) {
				if (pointInPolygon(rep, *br.ring) != PointInPolygon::Inside) {
					continue;
				}
				const Int128& area = br.areaDoubledAbs;
				if (!tagged || area < bestArea) {
					bestArea	= area;
					wf.blocker	= br.provenance;
					wf.opening	= br.opening;
					tagged		= true;
				}
			}
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

		return result;
	}

} // namespace geometry::nav
