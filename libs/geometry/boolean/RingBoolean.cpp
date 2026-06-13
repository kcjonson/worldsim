#include "RingBoolean.h"

#include "../arrangement/Arrangement.h"
#include "../arrangement/HalfEdge.h"
#include "../offset/WallOffset.h"
#include "../predicates/Predicates.h"

#include <cstddef>
#include <map>
#include <vector>

namespace geometry {

	namespace {

		// Base offset for ring B's edge provenance so A's and B's edges occupy
		// disjoint index ranges in the shared arrangement (A: i, B: kBBase + j).
		// Classification reads the representative point, not provenance, but keeping
		// the ranges disjoint is cheap and aids debugging.
		constexpr std::int64_t kBBase = 1'000'000'000LL;

		bool ringValid(const Ring& r) {
			return r.size() >= 3 && isSimple(r).pass;
		}

		void appendRingSegments(const Ring& ring, std::int64_t base, std::vector<InputSegment>& out) {
			const std::size_t n = ring.size();
			for (std::size_t i = 0; i < n; ++i) {
				out.push_back({ring[i], ring[(i + 1) % n], base + static_cast<std::int64_t>(i)});
			}
		}

		// One stitched result loop: its vertices in order, and whether the kept
		// region touches itself at one of its vertices (a pinch, non-simple result).
		// Winding is read on demand from signedAreaDoubled(ring).
		struct ResultLoop {
			Ring ring;
			bool selfTouch = false;
		};

		// Walk the half-edge boundary of the kept face set into oriented loops.
		// A boundary half-edge has a kept face on its own side and a non-kept face
		// across its twin. Such half-edges, chained by rotating around their target
		// vertex past interior (kept|kept) edges, form closed loops: CCW around a
		// kept region's outer boundary, CW around an enclosed void.
		std::vector<ResultLoop> walkBoundary(const HalfEdgeMesh& mesh, const std::vector<bool>& kept) {
			const std::size_t					halfCount = mesh.halfEdges.size();
			auto keptSide = [&](std::size_t h) {
				const std::size_t f = mesh.halfEdges[h].face;
				return f < kept.size() && kept[f];
			};
			auto isBoundary = [&](std::size_t h) {
				return keptSide(h) && !keptSide(mesh.halfEdges[h].twin);
			};

			std::vector<bool>		visited(halfCount, false);
			std::vector<ResultLoop> loops;

			for (std::size_t start = 0; start < halfCount; ++start) {
				if (visited[start] || !isBoundary(start)) {
					continue;
				}

				ResultLoop	loop;
				std::size_t h = start;
				do {
					visited[h] = true;
					loop.ring.push_back(mesh.vertices[mesh.halfEdges[h].origin]);

					// Successor boundary half-edge: from h's target, advance along the
					// face cycle, stepping over any edge interior to the kept region
					// (kept on both sides) by crossing its twin.
					std::size_t cur = mesh.halfEdges[h].next;
					while (keptSide(mesh.halfEdges[cur].twin)) {
						cur = mesh.halfEdges[mesh.halfEdges[cur].twin].next;
					}
					h = cur;
				} while (h != start);

				loops.push_back(std::move(loop));
			}

			// Pinch detection: the kept region touches itself at a vertex. This shows
			// up two ways depending on the angular sort at the shared vertex, either
			// as one loop visiting the point twice (a figure-eight) or as two loops
			// that share the point. Flag every loop that participates so callers see a
			// pinch regardless of which form the walk produced. Exact on integer
			// coordinates.
			std::map<Vec2i64, int> vertexCount;
			for (const ResultLoop& l : loops) {
				std::vector<Vec2i64> uniqueInLoop;
				for (const Vec2i64& v : l.ring) {
					bool seenHere = false;
					for (const Vec2i64& u : uniqueInLoop) {
						if (u == v) {
							seenHere = true;
							break;
						}
					}
					// Count a coordinate once per loop unless it repeats within the loop;
					// a within-loop repeat alone already proves a pinch.
					if (seenHere) {
						vertexCount[v] += 2;
					} else {
						uniqueInLoop.push_back(v);
						vertexCount[v] += 1;
					}
				}
			}
			for (ResultLoop& l : loops) {
				for (const Vec2i64& v : l.ring) {
					if (vertexCount[v] >= 2) {
						l.selfTouch = true;
						break;
					}
				}
			}

			return loops;
		}

		// The arrangement + face classification core. `keepFace(insideA, insideB)`
		// selects which bounded faces are in the result region; `connected` reports
		// whether the arrangement is a single connected component (one outer cycle).
		// The boundary walk is only sound when connected: with disconnected
		// components a strictly-interior ring forms its own outer cycle and a
		// spurious boundary loop, so callers must resolve the disconnected case by
		// containment instead of trusting these loops.
		template <typename KeepFn>
		std::vector<ResultLoop> classifyAndWalk(const Ring& a, const Ring& b, KeepFn keepFace, bool& connected) {
			std::vector<InputSegment> segs;
			segs.reserve(a.size() + b.size());
			appendRingSegments(a, 0, segs);
			appendRingSegments(b, kBBase, segs);

			const Arrangement arr	= buildArrangement(segs);
			const HalfEdgeMesh mesh = extractFaces(arr);

			std::size_t outerCount = 0;
			for (const Face& face : mesh.faces) {
				if (face.outer) {
					++outerCount;
				}
			}
			connected = outerCount <= 1;

			std::vector<bool> kept(mesh.faces.size(), false);
			for (std::size_t f = 0; f < mesh.faces.size(); ++f) {
				const Face& face = mesh.faces[f];
				if (face.outer || !face.representativePoint) {
					continue;
				}
				const Vec2i64 p		  = *face.representativePoint;
				const bool	  insideA = pointInPolygon(p, a) == PointInPolygon::Inside;
				const bool	  insideB = pointInPolygon(p, b) == PointInPolygon::Inside;
				kept[f]				  = keepFace(insideA, insideB);
			}

			return walkBoundary(mesh, kept);
		}

		// Containment via a single interior sample: when the rings share no boundary
		// point (disconnected arrangement), each ring is wholly inside or wholly
		// outside the other, so testing one vertex decides it. b's first vertex
		// cannot be OnBoundary here (no shared boundary), so Inside is conclusive.
		bool firstVertexInside(const Ring& inner, const Ring& outer) {
			return pointInPolygon(inner.front(), outer) == PointInPolygon::Inside;
		}

		// Finalize a single candidate loop into a result: simplify (1 mm), orient
		// CCW, re-validate simple. Used after the loop count check confirms there is
		// exactly one CCW ring.
		BooleanResult finalize(Ring ring) {
			simplifyRing(ring, kBooleanSimplifyEpsMm);
			if (ring.size() < 3 || signedAreaDoubled(ring).sign() == 0) {
				return {BooleanStatus::InvalidInput, {}};
			}
			ensureCounterClockwise(ring);
			if (!isSimple(ring).pass) {
				return {BooleanStatus::InvalidInput, {}};
			}
			return {BooleanStatus::Ok, std::move(ring)};
		}

		// Partition stitched loops into outer (CCW) and hole (CW) counts. Degenerate
		// zero-area loops are ignored (they cannot occur for real face boundaries
		// but the count stays honest if one slips through).
		void countLoops(const std::vector<ResultLoop>& loops, std::size_t& ccw, std::size_t& cw) {
			ccw = 0;
			cw	= 0;
			for (const ResultLoop& l : loops) {
				const int s = signedAreaDoubled(l.ring).sign();
				if (s > 0) {
					++ccw;
				} else if (s < 0) {
					++cw;
				}
			}
		}

		const ResultLoop* firstCcw(const std::vector<ResultLoop>& loops) {
			for (const ResultLoop& l : loops) {
				if (signedAreaDoubled(l.ring).sign() > 0) {
					return &l;
				}
			}
			return nullptr;
		}

		bool anySelfTouch(const std::vector<ResultLoop>& loops) {
			for (const ResultLoop& l : loops) {
				if (l.selfTouch) {
					return true;
				}
			}
			return false;
		}

	} // namespace

	bool ringsInteriorOverlap(const Ring& a, const Ring& b) {
		if (!ringValid(a) || !ringValid(b)) {
			return false;
		}

		// Witness-based and connectivity-independent: the interiors overlap iff some
		// bounded face of the combined arrangement has its (exact, strictly-interior)
		// representative point inside both rings. The point is a genuine witness in
		// both open interiors, so this is exact regardless of how the components
		// connect. Edge- or vertex-only contact produces no face interior to both, so
		// the legal snapped-adjacent case correctly returns false.
		std::vector<InputSegment> segs;
		segs.reserve(a.size() + b.size());
		appendRingSegments(a, 0, segs);
		appendRingSegments(b, kBBase, segs);

		const Arrangement	arr	 = buildArrangement(segs);
		const HalfEdgeMesh	mesh = extractFaces(arr);
		for (const Face& face : mesh.faces) {
			if (face.outer || !face.representativePoint) {
				continue;
			}
			const Vec2i64 p = *face.representativePoint;
			if (pointInPolygon(p, a) == PointInPolygon::Inside && pointInPolygon(p, b) == PointInPolygon::Inside) {
				return true;
			}
		}
		return false;
	}

	BooleanResult unionRings(const Ring& a, const Ring& b) {
		if (!ringValid(a) || !ringValid(b)) {
			return {BooleanStatus::InvalidInput, {}};
		}

		bool						  connected = true;
		const std::vector<ResultLoop> loops =
			classifyAndWalk(a, b, [](bool insideA, bool insideB) { return insideA || insideB; }, connected);

		// Disconnected arrangement: the rings share no boundary point, so they are
		// either separate or one is nested in the other. The face walk is unreliable
		// here (a nested ring forms a spurious loop), so decide by containment.
		if (!connected) {
			if (firstVertexInside(b, a)) {
				return finalize(a); // b lies inside a; the union is just a
			}
			if (firstVertexInside(a, b)) {
				return finalize(b);
			}
			return {BooleanStatus::Disjoint, {}};
		}

		// A pinch: the inputs touch only at a single vertex, so the kept region's
		// boundary visits that point twice (a figure-eight). Non-simple result.
		if (anySelfTouch(loops)) {
			return {BooleanStatus::PinchVertex, {}};
		}

		std::size_t ccw = 0;
		std::size_t cw	= 0;
		countLoops(loops, ccw, cw);

		// One CCW outer plus a CW loop: the union encloses a void (a concave shape
		// and a bar closing it). A foundation cannot have a hole.
		if (cw >= 1) {
			return {BooleanStatus::ResultHasHole, {}};
		}
		if (ccw != 1) {
			return {BooleanStatus::Disjoint, {}};
		}

		return finalize(firstCcw(loops)->ring);
	}

	BooleanResult subtractRings(const Ring& a, const Ring& b) {
		if (!ringValid(a) || !ringValid(b)) {
			return {BooleanStatus::InvalidInput, {}};
		}

		// No effect when the interiors don't overlap (b is fully outside a, or only
		// shares an edge/vertex): nothing is carved away. Checked first so the
		// disjoint case reports NoEffect rather than echoing a unchanged.
		if (!ringsInteriorOverlap(a, b)) {
			return {BooleanStatus::NoEffect, {}};
		}

		bool						  connected = true;
		const std::vector<ResultLoop> loops =
			classifyAndWalk(a, b, [](bool insideA, bool insideB) { return insideA && !insideB; }, connected);

		// Disconnected arrangement with overlapping interiors means strict nesting
		// (no shared boundary point): either b covers a, or b sits strictly inside a
		// and would carve an enclosed hole. The face walk is unreliable here.
		if (!connected) {
			if (firstVertexInside(a, b)) {
				return {BooleanStatus::ConsumesInput, {}}; // b contains a
			}
			return {BooleanStatus::ResultHasHole, {}}; // b strictly inside a
		}

		// A pinch in the remainder (b's cut touches a's boundary at a single point,
		// leaving a self-touching outline): non-simple result.
		if (anySelfTouch(loops)) {
			return {BooleanStatus::PinchVertex, {}};
		}

		std::size_t ccw = 0;
		std::size_t cw	= 0;
		countLoops(loops, ccw, cw);

		// No kept area: b covered all of a's interior.
		if (ccw == 0) {
			return {BooleanStatus::ConsumesInput, {}};
		}
		// A CW loop alongside the outer boundary: b sits strictly inside a and the
		// remainder is an annulus. A foundation cannot have a hole.
		if (cw >= 1) {
			return {BooleanStatus::ResultHasHole, {}};
		}
		// Two separate CCW pieces: b cut clean across a, splitting it.
		if (ccw >= 2) {
			return {BooleanStatus::ResultSplits, {}};
		}

		return finalize(firstCcw(loops)->ring);
	}

} // namespace geometry
