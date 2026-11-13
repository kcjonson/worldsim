#include "Visibility.h"

#include "../offset/WallOffset.h" // simplifyRing
#include "../predicates/Predicates.h"
#include "SightPolygon.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

// Design: angular sweep over candidate directions. The naive endpoint ray-cast
// (cast a ray straight at each endpoint plus a nudge a hair to each side) is the
// textbook method, but its nudge cannot survive integer snapping at small radii:
// a ray a hair off an endpoint rounds to the same pixel as the endpoint, so the
// near corner and the far-field continuation past it collapse to one angle, and
// the polygon zigzags across its own interior. We avoid the nudge entirely.
//
// The sight disc is the sight polygon (SightPolygon.h), so the result is the part
// of that polygon the observer sees. The candidate ray directions are its vertices,
// every occluder endpoint inside the sight circle, every point where an occluder
// crosses the polygon's rim, and every crossing of two occluders, sorted CCW with
// the exact angleLess comparator and deduplicated by angle so exactly one ray is
// cast per distinct direction. Along each ray we take the nearest blocker hit (an occluder edge, or
// the rim when none is nearer) and emit it as a boundary vertex. Because the
// directions are unique by angle, the near/far transition at a grazed endpoint is
// split across adjacent directions (the endpoint ray gives the near corner; the
// neighbouring rim or endpoint ray gives the far field), so the CCW walk is a star
// polygon, up to the millimetre snapping of the hits (simpleAround checks it).
//
// Exactness: the nearest-blocker pick is exact integer (Int128 squared-distance
// comparison); the emitted points are hits snapped to the millimetre by
// intersectSegments, or the rim's own vertices. No trigonometry runs: the rim comes
// from a compile-time table, so every toolchain builds the same ring.

namespace geometry {

	namespace {

		Int128 distSq(const Vec2i64& observer, const Vec2i64& p) {
			const Vec2i64 d = p - observer;
			return dot(d, d);
		}

		// The nearest visible point along the ray from `observer` in direction `dir`,
		// out to the rim (`rimPoint`, where the ray leaves the sight polygon),
		// evaluated on one angular SIDE of the ray: side = +1 takes the ray as rotated
		// an infinitesimal CCW, side = -1 as rotated CW. This is the heart of the sweep.
		// At a grazed endpoint the near corner is visible on one side and the view
		// opens past it on the other; evaluating both sides at the endpoint direction
		// yields the two distinct boundary vertices (near corner + far field) that a
		// single ray cast cannot separate. The perturbation is resolved exactly with
		// orientation predicates, never with a float nudge.
		//
		// An occluder edge contributes a hit when the (perturbed) ray meets it:
		//  - ProperCrossing: the ray crosses the edge interior; robust to the
		//    infinitesimal rotation, so it counts on both sides. Distance = the
		//    mm-snapped crossing point.
		//  - EndpointTouch: the ray grazes the edge at a single point. If that point
		//    is the edge's own endpoint sitting on the ray, the edge lies entirely to
		//    one angular side of the ray; the perturbed ray hits it only on that side
		//    (sign of cross(dir, otherEnd - observer)). A touch at the observer
		//    (distance 0) never blocks. Touches in the edge interior count on both
		//    sides.
		//  - CollinearOverlap: the ray runs along the edge; the nearer overlap end is
		//    the first opaque point, counted on both sides.
		Vec2i64 castSide(const Vec2i64& observer, const Vec2i64& dir, const Vec2i64& farExact, const Vec2i64& rimPoint, int side,
						 const std::vector<OccluderSegment>& occluders) {
			// Occluder hits nearer than the rim override it; hits past it are ignored.
			Vec2i64 best   = rimPoint;
			Int128	bestSq = distSq(observer, rimPoint);

			auto consider = [&](const Vec2i64& hit) {
				const Int128 sq = distSq(observer, hit);
				if (sq.sign() > 0 && sq < bestSq) {
					bestSq = sq;
					best   = hit;
				}
			};

			for (const OccluderSegment& occ : occluders) {
				// The ray is the finite segment observer->farExact, which lies EXACTLY
				// on `dir` (an integer multiple of it) and extends past the rim, so
				// it passes precisely through endpoints in this direction.
				const SegmentIntersection si = intersectSegments(observer, farExact, occ.a, occ.b);
				switch (si.relation) {
					case SegmentRelation::ProperCrossing:
						consider(si.point);
						break;
					case SegmentRelation::EndpointTouch: {
						if (si.point == observer) {
							break; // occluder touches the observer; no occlusion
						}
						// Is the touch the edge's own endpoint resting on the ray (a
						// graze), or a point in the edge interior the ray crosses? If
						// the touched point equals an edge endpoint, classify the side
						// the edge lies on by the orientation of its OTHER endpoint
						// about the ray direction.
						const bool atA = (si.point == occ.a);
						const bool atB = (si.point == occ.b);
						if (atA || atB) {
							const Vec2i64 other = atA ? occ.b : occ.a;
							const int	  s		= cross(dir, other - observer).sign();
							// s > 0: other end is CCW of the ray, edge on the +side.
							// Count the graze only when the perturbation is toward it.
							if (s != 0 && s != side) {
								break;
							}
						}
						consider(si.point);
						break;
					}
					case SegmentRelation::CollinearOverlap:
						consider(si.overlapStart);
						consider(si.overlapEnd);
						break;
					case SegmentRelation::Disjoint:
						break;
				}
			}
			return best;
		}

		// Strict CCW total order of integer direction vectors about the observer:
		// angleLess, then exact squared length, then lexicographic. Copied from
		// WallOffset's junction-star ordering: angleLess alone is not a strict order
		// (equal directions tie), so a deterministic tiebreak is required.
		struct DirLess {
			bool operator()(const Vec2i64& l, const Vec2i64& r) const {
				if (angleLess(l, r)) {
					return true;
				}
				if (angleLess(r, l)) {
					return false;
				}
				const Int128 ll = dot(l, l);
				const Int128 rr = dot(r, r);
				if (ll != rr) {
					return ll < rr;
				}
				return l < r;
			}
			bool sameAngle(const Vec2i64& l, const Vec2i64& r) const { return !angleLess(l, r) && !angleLess(r, l); }
		};

	} // namespace

	Ring computeVisibilityPolygon(const Vec2i64& observer, std::int64_t sightRadiusMm, const std::vector<OccluderSegment>& occluders) {
		Ring result;
		if (sightRadiusMm <= 0) {
			return result;
		}

		Ring rim = sightPolygon(observer, sightRadiusMm);
		rim.erase(std::unique(rim.begin(), rim.end()), rim.end());
		if (rim.size() >= 2 && rim.front() == rim.back()) {
			rim.pop_back();
		}
		if (rim.size() < 3) {
			return result;
		}
		// The rim's directions from the observer, ascending by angleLess: vertex 0 lies
		// along +x, where angleLess starts.
		std::vector<Vec2i64> rimDirs;
		rimDirs.reserve(rim.size());
		for (const Vec2i64& vertex : rim) {
			rimDirs.push_back(vertex - observer);
		}

		// Candidate sweep directions: the rim's vertices; every occluder endpoint inside
		// the sight circle (one outside it changes nothing the observer sees); every
		// point where an occluder crosses the rim, where the nearest blocker turns from
		// the occluder to the rim; and every crossing of two occluders inside the circle.
		std::vector<Vec2i64> dirs = rimDirs;
		dirs.reserve(rim.size() + occluders.size() * 2);
		const Int128	   radiusSq	  = Int128::product(sightRadiusMm, sightRadiusMm);
		const std::int64_t inradius	  = sightInradius(sightRadiusMm);
		const Int128	   inradiusSq = Int128::product(std::max<std::int64_t>(inradius, 0), std::max<std::int64_t>(inradius, 0));
		for (const OccluderSegment& occ : occluders) {
			const Int128 firstSq  = distSq(observer, occ.a);
			const Int128 secondSq = distSq(observer, occ.b);
			if (occ.a != observer && firstSq <= radiusSq) {
				dirs.push_back(occ.a - observer);
			}
			if (occ.b != observer && secondSq <= radiusSq) {
				dirs.push_back(occ.b - observer);
			}
			if (firstSq < inradiusSq && secondSq < inradiusSq) {
				continue;
			}
			for (std::size_t k = 0; k < rim.size(); ++k) {
				const SegmentIntersection crossing = intersectSegments(occ.a, occ.b, rim[k], rim[(k + 1) % rim.size()]);
				if (crossing.relation == SegmentRelation::ProperCrossing && crossing.point != observer) {
					dirs.push_back(crossing.point - observer);
				}
			}
		}
		// Where two occluders cross, the nearer of them changes.
		for (std::size_t i = 0; i < occluders.size(); ++i) {
			const OccluderSegment& first = occluders[i];
			for (std::size_t j = i + 1; j < occluders.size(); ++j) {
				const OccluderSegment& second = occluders[j];
				if (std::max(first.a.x, first.b.x) < std::min(second.a.x, second.b.x) || std::max(second.a.x, second.b.x) < std::min(first.a.x, first.b.x) ||
					std::max(first.a.y, first.b.y) < std::min(second.a.y, second.b.y) || std::max(second.a.y, second.b.y) < std::min(first.a.y, first.b.y)) {
					continue;
				}
				const SegmentIntersection crossing = intersectSegments(first.a, first.b, second.a, second.b);
				if (crossing.relation == SegmentRelation::ProperCrossing && crossing.point != observer && distSq(observer, crossing.point) <= radiusSq) {
					dirs.push_back(crossing.point - observer);
				}
			}
		}

		const DirLess less;
		std::sort(dirs.begin(), dirs.end(), less);
		// Collapse exactly-coincident directions (same ray); keep one per angle.
		dirs.erase(std::unique(dirs.begin(), dirs.end(), [&](const Vec2i64& a, const Vec2i64& b) { return less.sameAngle(a, b); }), dirs.end());

		// Two boundary vertices per distinct direction, evaluated on each angular
		// side: the CW-side hit (the trailing boundary of the sector ending at this
		// ray) then the CCW-side hit (the leading boundary of the next sector).
		// Emitting them in that order, while walking directions CCW, lays the star
		// boundary down without any sort: at a grazed endpoint the CW side gives the
		// far field arriving into the corner and the CCW side gives the near corner
		// departing it (or vice versa), so the near/far transition is two adjacent
		// vertices, not two vertices fighting over one angle.
		result.reserve(dirs.size() * 2);
		for (const Vec2i64& dir : dirs) {
			// Exact far point: an integer multiple of `dir` whose length exceeds the
			// sight radius, so the ray segment observer->farExact lies precisely on
			// the ray and reaches past the rim. The multiplier is an integer count
			// (the only float here, and it cannot move the ray off `dir`).
			const double dirLen = std::sqrt(static_cast<double>(dir.x) * dir.x + static_cast<double>(dir.y) * dir.y);
			std::int64_t scale	= static_cast<std::int64_t>(static_cast<double>(sightRadiusMm) / dirLen) + 2;
			// Cap the multiplier so dir*scale (and observer+dir*scale) stay within
			// int64 -- Vec2i64::operator* is unchecked. In practice scale ~
			// radius/|dir| keeps the product near the radius; this only guards
			// pathological inputs and never trims a scale we actually need.
			const std::int64_t kSafe   = (std::numeric_limits<std::int64_t>::max)() / 4;
			const std::int64_t maxComp = std::max<std::int64_t>(1, std::max(std::llabs(dir.x), std::llabs(dir.y)));
			scale					   = std::max<std::int64_t>(1, std::min(scale, kSafe / maxComp));
			const Vec2i64 farExact	   = observer + dir * scale;

			// Where the ray leaves the sight polygon: the rim vertex in this exact
			// direction, or the snapped crossing with the rim side whose angular span
			// holds it.
			const auto		  after	   = std::upper_bound(rimDirs.begin(), rimDirs.end(), dir, [](const Vec2i64& l, const Vec2i64& r) { return angleLess(l, r); });
			const std::size_t rimSide  = static_cast<std::size_t>(after - rimDirs.begin()) - 1;
			Vec2i64			  rimPoint = rim[rimSide];
			if (!less.sameAngle(rimDirs[rimSide], dir)) {
				rimPoint = intersectSegments(observer, farExact, rim[rimSide], rim[(rimSide + 1) % rim.size()]).point;
			}

			const Vec2i64 cw  = castSide(observer, dir, farExact, rimPoint, -1, occluders);
			const Vec2i64 ccw = castSide(observer, dir, farExact, rimPoint, +1, occluders);
			if (cw != observer) {
				result.push_back(cw);
			}
			if (ccw != cw && ccw != observer) {
				result.push_back(ccw);
			}
		}

		// Dedup consecutive coincident vertices and the wrap seam, then drop any
		// vertex at the observer. The direction walk already emits CCW.
		Ring deduped;
		deduped.reserve(result.size());
		for (const Vec2i64& v : result) {
			if (v == observer) {
				continue;
			}
			if (!deduped.empty() && deduped.back() == v) {
				continue;
			}
			deduped.push_back(v);
		}
		if (deduped.size() >= 2 && deduped.front() == deduped.back()) {
			deduped.pop_back();
		}

		// Collapse exactly-collinear triples. Adjacent directions along one flat
		// blocker face emit a run of vertices that are world-collinear and so add no
		// shape; simplifyRing with epsilon 0 drops only those exact-collinear middles,
		// leaving the minimal star polygon.
		simplifyRing(deduped, 0);
		return deduped;
	}

	bool angleBefore(const UnwrappedAngle& first, const UnwrappedAngle& second) {
		if (first.turns != second.turns) {
			return first.turns < second.turns;
		}
		return angleLess(first.direction, second.direction);
	}

	bool unwrapAngles(const Ring& ring, const Vec2i64& centre, std::vector<UnwrappedAngle>& out) {
		out.clear();
		if (ring.empty()) {
			return false;
		}
		Vec2i64 previous = ring.front() - centre;
		if (previous == Vec2i64{0, 0}) {
			return false;
		}
		std::int64_t turns = 0;
		out.push_back({turns, previous});
		for (std::size_t i = 1; i <= ring.size(); ++i) {
			const Vec2i64 next = ring[i % ring.size()] - centre;
			if (next == Vec2i64{0, 0}) {
				return false;
			}
			const int turn = cross(previous, next).sign();
			if (turn == 0 && dot(previous, next).sign() < 0) {
				return false;
			}
			if (turn > 0 && angleLess(next, previous)) {
				++turns;
			} else if (turn < 0 && angleLess(previous, next)) {
				--turns;
			}
			out.push_back({turns, next});
			previous = next;
		}
		return true;
	}

	bool simpleAround(const Ring& ring, const Vec2i64& centre) {
		const std::size_t count = ring.size();
		if (count < 3) {
			return false;
		}
		for (std::size_t i = 0; i < count; ++i) {
			if (ring[i] == ring[(i + 1) % count]) {
				return false;
			}
		}
		std::vector<UnwrappedAngle> angles;
		if (!unwrapAngles(ring, centre, angles) || angles.back().turns != 1) {
			return false;
		}
		UnwrappedAngle lowest	= angles.front();
		UnwrappedAngle highest = angles.front();
		for (const UnwrappedAngle& angle : angles) {
			lowest  = angleBefore(angle, lowest) ? angle : lowest;
			highest = angleBefore(highest, angle) ? angle : highest;
		}
		// Under two turns from lowest to highest, a pair of spans meets round the circle only
		// as they are or with one of them a turn on.
		if (!angleBefore(highest, UnwrappedAngle{lowest.turns + 2, lowest.direction})) {
			return false;
		}

		struct Span {
			UnwrappedAngle low;
			UnwrappedAngle high;
			std::size_t	   edge = 0;
		};
		std::vector<Span> spans;
		spans.reserve(2 * count);
		for (std::size_t i = 0; i < count; ++i) {
			const bool			  forward = !angleBefore(angles[i + 1], angles[i]);
			const UnwrappedAngle& low	= forward ? angles[i] : angles[i + 1];
			const UnwrappedAngle& high	= forward ? angles[i + 1] : angles[i];
			spans.push_back({low, high, i});
		}
		for (std::size_t i = 0; i < count; ++i) {
			const Span& span = spans[i];
			spans.push_back({{span.low.turns + 1, span.low.direction}, {span.high.turns + 1, span.high.direction}, span.edge});
		}
		// The spans arrive nearly in order (they are out of it only where the ring steps back), so
		// an insertion sort is close to one pass.
		for (std::size_t i = 1; i < spans.size(); ++i) {
			const Span	moving = spans[i];
			std::size_t at	   = i;
			for (; at > 0 && angleBefore(moving.low, spans[at - 1].low); --at) {
				spans[at] = spans[at - 1];
			}
			spans[at] = moving;
		}

		// Adjacent edges may share their common vertex and nothing more; others nothing at all.
		auto breaksSimplicity = [&](std::size_t first, std::size_t second) {
			if (first == second) {
				return false;
			}
			const SegmentRelation relation = intersectSegments(ring[first], ring[(first + 1) % count], ring[second], ring[(second + 1) % count]).relation;
			const bool			  adjacent = (first + 1) % count == second || (second + 1) % count == first;
			return relation != SegmentRelation::Disjoint && !(adjacent && relation == SegmentRelation::EndpointTouch);
		};
		std::vector<const Span*> open;
		for (const Span& span : spans) {
			open.erase(std::remove_if(open.begin(), open.end(), [&](const Span* other) { return angleBefore(other->high, span.low); }), open.end());
			for (const Span* other : open) {
				if (breaksSimplicity(span.edge, other->edge)) {
					return false;
				}
			}
			open.push_back(&span);
		}
		return true;
	}

	bool hasLineOfSight(const Vec2i64& observer, const Vec2i64& target, std::int64_t sightRadiusMm, const std::vector<OccluderSegment>& occluders) {
		if (sightRadiusMm <= 0) {
			return false; // non-positive sight radius sees nothing (mirrors computeVisibilityPolygon)
		}
		if (observer == target) {
			return true;
		}
		// Range gate: exact squared-distance comparison, no float.
		const Vec2i64 d		   = target - observer;
		const Int128  rangeSq  = dot(d, d);
		const Int128  radiusSq = Int128::product(sightRadiusMm, sightRadiusMm);
		if (rangeSq > radiusSq) {
			return false;
		}

		// Any occluder that the sight segment properly crosses, or runs collinearly
		// along, blocks the view. A bare endpoint touch does NOT block: grazing the
		// tip of a wall (or the observer/target sitting on an endpoint) still leaves
		// a clear line, matching the polygon's "see up to the near face" behaviour.
		for (const OccluderSegment& occ : occluders) {
			const SegmentIntersection si = intersectSegments(observer, target, occ.a, occ.b);
			if (si.relation == SegmentRelation::ProperCrossing || si.relation == SegmentRelation::CollinearOverlap) {
				return false;
			}
		}
		return true;
	}

} // namespace geometry
