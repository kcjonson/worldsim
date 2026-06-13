#include "Arrangement.h"

#include "../core/Int128.h"

#include <algorithm>
#include <map>

namespace geometry {

	namespace {

		// A segment still being resolved. Carries the provenance accumulated so
		// far; sub-segments inherit their parent's provenance, and coincident
		// overlaps union it at dedup time.
		struct WorkSegment {
			Vec2i64					 a;
			Vec2i64					 b;
			std::vector<std::int64_t> provenance;
		};

		bool collinear(const Vec2i64& a, const Vec2i64& b, const Vec2i64& c) {
			return cross(b - a, c - a).sign() == 0;
		}

		// Is p strictly interior to segment [a,b] (collinear, between, not an
		// endpoint)? Used to decide whether a candidate point actually splits the
		// segment. Assumes a != b.
		bool strictlyInterior(const Vec2i64& p, const Vec2i64& a, const Vec2i64& b) {
			if (p == a || p == b) {
				return false;
			}
			if (!collinear(a, b, p)) {
				return false;
			}
			return p.x >= std::min(a.x, b.x) && p.x <= std::max(a.x, b.x) && p.y >= std::min(a.y, b.y) &&
				   p.y <= std::max(a.y, b.y);
		}

		// Order two collinear points along [a,b]'s direction by projection onto
		// the dominant axis, returning them low-to-high.
		void orderAlong(const Vec2i64& a, const Vec2i64& b, Vec2i64& lo, Vec2i64& hi) {
			if (b < a) {
				lo = b;
				hi = a;
			} else {
				lo = a;
				hi = b;
			}
		}

		// Collect every point at which `other` forces a split of segment `s`'s
		// interior. Returns the interior split points (never s's own endpoints).
		// Handles all four SegmentRelation cases plus the collinear-endpoint
		// incidence the predicate reports as EndpointTouch.
		void interiorSplitPoints(const WorkSegment& s, const WorkSegment& other, std::vector<Vec2i64>& out) {
			const SegmentIntersection r = intersectSegments(s.a, s.b, other.a, other.b);
			switch (r.relation) {
				case SegmentRelation::Disjoint:
					return;
				case SegmentRelation::ProperCrossing:
					if (strictlyInterior(r.point, s.a, s.b)) {
						out.push_back(r.point);
					}
					return;
				case SegmentRelation::EndpointTouch:
					if (strictlyInterior(r.point, s.a, s.b)) {
						out.push_back(r.point);
					}
					return;
				case SegmentRelation::CollinearOverlap:
					// The shared subsegment's endpoints split s wherever they fall
					// in its interior; the overlap body itself is handled by the
					// dedup pass once both segments share those vertices.
					if (strictlyInterior(r.overlapStart, s.a, s.b)) {
						out.push_back(r.overlapStart);
					}
					if (strictlyInterior(r.overlapEnd, s.a, s.b)) {
						out.push_back(r.overlapEnd);
					}
					return;
			}
		}

	} // namespace

	Arrangement buildArrangement(const std::vector<InputSegment>& segments) {
		std::vector<WorkSegment> work;
		work.reserve(segments.size());
		for (const InputSegment& in : segments) {
			if (in.a == in.b) {
				continue; // reject zero-length input
			}
			work.push_back({in.a, in.b, {in.index}});
		}

		// Split to a fixpoint. Each pass gathers every interior incidence on each
		// segment (from the exact predicate) and splits there. A rounded crossing
		// point added this pass may newly land on a third segment or coincide with
		// another rounded point; the next pass sees that incidence and splits
		// again. The loop terminates because every split strictly shortens a
		// segment and points live on the finite integer-mm grid, so the supply of
		// distinct interior points is bounded.
		bool changed = true;
		while (changed) {
			changed = false;
			std::vector<WorkSegment> next;
			next.reserve(work.size());

			for (std::size_t i = 0; i < work.size(); ++i) {
				const WorkSegment& s = work[i];

				std::vector<Vec2i64> cuts;
				for (std::size_t j = 0; j < work.size(); ++j) {
					if (i == j) {
						continue;
					}
					interiorSplitPoints(s, work[j], cuts);
				}

				if (cuts.empty()) {
					next.push_back(s);
					continue;
				}

				// Order the cut points along the segment and emit the chain of
				// sub-segments. Dedup identical cuts so a point hit by several
				// neighbors splits only once.
				std::sort(cuts.begin(), cuts.end(), [&](const Vec2i64& p, const Vec2i64& q) {
					return s.a < s.b ? p < q : q < p;
				});
				cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

				Vec2i64 prev = s.a;
				for (const Vec2i64& c : cuts) {
					if (c != prev) {
						next.push_back({prev, c, s.provenance});
						prev = c;
					}
				}
				if (s.b != prev) {
					next.push_back({prev, s.b, s.provenance});
				}
				changed = true;
			}

			work.swap(next);
		}

		// Canonicalize vertices: collect unique points, sort, assign indices.
		std::map<Vec2i64, std::size_t> vertexIndex;
		for (const WorkSegment& s : work) {
			vertexIndex.emplace(s.a, 0);
			vertexIndex.emplace(s.b, 0);
		}
		Arrangement out;
		out.vertices.reserve(vertexIndex.size());
		for (auto& [pt, idx] : vertexIndex) {
			idx = out.vertices.size();
			out.vertices.push_back(pt);
		}

		// Build edges keyed by canonical {from,to} (from < to), merging the
		// provenance of every work segment that maps onto the same vertex pair.
		// This is where coincident edges collapse to one: two segments covering
		// the same sub-segment land on the identical key and union provenance.
		std::map<std::pair<std::size_t, std::size_t>, std::vector<std::int64_t>> edgeMap;
		for (const WorkSegment& s : work) {
			if (s.a == s.b) {
				continue; // defensive: never emit zero-length
			}
			std::size_t u = vertexIndex[s.a];
			std::size_t v = vertexIndex[s.b];
			if (u > v) {
				std::swap(u, v);
			}
			auto&					   prov = edgeMap[{u, v}];
			prov.insert(prov.end(), s.provenance.begin(), s.provenance.end());
		}

		out.edges.reserve(edgeMap.size());
		for (auto& [key, prov] : edgeMap) {
			std::sort(prov.begin(), prov.end());
			prov.erase(std::unique(prov.begin(), prov.end()), prov.end());
			out.edges.push_back({key.first, key.second, std::move(prov)});
		}

		return out;
	}

} // namespace geometry
