#include "NavMesh.h"

#include "../arrangement/Arrangement.h"
#include "../arrangement/HalfEdge.h"
#include "../core/Int128.h"
#include "../predicates/Predicates.h"
#include "../triangulation/Triangulation.h"

#include <algorithm>
#include <array>
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
		// edge get the tag.
		if (!input.doors.empty()) {
			std::map<Vec2i64, std::uint32_t> vertexIndex;
			for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
				vertexIndex.emplace(mesh.vertices[i], static_cast<std::uint32_t>(i));
			}
			std::unordered_map<EdgeKey, std::int64_t> portalEdges;
			portalEdges.reserve(input.doors.size());
			for (const DoorPortal& door : input.doors) {
				auto ia = vertexIndex.find(door.a);
				auto ib = vertexIndex.find(door.b);
				if (ia == vertexIndex.end() || ib == vertexIndex.end()) {
					continue; // portal endpoint not a mesh vertex; cannot tag
				}
				portalEdges.emplace(makeEdgeKey(ia->second, ib->second), door.openingId);
			}
			for (NavTriangle& tri : result.triangles) {
				for (int e = 0; e < 3; ++e) {
					const EdgeKey key = makeEdgeKey(tri.v[e], tri.v[(e + 1) % 3]);
					auto		  it  = portalEdges.find(key);
					if (it != portalEdges.end()) {
						tri.edgeOpening[e] = it->second;
					}
				}
			}
		}

		return result;
	}

} // namespace geometry::nav
