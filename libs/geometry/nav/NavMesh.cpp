#include "NavMesh.h"

#include "../arrangement/Arrangement.h"
#include "../core/Int128.h"
#include "../predicates/Predicates.h"
#include "../triangulation/ConstrainedDelaunay.h"
#include "PathQuery.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <map>
#include <span>
#include <unordered_map>
#include <tuple>
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

		// A face of the arrangement: the triangles one flood fill reaches without
		// crossing a constraint. Its largest triangle supplies the classification
		// sample, well inside the face even when snap rounding moved an arrangement
		// edge a fraction of a millimetre off its input ring.
		struct MeshFace {
			std::int32_t sampleTri	 = -1;
			Int128		 sampleArea2 = Int128(0);
			bool		 touchesFrame = false; // unbounded: outside every ring
			bool		 inBounds	  = false;
			std::int64_t blocker	  = kNoBlocker;
			std::int64_t opening	  = kNoOpening;
		};

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
		//      blockedness (the boundary of a water/obstacle region). Belief-dependent walls
		//      (faceBlocker > 0) are NOT obstacles for this pass (see NavMesh.h).
		// All obstacles in our model are filled polygons, so every obstacle vertex has
		// incident obstacle edges (no bare point obstacles); the paper's edge-based
		// search therefore captures every obstacle the disc must round.

		bool isCommonKnowledgeBlocked(std::int64_t faceBlocker) {
			return faceBlocker < 0 && faceBlocker != kNoBlocker;
		}

		// Edge e of triangle ti is an obstacle edge iff exactly one of the two faces it
		// separates is common-knowledge-blocked (the boundary of a water/obstacle region).
		// A mesh-boundary edge (neighbor == -1) has no obstacle face outside it, so a
		// floor border is not an obstacle edge (scope: terrain/obstacles only).
		bool isObstacleEdge(const NavView& view, std::int32_t ti, int e) {
			const bool		   self	 = isCommonKnowledgeBlocked(view.triangle(ti).faceBlocker);
			const std::int32_t nb	 = view.triangle(ti).neighbor[static_cast<std::size_t>(e)];
			const bool		   other = nb < 0 ? false : isCommonKnowledgeBlocked(view.triangle(nb).faceBlocker);
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

		// floor(|d|) < bound, decided on squares: no root is taken.
		// A fresh mark for one search's triangles.
		void beginWidthSearch(const NavView& view, WidthSearch& search) {
			search.visited.growTo(static_cast<std::size_t>(view.triangleSlots()));
			// A wrapped stamp would match marks left by searches long ago.
			if (++search.stamp == 0) {
				search.visited.zero();
				search.stamp = 1;
			}
		}

		bool lengthBelow(const Vec2i64& d, std::int64_t bound) {
			return dot(d, d) < Int128::product(bound, bound);
		}

		// floor(distance from c to the line through u and v) < bound, on squares:
		// cross^2 < bound^2 * |uv|^2.
		bool lineDistanceBelow(const Vec2i64& c, const Vec2i64& u, const Vec2i64& v, std::int64_t bound) {
			const Vec2i64 uv = v - u;
			return Int128::compareSquareToProduct(cross(uv, c - u), Int128::product(bound, bound), dot(uv, uv)) < 0;
		}

		// Demyen Algorithm 2: SearchWidth. Walk across non-obstacle edges from apex C,
		// bounded above by the closest obstacle found so far (`bound`, a floored mm
		// distance), returning the tightened bound. `ti`/`e` is the edge being entered.
		// The visited stamp only guards against a non-Delaunay sliver: Demyen proves (Thm
		// 4.3.4/4.3.6) a CDT width search never revisits a triangle, and an already-visited
		// triangle was fully explored from this apex, so no obstacle is dropped.
		std::int64_t searchWidth(const NavView& view, WidthSearch& scratch, const Vec2i64& c, std::int32_t ti, int e, std::int64_t bound) {
			const NavTriangle& tri = view.triangle(ti);
			const Vec2i64&	   u   = view.vertex(tri.v[static_cast<std::size_t>(e)]);
			const Vec2i64&	   v   = view.vertex(tri.v[static_cast<std::size_t>((e + 1) % 3)]);

			// Prune: if the foot of the perpendicular from C to line UV is outside the
			// segment, no obstacle reachable across this edge is in the wedge (Demyen
			// Fig. 4.13-4.14); the bound already reflects the nearer base vertex.
			if (obtuseAt(c, u, v) || obtuseAt(c, v, u)) {
				return bound;
			}
			// The foot is on the segment here, so its distance is the line distance.
			if (!lineDistanceBelow(c, u, v, bound)) {
				return bound; // nothing closer this way
			}
			if (isObstacleEdge(view, ti, e)) {
				return floorDistancePointToSegment(c, u, v); // closest obstacle is this constrained edge
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
			const NavTriangle& opp = view.triangle(nb);
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
			out = searchWidth(view, scratch, c, nb, (back + 1) % 3, out);
			out = searchWidth(view, scratch, c, nb, (back + 2) % 3, out);
			return out;
		}
	} // namespace

	// Demyen Algorithm 3, CalculateWidth, for the edge pair meeting at apex v[apex]: edges
	// apex (C, A) and (apex+2)%3 (B, C), opposite edge (apex+1)%3 (A, B). A base vertex
	// bounds the width only when one of this triangle's edges meeting it is an obstacle
	// edge, and an edge's obstacle status is read only when the edge is nearer than the
	// bound, which keeps every read within maxClearanceMm of the apex.
	std::int64_t corridorWidthMm(const NavView& viewIn, WidthSearch& search, std::int32_t ti, int apex, std::int64_t maxClearanceMm) {
		const NavView	   view	  = viewIn.resolved();
		const NavTriangle& tri	  = view.triangle(ti);
		const int		   cEdge  = (apex + 1) % 3;
		const int		   bEdge  = (apex + 2) % 3;
		const Vec2i64&	   c	  = view.vertex(tri.v[static_cast<std::size_t>(apex)]);
		const Vec2i64&	   a	  = view.vertex(tri.v[static_cast<std::size_t>(cEdge)]);
		const Vec2i64&	   b	  = view.vertex(tri.v[static_cast<std::size_t>(bEdge)]);
		auto			   capped = [&](std::int64_t d) { return d >= maxClearanceMm ? kUnconstrainedWidth : d; };

		std::int64_t d				= maxClearanceMm;
		auto		 obstacleVertex = [&](int edgeIn) { return isObstacleEdge(view, ti, cEdge) || isObstacleEdge(view, ti, edgeIn); };
		if (lengthBelow(a - c, d) && obstacleVertex(apex)) {
			d = floorSqrtSquared(dot(a - c, a - c));
		}
		if (lengthBelow(b - c, d) && obstacleVertex(bEdge)) {
			d = floorSqrtSquared(dot(b - c, b - c));
		}
		// An obtuse base angle: the nearer base vertex is the closest obstacle in the
		// wedge, and crossing the opposite edge would leave it.
		if (obtuseAt(c, a, b) || obtuseAt(c, b, a)) {
			return capped(d);
		}
		if (lineDistanceBelow(c, a, b, d) && isObstacleEdge(view, ti, cEdge)) {
			return capped(floorDistancePointToSegment(c, a, b));
		}
		beginWidthSearch(view, search);
		search.visited[static_cast<std::size_t>(ti)] = search.stamp;
		return capped(searchWidth(view, search, c, ti, cEdge, d));
	}

	std::uint64_t nextMeshGeneration() {
		static std::atomic<std::uint64_t> next{1};
		return next.fetch_add(1, std::memory_order_relaxed);
	}

	NavView navView(const NavMesh& mesh) {
		NavView view;
		view.vertices.of(mesh.vertices);
		view.triangles.of(mesh.triangles);
		view.owner			 = &mesh;
		view.locateIn		 = [](const void* owner, const Vec2i64& p) { return locateTriangle(*static_cast<const NavMesh*>(owner), p); };
		view.componentsIn	 = [](const void*) -> const MeshComponents* { return nullptr; };
		view.widthsCurrentIn = [](const void*) { return true; };
		view.epochIn		 = [](const void* owner) { return static_cast<const NavMesh*>(owner)->generation; };
		view.resolveIn		 = [](const void* owner, NavView& v) {
			const NavMesh& m = *static_cast<const NavMesh*>(owner);
			v.vertices.of(m.vertices);
			v.triangles.of(m.triangles);
		};
		return view;
	}

	SampleRing sampleRing(const NavInputPolygon& poly) {
		SampleRing r;
		r.ring3.reserve(poly.ring.size());
		for (const Vec2i64& p : poly.ring) {
			r.ring3.push_back(p * 3);
		}
		r.lo3 = r.ring3.front();
		r.hi3 = r.ring3.front();
		for (const Vec2i64& p : r.ring3) {
			r.lo3 = {std::min(r.lo3.x, p.x), std::min(r.lo3.y, p.y)};
			r.hi3 = {std::max(r.hi3.x, p.x), std::max(r.hi3.y, p.y)};
		}
		r.blocked		 = poly.blocked;
		r.provenance	 = poly.provenanceId;
		r.opening		 = poly.openingId;
		r.holeCapable	 = poly.holeCapable;
		r.areaDoubledAbs = signedAreaDoubledAbs(poly.ring);
		return r;
	}

	bool ringContainsSample(const SampleRing& r, const Vec2i64& p3) {
		return p3.x >= r.lo3.x && p3.x <= r.hi3.x && p3.y >= r.lo3.y && p3.y <= r.hi3.y &&
			   pointInPolygon(p3, r.ring3) == PointInPolygon::Inside;
	}

	bool boundsContainSample(const SampleRing& r, const Vec2i64& p3) {
		if (p3.x < r.lo3.x || p3.x >= r.hi3.x || p3.y < r.lo3.y || p3.y >= r.hi3.y) {
			return false;
		}
		// Crossing number of the ray from p + (e, e^2) along +x. An edge crosses the line
		// y = p.y + e^2 when one end is above p.y and the other is not, and the crossing
		// is right of p.x + e when it is strictly right of p.x.
		bool			  inside = false;
		const std::size_t n		 = r.ring3.size();
		for (std::size_t i = 0; i < n; ++i) {
			const Vec2i64& a = r.ring3[i];
			const Vec2i64& b = r.ring3[(i + 1) % n];
			if ((a.y > p3.y) == (b.y > p3.y)) {
				continue;
			}
			const int side = cross(b - a, p3 - a).sign();
			if ((b.y > a.y ? side > 0 : side < 0)) {
				inside = !inside;
			}
		}
		return inside;
	}

	FaceTags classifyCover(const std::vector<const SampleRing*>& containing) {
		// Smallest area first; equal areas by content.
		auto ranksBefore = [](const SampleRing& a, const SampleRing& b) {
			if (a.areaDoubledAbs != b.areaDoubledAbs) {
				return a.areaDoubledAbs < b.areaDoubledAbs;
			}
			return std::tie(a.provenance, a.opening, a.ring3) < std::tie(b.provenance, b.opening, b.ring3);
		};
		const SampleRing* solid		 = nullptr;
		const SampleRing* water		 = nullptr;
		int				  waterDepth = 0;
		for (const SampleRing* br : containing) {
			if (br->holeCapable) {
				++waterDepth;
				if (water == nullptr || ranksBefore(*br, *water)) {
					water = br;
				}
			} else if (solid == nullptr || ranksBefore(*br, *solid)) {
				solid = br;
			}
		}
		const bool		   solidTagged	= solid != nullptr;
		const std::int64_t solidBlocker = solidTagged ? solid->provenance : kNoBlocker;
		const std::int64_t solidOpening = solidTagged ? solid->opening : kNoOpening;
		const std::int64_t waterBlocker = water != nullptr ? water->provenance : kNoBlocker;
		const std::int64_t waterOpening = water != nullptr ? water->opening : kNoOpening;

		if (solidTagged) {
			return {solidBlocker, solidOpening};
		}
		if (waterDepth % 2 == 1) {
			return {waterBlocker, waterOpening};
		}
		return {};
	}

	BoundsSeams findBoundsSeams(const std::vector<const NavInputPolygon*>& rings) {
		struct Side {
			Vec2i64		lo;
			Vec2i64		hi;
			RingEdgeRef ref;

			bool operator<(const Side& other) const { return std::tie(lo, hi, ref) < std::tie(other.lo, other.hi, other.ref); }
		};
		BoundsSeams		  seams;
		std::vector<Side> sides;
		seams.partner.resize(rings.size());
		for (std::size_t r = 0; r < rings.size(); ++r) {
			const std::vector<Vec2i64>& ring = rings[r]->ring;
			if (rings[r]->blocked || ring.size() < 3) {
				continue;
			}
			seams.partner[r].assign(ring.size(), RingEdgeRef{});
			for (std::size_t i = 0; i < ring.size(); ++i) {
				const Vec2i64& a = ring[i];
				const Vec2i64& b = ring[(i + 1) % ring.size()];
				if (a != b) {
					sides.push_back({std::min(a, b), std::max(a, b), {static_cast<std::int32_t>(r), static_cast<std::int32_t>(i)}});
				}
			}
		}
		std::sort(sides.begin(), sides.end());
		std::map<Vec2i64, std::int32_t> pins;
		for (std::size_t begin = 0; begin < sides.size();) {
			std::size_t end = begin + 1;
			while (end < sides.size() && sides[end].lo == sides[begin].lo && sides[end].hi == sides[begin].hi) {
				++end;
			}
			for (std::size_t i = begin; i < end; ++i) {
				for (std::size_t j = begin; j < end; ++j) {
					if (sides[j].ref.ring != sides[i].ref.ring) {
						seams.partner[static_cast<std::size_t>(sides[i].ref.ring)][static_cast<std::size_t>(sides[i].ref.edge)] = sides[j].ref;
						if (sides[i].ref < sides[j].ref) {
							++pins[sides[i].lo];
							++pins[sides[i].hi];
						}
						break;
					}
				}
			}
			begin = end;
		}
		seams.pins.assign(pins.begin(), pins.end());
		return seams;
	}

	NavMesh buildNavMesh(const NavMeshInput& input) {
		NavMesh result;

		// Gather the walkable-bounds (unblocked) rings and the blocked rings. One
		// border is the common case, but multiple disjoint walkable regions are
		// supported: a face is in-bounds iff it lies inside ANY unblocked bound. Each
		// blocked ring carries its belief tags (provenanceId/openingId) so a face
		// landing inside it inherits them.
		std::vector<SampleRing> borderRings;
		std::vector<SampleRing> blockedRings;
		for (const NavInputPolygon& poly : input.polygons) {
			if (poly.ring.size() < 3) {
				continue;
			}
			(poly.blocked ? blockedRings : borderRings).push_back(sampleRing(poly));
		}
		if (borderRings.empty()) {
			return result; // no walkable bounds -> empty mesh
		}

		// 1. Arrange every ring edge, tagging each segment with its source polygon's
		// provenanceId so extracted edges carry it back. The arrangement splits edges
		// at every crossing and touch and merges overlaps, so its edges form a planar
		// straight-line graph.
		std::vector<const NavInputPolygon*> rings;
		for (const NavInputPolygon& poly : input.polygons) {
			rings.push_back(&poly);
		}
		std::vector<InputSegment> segments;
		std::vector<Vec2i64>	  pins;
		ringsArrangementInput(
			rings, findBoundsSeams(rings), [&](std::int32_t ring, std::int32_t) { return rings[static_cast<std::size_t>(ring)]->provenanceId; },
			segments, pins);
		const Arrangement arrangement = buildArrangement(segments, pins);
		result.vertices				  = arrangement.vertices;

		// 2. Triangulate the whole arrangement at once: a constrained Delaunay
		// triangulation of its vertices with every arrangement edge as a constraint.
		// Faces are never triangulated one at a time, so a weakly simple face (a pinch
		// vertex, holes touching each other or the border at one point) or a face with
		// hundreds of holes needs nothing special, and no face can fail and drop out.
		std::vector<std::array<std::uint32_t, 2>> constraints;
		constraints.reserve(arrangement.edges.size());
		for (const ArrangementEdge& ae : arrangement.edges) {
			constraints.push_back({static_cast<std::uint32_t>(ae.from), static_cast<std::uint32_t>(ae.to)});
		}
		const ConstrainedDelaunay cdt = buildConstrainedDelaunay(arrangement.vertices, constraints);

		// 3. Faces. Flood fill across unconstrained edges: each fill is one face of the
		// arrangement. The fill that reaches the frame is the unbounded face, outside
		// every ring.
		const std::int32_t		  cdtCount = static_cast<std::int32_t>(cdt.triangles.size());
		std::vector<std::int32_t> faceOf(static_cast<std::size_t>(cdtCount), -1);
		std::vector<MeshFace>	  faces;
		std::vector<std::int32_t> fill;
		for (std::int32_t seed = 0; seed < cdtCount; ++seed) {
			if (faceOf[static_cast<std::size_t>(seed)] >= 0) {
				continue;
			}
			const std::int32_t f = static_cast<std::int32_t>(faces.size());
			MeshFace		   face;
			faceOf[static_cast<std::size_t>(seed)] = f;
			fill.push_back(seed);
			while (!fill.empty()) {
				const std::int32_t t = fill.back();
				fill.pop_back();
				const CdtTriangle& tri	 = cdt.triangles[static_cast<std::size_t>(t)];
				const Int128	   area2 = cross(cdt.vertices[tri.v[1]] - cdt.vertices[tri.v[0]],
												 cdt.vertices[tri.v[2]] - cdt.vertices[tri.v[0]]);
				if (face.sampleTri < 0 || face.sampleArea2 < area2 || (area2 == face.sampleArea2 && t < face.sampleTri)) {
					face.sampleTri	 = t;
					face.sampleArea2 = area2;
				}
				for (int e = 0; e < 3; ++e) {
					face.touchesFrame = face.touchesFrame || tri.v[e] >= cdt.frameStart;
					const std::int32_t nb = tri.neighbor[e];
					if (nb >= 0 && tri.constraint[e] < 0 && faceOf[static_cast<std::size_t>(nb)] < 0) {
						faceOf[static_cast<std::size_t>(nb)] = f;
						fill.push_back(nb);
					}
				}
			}
			faces.push_back(face);
		}

		// 4. Classify each face from one exact interior sample: its sample triangle's
		// centroid in 3x space. The sample is strictly inside the triangle, so strictly
		// inside the face and off every arrangement edge; containment reads Inside or
		// Outside, never boundary. Faces outside every walkable bound are discarded.
		// In-bounds faces are KEPT whether or not they sit inside a blocked ring: the
		// whole region is triangulated, wall interiors included, so a belief query can
		// optimistically path through an unseen wall. classifyCover holds the rule.
		std::vector<const SampleRing*> containing;
		for (MeshFace& face : faces) {
			if (face.touchesFrame) {
				continue;
			}
			const CdtTriangle& s	   = cdt.triangles[static_cast<std::size_t>(face.sampleTri)];
			const Vec2i64	   sample3 = cdt.vertices[s.v[0]] + cdt.vertices[s.v[1]] + cdt.vertices[s.v[2]];
			for (const SampleRing& border : borderRings) {
				if (boundsContainSample(border, sample3)) {
					face.inBounds = true;
					break;
				}
			}
			if (!face.inBounds) {
				continue;
			}
			containing.clear();
			for (const SampleRing& br : blockedRings) {
				if (ringContainsSample(br, sample3)) {
					containing.push_back(&br);
				}
			}
			const FaceTags tags = classifyCover(containing);
			face.blocker		= tags.blocker;
			face.opening		= tags.opening;
		}

		// 5. Keep the in-bounds faces' triangles (floor AND wall interiors), copying each
		// face's belief tags onto its triangles. The CDT emits CCW triangles, and none
		// kept touches a frame corner, so every vertex index is an arrangement vertex.
		std::vector<std::int32_t> keptIndex(static_cast<std::size_t>(cdtCount), -1);
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			const MeshFace& face = faces[static_cast<std::size_t>(faceOf[static_cast<std::size_t>(t)])];
			if (!face.inBounds) {
				continue;
			}
			keptIndex[static_cast<std::size_t>(t)] = static_cast<std::int32_t>(result.triangles.size());
			NavTriangle nt;
			nt.v			  = cdt.triangles[static_cast<std::size_t>(t)].v;
			nt.neighbor		  = {-1, -1, -1};
			nt.edgeProvenance = {kNoProvenance, kNoProvenance, kNoProvenance};
			nt.edgeOpening	  = {kNoOpening, kNoOpening, kNoOpening};
			nt.faceBlocker	  = face.blocker;
			nt.faceOpening	  = face.opening;
			result.triangles.push_back(nt);
		}

		// 6. Adjacency and edge provenance, straight from the CDT. A neighbor in a
		// discarded face becomes a mesh boundary. Floor and wall faces share their band
		// edges and so stay linked -- that link is what lets a belief query traverse INTO
		// an unseen wall. Truth/belief gating then happens in the path query, not the
		// topology: the mesh is one connected graph, the filter decides which edges to
		// cross. A constrained edge carries its arrangement edge's provenance (a single
		// id per nav input ring; we take the first, since nav rings do not overlap
		// coincidentally the way wall centerlines can).
		for (std::int32_t t = 0; t < cdtCount; ++t) {
			const std::int32_t kept = keptIndex[static_cast<std::size_t>(t)];
			if (kept < 0) {
				continue;
			}
			const CdtTriangle& tri = cdt.triangles[static_cast<std::size_t>(t)];
			NavTriangle&	   nt  = result.triangles[static_cast<std::size_t>(kept)];
			for (int e = 0; e < 3; ++e) {
				if (tri.neighbor[e] >= 0) {
					nt.neighbor[e] = keptIndex[static_cast<std::size_t>(tri.neighbor[e])];
				}
				if (tri.constraint[e] >= 0) {
					const std::vector<std::int64_t>& prov = arrangement.edges[static_cast<std::size_t>(tri.constraint[e])].provenance;
					if (!prov.empty()) {
						nt.edgeProvenance[e] = prov.front();
					}
				}
			}
		}

		// 7. Door tagging. Each portal's (a, b) endpoints are exact mesh vertices;
		// map a position to its vertex index, then tag the triangle edge(s) whose
		// canonical vertex pair matches. Both triangles sharing an interior portal
		// edge get the tag (openingId and clearWidthMm: the width filter gates a door
		// crossing by the latter, since a door span is faceBlocker>0, not a common-
		// knowledge obstacle, so the Demyen widths below never see the door gap).
		if (!input.doors.empty()) {
			std::map<Vec2i64, std::uint32_t> vertexIndex;
			for (std::size_t i = 0; i < result.vertices.size(); ++i) {
				vertexIndex.emplace(result.vertices[i], static_cast<std::uint32_t>(i));
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

		// 8. Corridor widths for every triangle and apex, uncapped. They need the
		// finished adjacency and face tags.
		{
			const NavView view = navView(result);
			WidthSearch	  search;
			for (std::int32_t ti = 0; ti < static_cast<std::int32_t>(result.triangles.size()); ++ti) {
				NavTriangle& tri = result.triangles[static_cast<std::size_t>(ti)];
				for (int apex = 0; apex < 3; ++apex) {
					tri.edgePairWidthMm[static_cast<std::size_t>(apex)] = corridorWidthMm(view, search, ti, apex, kUnconstrainedWidth);
				}
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
