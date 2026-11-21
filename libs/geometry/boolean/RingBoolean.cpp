#include "RingBoolean.h"

#include "../arrangement/Arrangement.h"
#include "../arrangement/HalfEdge.h"
#include "../offset/WallOffset.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cstddef>
#include <map>
#include <utility>
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
		// `keptSide(h)` says whether the region on half-edge h's left, its face, is kept. With `flagPinches`
		// each loop's selfTouch is set (below); otherwise it stays false.
		template <typename KeptSideFn>
		std::vector<ResultLoop> walkBoundary(const HalfEdgeMesh& mesh, KeptSideFn keptSide, bool flagPinches) {
			const std::size_t halfCount = mesh.halfEdges.size();
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
			if (!flagPinches) {
				return loops;
			}

			// Pinch detection: the kept region touches itself at a vertex. This shows
			// up two ways depending on the angular sort at the shared vertex, either
			// as one loop visiting the point twice (a figure-eight) or as two loops
			// that share the point. Either way the point occurs twice among all the
			// loops' vertices; flag every loop that holds such a point. Exact on
			// integer coordinates.
			std::vector<std::pair<Vec2i64, std::size_t>> occurrences;
			for (std::size_t loop = 0; loop < loops.size(); ++loop) {
				for (const Vec2i64& vertex : loops[loop].ring) {
					occurrences.emplace_back(vertex, loop);
				}
			}
			std::sort(occurrences.begin(), occurrences.end());
			for (std::size_t first = 0; first < occurrences.size();) {
				std::size_t last = first + 1;
				while (last < occurrences.size() && occurrences[last].first == occurrences[first].first) {
					++last;
				}
				if (last - first >= 2) {
					for (std::size_t i = first; i < last; ++i) {
						loops[occurrences[i].second].selfTouch = true;
					}
				}
				first = last;
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

			return walkBoundary(mesh, [&](std::size_t h) {
				const std::size_t f = mesh.halfEdges[h].face;
				return f < kept.size() && kept[f];
			}, true);
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

		// An arrangement edge of one fill group, `weight` the group's net count of rings running along it
		// from `from` to `to` (against counts -1). Coordinates doubled, so an edge's midpoint is a lattice point.
		struct WeightedEdge {
			Vec2i64 from;
			Vec2i64 to;
			int		weight = 0;
		};

		// The winding number of weighted edges round `point` (doubled), by a ray toward +x (+y when
		// `upward`), the edge rule half-open. An edge through the point is not counted, so for a point on
		// an edge the answer is the winding just off it toward +x (+y): the side the ray leaves from.
		int windingAt(const std::vector<WeightedEdge>& edges, Vec2i64 point, bool upward) {
			// A quarter turn taking +y to +x keeps every winding and makes the upward ray a rightward one.
			const auto	  turned = [upward](Vec2i64 vertex) { return upward ? Vec2i64{vertex.y, -vertex.x} : vertex; };
			const Vec2i64 probe = turned(point);
			int			  winding = 0;
			for (const WeightedEdge& edge : edges) {
				const Vec2i64 start = turned(edge.from);
				const Vec2i64 end = turned(edge.to);
				if (start.y <= probe.y) {
					if (end.y > probe.y && cross(end - start, probe - start).sign() > 0) {
						winding += edge.weight;
					}
				} else if (end.y <= probe.y && cross(end - start, probe - start).sign() < 0) {
					winding -= edge.weight;
				}
			}
			return winding;
		}

		// Cuts a loop where it visits a vertex twice, so each piece visits every vertex once.
		void splitAtRepeatedVertices(const Ring& loop, std::vector<Ring>& out) {
			Ring						   open;
			std::map<Vec2i64, std::size_t> openIndex;
			for (const Vec2i64& vertex : loop) {
				const auto repeat = openIndex.find(vertex);
				if (repeat != openIndex.end()) {
					const std::size_t from = repeat->second;
					for (std::size_t i = from; i < open.size(); ++i) {
						openIndex.erase(open[i]);
					}
					out.emplace_back(open.begin() + static_cast<std::ptrdiff_t>(from), open.end());
					open.resize(from);
				}
				openIndex.emplace(vertex, open.size());
				open.push_back(vertex);
			}
			out.push_back(std::move(open));
		}

	} // namespace

	std::vector<RegionPolygon> unionOfFills(const std::vector<std::vector<Ring>>& groups) {
		struct InputEdge {
			std::size_t group = 0;
			Vec2i64		from;
			Vec2i64		to;
		};
		std::vector<InputEdge>	  inputs;
		std::vector<InputSegment> segments;
		for (std::size_t group = 0; group < groups.size(); ++group) {
			for (const Ring& ring : groups[group]) {
				for (std::size_t i = 0; i < ring.size(); ++i) {
					const Vec2i64& from = ring[i];
					const Vec2i64& to = ring[(i + 1) % ring.size()];
					if (from == to) {
						continue;
					}
					segments.push_back({from, to, static_cast<std::int64_t>(inputs.size())});
					inputs.push_back({group, from, to});
				}
			}
		}
		if (segments.empty()) {
			return {};
		}

		const Arrangement  arrangement = buildArrangement(segments);
		const HalfEdgeMesh mesh = extractFaces(arrangement, FaceDetail::CyclesOnly);
		const std::size_t  groupCount = groups.size();
		const std::size_t  edgeCount = arrangement.edges.size();

		// Each group's edges with their net weights, and the doubled box outside which its winding is 0.
		std::vector<int>						weights(edgeCount * groupCount, 0);
		std::vector<std::vector<WeightedEdge>>	groupEdges(groupCount);
		std::vector<std::pair<Vec2i64, Vec2i64>> groupBoxes(groupCount);
		std::vector<bool>						groupBoxSet(groupCount, false);
		for (std::size_t e = 0; e < edgeCount; ++e) {
			const ArrangementEdge& edge = arrangement.edges[e];
			const Vec2i64		   from = arrangement.vertices[edge.from];
			const Vec2i64		   to = arrangement.vertices[edge.to];
			for (const std::int64_t source : edge.provenance) {
				const InputEdge& input = inputs[static_cast<std::size_t>(source)];
				weights[e * groupCount + input.group] += dot(input.to - input.from, to - from).sign() > 0 ? 1 : -1;
			}
			for (std::size_t group = 0; group < groupCount; ++group) {
				const int weight = weights[e * groupCount + group];
				if (weight == 0) {
					continue;
				}
				groupEdges[group].push_back({from * 2, to * 2, weight});
				auto& [low, high] = groupBoxes[group];
				for (const Vec2i64 point : {from * 2, to * 2}) {
					if (!groupBoxSet[group]) {
						low = high = point;
						groupBoxSet[group] = true;
					}
					low = {std::min(low.x, point.x), std::min(low.y, point.y)};
					high = {std::max(high.x, point.x), std::max(high.y, point.y)};
				}
			}
		}

		// Each face's winding per group. Crossing a half-edge from its own face to its twin's drops the winding
		// by the edge's weight along the half-edge, so one ray cast per connected piece of the arrangement (at
		// the middle of an edge of the first face reached, the computed side being the one the ray leaves from)
		// and a flood across the twins give every face. Linear in the arrangement but for the casts.
		const std::size_t faceCount = mesh.faces.size();
		const auto		  weightAlong = [&](std::size_t halfEdge, std::size_t group) {
			   const HalfEdge& half = mesh.halfEdges[halfEdge];
			   const int	   weight = weights[half.edge * groupCount + group];
			   return half.origin == arrangement.edges[half.edge].from ? weight : -weight;
		};
		std::vector<int>		 faceWinding(faceCount * groupCount, 0);
		std::vector<bool>		 faceReached(faceCount, false);
		std::vector<std::size_t> pending;
		for (std::size_t seed = 0; seed < faceCount; ++seed) {
			if (faceReached[seed] || mesh.faces[seed].halfEdges.empty()) {
				continue;
			}
			const std::size_t	   seedHalf = mesh.faces[seed].halfEdges.front();
			const ArrangementEdge& edge = arrangement.edges[mesh.halfEdges[seedHalf].edge];
			const Vec2i64		   from = arrangement.vertices[edge.from];
			const Vec2i64		   to = arrangement.vertices[edge.to];
			const Vec2i64		   middle = from + to;
			const Vec2i64		   direction = to - from;
			const bool			   upward = direction.y == 0;
			const bool			   computedIsRight = upward ? direction.x < 0 : direction.y > 0;
			const bool			   seedIsLeft = mesh.halfEdges[seedHalf].origin == edge.from;
			for (std::size_t group = 0; group < groupCount; ++group) {
				const auto& [low, high] = groupBoxes[group];
				if (!groupBoxSet[group] || middle.x < low.x || middle.x > high.x || middle.y < low.y || middle.y > high.y) {
					continue;
				}
				const int weight = weights[mesh.halfEdges[seedHalf].edge * groupCount + group];
				const int computed = windingAt(groupEdges[group], middle, upward);
				const int right = computedIsRight ? computed : computed - weight;
				faceWinding[seed * groupCount + group] = seedIsLeft ? right + weight : right;
			}
			faceReached[seed] = true;
			pending.push_back(seed);
			while (!pending.empty()) {
				const std::size_t face = pending.back();
				pending.pop_back();
				for (const std::size_t halfEdge : mesh.faces[face].halfEdges) {
					const std::size_t across = mesh.halfEdges[mesh.halfEdges[halfEdge].twin].face;
					if (faceReached[across]) {
						continue;
					}
					faceReached[across] = true;
					for (std::size_t group = 0; group < groupCount; ++group) {
						faceWinding[across * groupCount + group] = faceWinding[face * groupCount + group] - weightAlong(halfEdge, group);
					}
					pending.push_back(across);
				}
			}
		}
		std::vector<bool> faceInside(faceCount, false);
		for (std::size_t face = 0; face < faceCount; ++face) {
			for (std::size_t group = 0; group < groupCount && !faceInside[face]; ++group) {
				faceInside[face] = faceWinding[face * groupCount + group] != 0;
			}
		}

		const std::vector<ResultLoop> loops =
			walkBoundary(mesh, [&](std::size_t halfEdge) { return faceInside[mesh.halfEdges[halfEdge].face]; }, false);

		std::vector<Ring> pieces;
		for (const ResultLoop& loop : loops) {
			splitAtRepeatedVertices(loop.ring, pieces);
		}
		std::vector<RegionPolygon> region;
		std::vector<Ring>		   holes;
		for (Ring& piece : pieces) {
			simplifyRing(piece, kBooleanSimplifyEpsMm);
			const int sign = piece.size() < 3 ? 0 : signedAreaDoubled(piece).sign();
			if (sign > 0) {
				region.push_back({std::move(piece), {}});
			} else if (sign < 0) {
				holes.push_back(std::move(piece));
			}
		}

		// A hole belongs to the smallest outline round the middle of its first edge.
		std::vector<Ring> doubledOuters;
		doubledOuters.reserve(region.size());
		for (const RegionPolygon& polygon : region) {
			Ring doubled;
			doubled.reserve(polygon.outer.size());
			for (const Vec2i64& point : polygon.outer) {
				doubled.push_back(point * 2);
			}
			doubledOuters.push_back(std::move(doubled));
		}
		for (Ring& hole : holes) {
			const Vec2i64 probe = hole[0] + hole[1];
			std::size_t	  owner = region.size();
			for (std::size_t i = 0; i < region.size(); ++i) {
				if (pointInPolygon(probe, doubledOuters[i]) != PointInPolygon::Inside) {
					continue;
				}
				if (owner == region.size() || signedAreaDoubled(region[i].outer) < signedAreaDoubled(region[owner].outer)) {
					owner = i;
				}
			}
			if (owner < region.size()) {
				region[owner].holes.push_back(std::move(hole));
			}
		}
		return region;
	}

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
