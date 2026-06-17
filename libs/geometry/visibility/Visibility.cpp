#include "Visibility.h"

#include "../offset/WallOffset.h" // simplifyRing
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

// Design: angular sweep over candidate directions. The naive endpoint ray-cast
// (cast a ray straight at each endpoint plus a nudge a hair to each side) is the
// textbook method, but its nudge cannot survive integer snapping at small radii:
// a ray a hair off an endpoint rounds to the same pixel as the endpoint, so the
// near corner and the far-field continuation past it collapse to one angle, and
// the polygon zigzags across its own interior. We avoid the nudge entirely.
//
// The candidate ray directions are every occluder endpoint direction (exact
// integer vector from the observer) plus the sight-circle's N-gon directions,
// sorted CCW with the exact angleLess comparator and deduplicated by angle so
// exactly one ray is cast per distinct direction. Along each ray we take the
// nearest blocker hit (an occluder edge, or the sight circle when none is nearer)
// and emit it as a boundary vertex. Because the directions are unique by angle,
// the near/far transition at a grazed endpoint is split across adjacent
// directions (the endpoint ray gives the near corner; the neighbouring circle or
// endpoint ray gives the far field), so no two vertices share an angle and the
// CCW walk is a clean star polygon. The dense circle directions also bound every
// unoccluded arc to the circle.
//
// Exactness: the nearest-blocker pick and the emitted points are exact integer
// (Int128 squared-distance comparison; intersectSegments for the snapped point).
// The only float steps are the sight-circle polygonalization and the float
// direction handed to circlePoint; neither affects the exactness of an occluder
// hit, which comes straight from intersectSegments.

namespace geometry {

	namespace {

		// Sight-circle polygonalization. The circle has no exact integer form, so
		// it is approximated as a regular N-gon (float trig + std::llround), the
		// same policy as the flora octagon and the renderer's 64-gon. N is chosen
		// so the chord-to-arc sagitta error stays well under ~1-2 mm even at large
		// radii: sagitta = r * (1 - cos(pi/N)). At N=720, sagitta/r ~= 9.5e-6, so a
		// 100 m (1e5 mm) radius errs under 1 mm; a 1 km radius under 10 mm. This is
		// the ONE inexact construction step in the module.
		constexpr int kCircleSegments = 720;

		Int128 distSq(const Vec2i64& observer, const Vec2i64& p) {
			const Vec2i64 d = p - observer;
			return dot(d, d);
		}

		// A far point along direction (dirX, dirY), at the sight radius (float-snapped
		// to mm). Used as the second endpoint of a finite ray segment passed to
		// intersectSegments, and as the boundary point when no occluder is nearer.
		Vec2i64 circlePoint(const Vec2i64& observer, double dirX, double dirY, std::int64_t radiusMm) {
			const double len = std::sqrt(dirX * dirX + dirY * dirY);
			if (len == 0.0) {
				return observer;
			}
			const double r = static_cast<double>(radiusMm);
			return {
				observer.x + std::llround(dirX / len * r),
				observer.y + std::llround(dirY / len * r),
			};
		}

		// The nearest visible point along the ray from `observer` in direction `dir`,
		// out to the sight circle (`far`), evaluated on one angular SIDE of the ray:
		// side = +1 takes the ray as rotated an infinitesimal CCW, side = -1 as
		// rotated CW, side = 0 takes the ray exactly. This is the heart of the sweep.
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
		Vec2i64 castSide(const Vec2i64& observer, const Vec2i64& dir, const Vec2i64& farExact,
						 const Vec2i64& circleAtDir, std::int64_t radiusMm, int side,
						 const std::vector<OccluderSegment>& occluders) {
			// Default boundary is the sight circle in this direction. Occluder hits
			// nearer than the circle override it; hits past the circle are ignored
			// (clamped to the circle). bestSq tracks the circle distance as the cap.
			Vec2i64 best   = circleAtDir;
			Int128	bestSq = Int128::product(radiusMm, radiusMm);

			auto consider = [&](const Vec2i64& hit) {
				const Int128 sq = distSq(observer, hit);
				if (sq.sign() > 0 && sq < bestSq) {
					bestSq = sq;
					best   = hit;
				}
			};

			for (const OccluderSegment& occ : occluders) {
				// The ray is the finite segment observer->farExact, which lies EXACTLY
				// on `dir` (an integer multiple of it) and extends past the circle, so
				// it passes precisely through endpoints in this direction. Snapping it
				// to the circle (a float point off the exact ray) would miss them.
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
						// about the ray direction. side 0 keeps the touch (exact ray).
						const bool atA = (si.point == occ.a);
						const bool atB = (si.point == occ.b);
						if (side != 0 && (atA || atB)) {
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

	Ring computeVisibilityPolygon(const Vec2i64& observer, std::int64_t sightRadiusMm,
								   const std::vector<OccluderSegment>& occluders) {
		Ring result;
		if (sightRadiusMm <= 0) {
			return result;
		}

		// Candidate sweep directions: each occluder endpoint direction (exact
		// integer vector from the observer) plus the sight-circle N-gon directions
		// (float-snapped). The endpoint directions are where the nearest blocker can
		// change; the circle directions bound the unoccluded arcs to the circle.
		std::vector<Vec2i64> dirs;
		dirs.reserve(static_cast<std::size_t>(kCircleSegments) + occluders.size() * 2);

		constexpr double kTwoPi = 2.0 * std::numbers::pi;
		for (int i = 0; i < kCircleSegments; ++i) {
			const double ang = static_cast<double>(i) * (kTwoPi / static_cast<double>(kCircleSegments));
			// Snap the circle direction to an integer vector at the sight radius so
			// it lives in the same exact space as the endpoint directions.
			const Vec2i64 p = circlePoint(observer, std::cos(ang), std::sin(ang), sightRadiusMm);
			if (p != observer) {
				dirs.push_back(p - observer);
			}
		}
		for (const OccluderSegment& occ : occluders) {
			if (occ.a != observer) {
				dirs.push_back(occ.a - observer);
			}
			if (occ.b != observer) {
				dirs.push_back(occ.b - observer);
			}
		}

		const DirLess less;
		std::sort(dirs.begin(), dirs.end(), less);
		// Collapse exactly-coincident directions (same ray); keep one per angle.
		dirs.erase(std::unique(dirs.begin(), dirs.end(),
							   [&](const Vec2i64& a, const Vec2i64& b) { return less.sameAngle(a, b); }),
				   dirs.end());

		const std::size_t n = dirs.size();
		if (n < 3) {
			return result;
		}

		// Two boundary vertices per distinct direction, evaluated on each angular
		// side: the CW-side hit (the trailing boundary of the sector ending at this
		// ray) then the CCW-side hit (the leading boundary of the next sector).
		// Emitting them in that order, while walking directions CCW, lays the star
		// boundary down without any sort: at a grazed endpoint the CW side gives the
		// far field arriving into the corner and the CCW side gives the near corner
		// departing it (or vice versa), so the near/far transition is two adjacent
		// vertices, not two vertices fighting over one angle.
		result.reserve(n * 2);
		for (const Vec2i64& dir : dirs) {
			// Exact far point: an integer multiple of `dir` whose length exceeds the
			// sight radius, so the ray segment observer->farExact lies precisely on
			// the ray and reaches past the circle. The multiplier is an integer count
			// (the only float here, and it cannot move the ray off `dir`).
			const double dirLen = std::sqrt(static_cast<double>(dir.x) * dir.x + static_cast<double>(dir.y) * dir.y);
			if (dirLen == 0.0) {
				continue;
			}
			const std::int64_t scale   = static_cast<std::int64_t>(static_cast<double>(sightRadiusMm) / dirLen) + 2;
			const Vec2i64	   farExact = observer + dir * scale;
			// Circle clamp point in this exact direction (float-snapped, the one
			// inexact step; used only for unoccluded arcs).
			const Vec2i64 circleAtDir =
				circlePoint(observer, static_cast<double>(dir.x), static_cast<double>(dir.y), sightRadiusMm);

			const Vec2i64 cw  = castSide(observer, dir, farExact, circleAtDir, sightRadiusMm, -1, occluders);
			const Vec2i64 ccw = castSide(observer, dir, farExact, circleAtDir, sightRadiusMm, +1, occluders);
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
		// blocker face (or one circle chord) emit a run of vertices that are
		// world-collinear and so add no shape; simplifyRing with epsilon 0 drops
		// only those exact-collinear middles, leaving the minimal star polygon.
		simplifyRing(deduped, 0);
		return deduped;
	}

	bool hasLineOfSight(const Vec2i64& observer, const Vec2i64& target, std::int64_t sightRadiusMm,
						const std::vector<OccluderSegment>& occluders) {
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
