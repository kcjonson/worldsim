#include "Visibility.h"

#include "../offset/WallOffset.h" // simplifyRing
#include "../predicates/Predicates.h"
#include "SightPolygon.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <utility>

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
//
// Cost: a ray reads only the occluders whose angular cone from the observer holds
// it, nearest first, and stops once the rest lie past both sides' nearest hits; the
// directions are presorted by a rounded angle and only neighbours the rounding could
// misplace are put to the exact order. None of it changes the ring: a skipped
// occluder is disjoint from the ray or beyond its hits, rounded values only decide
// what to skip, with margins over their error, and of two hits at one distance the
// occluder first in the input wins, as reading every occluder in order would give.

namespace geometry {

	namespace {

		Int128 squaredDistance(const Vec2i64& observer, const Vec2i64& point) {
			const Vec2i64 offset = point - observer;
			return dot(offset, offset);
		}

		// The nearest visible point found so far along one angular SIDE of a ray: the
		// ray rotated an infinitesimal CCW (+1) or CW (-1). Starts at the rim, where the
		// ray leaves the sight polygon; a hit at the observer never blocks. Of hits at
		// one distance the rim wins, then the occluder first in the input, so the
		// occluders may be read in any order.
		struct NearestHit {
			static constexpr std::int64_t kRim = -1;

			Vec2i64		 point;
			Int128		 distance;
			std::int64_t source = kRim;
			double		 reach	= 0.0; // the distance in millimetres, rounded

			NearestHit(const Vec2i64& observer, const Vec2i64& rimPoint)
				: point(rimPoint),
				  distance(squaredDistance(observer, rimPoint)),
				  reach(std::sqrt(distance.toDouble())) {}

			void consider(const Vec2i64& observer, const Vec2i64& hit, std::int64_t occluder) {
				const Int128 candidate = squaredDistance(observer, hit);
				if (candidate.sign() > 0 && (candidate < distance || (candidate == distance && occluder < source))) {
					distance = candidate;
					point	 = hit;
					source	 = occluder;
					reach	 = std::sqrt(distance.toDouble());
				}
			}
		};

		// One occluder's hit on the ray observer->farExact, read for both sides at once.
		// At a grazed endpoint the near corner is visible on one side and the view opens
		// past it on the other; evaluating both sides at the endpoint direction yields
		// the two distinct boundary vertices (near corner + far field) that a single ray
		// cast cannot separate. The perturbation is resolved exactly with orientation
		// predicates, never with a float nudge.
		//  - ProperCrossing: the ray crosses the edge interior; robust to the
		//    infinitesimal rotation, so it counts on both sides. Distance = the
		//    mm-snapped crossing point.
		//  - EndpointTouch: the ray grazes the edge at a single point. If that point
		//    is the edge's own endpoint sitting on the ray, the edge lies entirely to
		//    one angular side of the ray; the perturbed ray hits it only on that side
		//    (sign of cross(direction, otherEnd - observer)). A touch at the observer
		//    (distance 0) never blocks. Touches in the edge interior count on both
		//    sides.
		//  - CollinearOverlap: the ray runs along the edge; the nearer overlap end is
		//    the first opaque point, counted on both sides.
		void castBothSides(const Vec2i64& observer, const Vec2i64& direction, const Vec2i64& farExact, const OccluderSegment& occluder, std::int64_t index,
						   NearestHit& clockwise, NearestHit& counterClockwise) {
			// The ray is the finite segment observer->farExact, which lies EXACTLY on
			// `direction` (an integer multiple of it) and extends past the rim, so it
			// passes precisely through endpoints in this direction.
			const SegmentIntersection hit = intersectSegments(observer, farExact, occluder.a, occluder.b);
			switch (hit.relation) {
				case SegmentRelation::ProperCrossing:
					clockwise.consider(observer, hit.point, index);
					counterClockwise.consider(observer, hit.point, index);
					break;
				case SegmentRelation::EndpointTouch: {
					if (hit.point == observer) {
						break;
					}
					int side = 0;
					if (hit.point == occluder.a || hit.point == occluder.b) {
						const Vec2i64 other = hit.point == occluder.a ? occluder.b : occluder.a;
						side				= cross(direction, other - observer).sign();
					}
					if (side <= 0) {
						clockwise.consider(observer, hit.point, index);
					}
					if (side >= 0) {
						counterClockwise.consider(observer, hit.point, index);
					}
					break;
				}
				case SegmentRelation::CollinearOverlap:
					clockwise.consider(observer, hit.overlapStart, index);
					clockwise.consider(observer, hit.overlapEnd, index);
					counterClockwise.consider(observer, hit.overlapStart, index);
					counterClockwise.consider(observer, hit.overlapEnd, index);
					break;
				case SegmentRelation::Disjoint:
					break;
			}
		}

		// Strict CCW total order of integer direction vectors about the observer:
		// angleLess, then exact squared length, then lexicographic. Copied from
		// WallOffset's junction-star ordering: angleLess alone is not a strict order
		// (equal directions tie), so a deterministic tiebreak is required.
		struct DirectionLess {
			bool operator()(const Vec2i64& first, const Vec2i64& second) const {
				if (angleLess(first, second)) {
					return true;
				}
				if (angleLess(second, first)) {
					return false;
				}
				const Int128 firstLength  = dot(first, first);
				const Int128 secondLength = dot(second, second);
				if (firstLength != secondLength) {
					return firstLength < secondLength;
				}
				return first < second;
			}
			bool sameAngle(const Vec2i64& first, const Vec2i64& second) const { return !angleLess(first, second) && !angleLess(second, first); }
		};

		// A direction's angle as a double in [0, 4), rising with angleLess's order but
		// rounded, so it only presorts: two directions a hair apart may come out in either
		// order or level, and the exact order is restored after.
		double roughAngle(const Vec2i64& direction) {
			const double x		= static_cast<double>(direction.x);
			const double spread = std::abs(x) + std::abs(static_cast<double>(direction.y));
			if (spread == 0.0) {
				return 2.0;
			}
			const bool upperHalf = direction.y > 0 || (direction.y == 0 && direction.x > 0);
			return upperHalf ? 1.0 - x / spread : 3.0 + x / spread;
		}

		// roughAngle is within a few units in the last place of the exact value, under
		// 1e-14; two rough angles further apart than this have their directions' exact
		// order.
		constexpr double kRoughAngleSlack = 1e-12;

		// The directions in CCW order, each angle once, by the exact order, with
		// `angles` their rough angles: a presort by roughAngle, then an insertion sort
		// that asks the exact order only of neighbours the rounding could have
		// misplaced. The order is total, so the result is what any exact sort gives.
		void sortDirections(std::vector<Vec2i64>& directions, std::vector<double>& angles) {
			const DirectionLess						less;
			std::vector<std::pair<double, Vec2i64>> keyed;
			keyed.reserve(directions.size());
			for (const Vec2i64& direction : directions) {
				keyed.emplace_back(roughAngle(direction), direction);
			}
			std::sort(keyed.begin(), keyed.end(), [](const auto& first, const auto& second) { return first.first < second.first; });
			directions.clear();
			angles.clear();
			for (const auto& [angle, direction] : keyed) {
				std::size_t at = directions.size();
				directions.push_back(direction);
				angles.push_back(angle);
				for (; at > 0 && angles[at - 1] + kRoughAngleSlack >= angle && less(direction, directions[at - 1]); --at) {
					directions[at] = directions[at - 1];
					angles[at]	   = angles[at - 1];
				}
				directions[at] = direction;
				angles[at]	   = angle;
			}
			// Keep the first of each run of one angle.
			std::size_t kept = 0;
			for (std::size_t i = 0; i < directions.size(); ++i) {
				if (kept > 0 && std::abs(angles[i] - angles[kept - 1]) <= kRoughAngleSlack && less.sameAngle(directions[kept - 1], directions[i])) {
					continue;
				}
				directions[kept] = directions[i];
				angles[kept]	 = angles[i];
				++kept;
			}
			directions.resize(kept);
			angles.resize(kept);
		}

		// The first of the sorted directions that `before` does not hold for, where
		// `before` is true of a leading run of them: found near `angle` by the rough
		// angles, then settled by the exact test.
		template <typename Before>
		std::size_t partitionPoint(const std::vector<Vec2i64>& directions, const std::vector<double>& angles, double angle, Before before) {
			std::size_t low	 = 0;
			std::size_t high = angles.size();
			while (low < high) {
				const std::size_t middle = low + (high - low) / 2;
				if (angles[middle] < angle) {
					low = middle + 1;
				} else {
					high = middle;
				}
			}
			while (low > 0 && !before(directions[low - 1])) {
				--low;
			}
			while (low < directions.size() && before(directions[low])) {
				++low;
			}
			return low;
		}

		// An occluder seen from the observer: its endpoints' directions, the clockwise one
		// first, so every ray that meets it lies in the closed cone from `low` round to
		// `high`, less than half a turn. Where its line passes through the observer
		// there is no such cone, and `throughObserver` is set.
		struct OccluderCone {
			Vec2i64 low;
			Vec2i64 high;
			bool	throughObserver = false;
		};

		OccluderCone coneOf(const Vec2i64& observer, const OccluderSegment& occluder) {
			OccluderCone cone{occluder.a - observer, occluder.b - observer};
			const int	 turn = cross(cone.low, cone.high).sign();
			if (turn < 0) {
				std::swap(cone.low, cone.high);
			}
			cone.throughObserver = turn == 0;
			return cone;
		}

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
		std::vector<Vec2i64> rimDirections;
		rimDirections.reserve(rim.size());
		for (const Vec2i64& vertex : rim) {
			rimDirections.push_back(vertex - observer);
		}
		const auto byAngle = [](const Vec2i64& first, const Vec2i64& second) { return angleLess(first, second); };
		// The rim side whose angular span holds a direction: the last rim vertex not past it.
		const auto rimSideOf = [&](const Vec2i64& direction) {
			return static_cast<std::size_t>(std::upper_bound(rimDirections.begin(), rimDirections.end(), direction, byAngle) - rimDirections.begin()) - 1;
		};

		std::vector<OccluderCone> cones;
		cones.reserve(occluders.size());
		for (const OccluderSegment& occluder : occluders) {
			cones.push_back(coneOf(observer, occluder));
		}

		// Candidate sweep directions: the rim's vertices; every occluder endpoint inside
		// the sight circle (one outside it changes nothing the observer sees); every
		// point where an occluder crosses the polygon's rim, where the nearest blocker
		// turns from the occluder to the rim; and every crossing of two occluders inside the circle.
		std::vector<Vec2i64> directions = rimDirections;
		directions.reserve(rim.size() + occluders.size() * 2);
		const Int128	   radiusSquared   = Int128::product(sightRadiusMm, sightRadiusMm);
		const std::int64_t inradius		   = sightInradius(sightRadiusMm);
		const Int128	   inradiusSquared = Int128::product(std::max<std::int64_t>(inradius, 0), std::max<std::int64_t>(inradius, 0));
		for (std::size_t index = 0; index < occluders.size(); ++index) {
			const OccluderSegment& occluder		 = occluders[index];
			const Int128		   firstSquared	 = squaredDistance(observer, occluder.a);
			const Int128		   secondSquared = squaredDistance(observer, occluder.b);
			if (occluder.a != observer && firstSquared <= radiusSquared) {
				directions.push_back(occluder.a - observer);
			}
			if (occluder.b != observer && secondSquared <= radiusSquared) {
				directions.push_back(occluder.b - observer);
			}
			if (firstSquared < inradiusSquared && secondSquared < inradiusSquared) {
				continue;
			}
			// A proper crossing with a rim side lies inside both that side's span and the
			// occluder's cone, so only the sides from the cone's low end round to its high
			// end can be crossed.
			const OccluderCone& cone	  = cones[index];
			const std::size_t	firstSide = cone.throughObserver ? 0 : rimSideOf(cone.low);
			const std::size_t	sideCount = cone.throughObserver ? rim.size() : (rimSideOf(cone.high) + rim.size() - firstSide) % rim.size() + 1;
			for (std::size_t step = 0; step < sideCount; ++step) {
				const std::size_t		  side	   = (firstSide + step) % rim.size();
				const SegmentIntersection crossing = intersectSegments(occluder.a, occluder.b, rim[side], rim[(side + 1) % rim.size()]);
				if (crossing.relation == SegmentRelation::ProperCrossing && crossing.point != observer) {
					directions.push_back(crossing.point - observer);
				}
			}
		}
		// Where two occluders cross, the nearer of them changes. Only pairs whose boxes
		// meet can cross, and a sweep along x in order of the boxes' low x finds them.
		struct Box {
			std::int64_t lowX;
			std::int64_t highX;
			std::int64_t lowY;
			std::int64_t highY;
		};
		std::vector<Box>		 boxes;
		std::vector<std::size_t> byLowX(occluders.size());
		boxes.reserve(occluders.size());
		for (std::size_t index = 0; index < occluders.size(); ++index) {
			const OccluderSegment& occluder = occluders[index];
			boxes.push_back({std::min(occluder.a.x, occluder.b.x), std::max(occluder.a.x, occluder.b.x), std::min(occluder.a.y, occluder.b.y), std::max(occluder.a.y, occluder.b.y)});
			byLowX[index] = index;
		}
		std::sort(byLowX.begin(), byLowX.end(), [&](std::size_t first, std::size_t second) { return boxes[first].lowX < boxes[second].lowX; });
		for (std::size_t position = 0; position < byLowX.size(); ++position) {
			const Box& box = boxes[byLowX[position]];
			for (std::size_t later = position + 1; later < byLowX.size() && boxes[byLowX[later]].lowX <= box.highX; ++later) {
				const Box& other = boxes[byLowX[later]];
				if (other.highY < box.lowY || box.highY < other.lowY) {
					continue;
				}
				// The pair in input order, as the crossing's rounding depends on it.
				const OccluderSegment&	  first	   = occluders[std::min(byLowX[position], byLowX[later])];
				const OccluderSegment&	  second   = occluders[std::max(byLowX[position], byLowX[later])];
				const SegmentIntersection crossing = intersectSegments(first.a, first.b, second.a, second.b);
				if (crossing.relation == SegmentRelation::ProperCrossing && crossing.point != observer && squaredDistance(observer, crossing.point) <= radiusSquared) {
					directions.push_back(crossing.point - observer);
				}
			}
		}

		std::vector<double> angles;
		sortDirections(directions, angles);
		const DirectionLess less;

		// The occluders each direction's ray can meet: those whose cone holds it, and
		// every occluder whose line passes through the observer. Any other is disjoint
		// from the ray, so reading only these finds the same hits. Kept as one list per
		// direction, back to back: direction i's list runs from reachable[listStart[i]]
		// up to reachable[listStart[i + 1]].
		struct DirectionSpans {
			std::array<std::size_t, 2> first{};
			std::array<std::size_t, 2> end{};
			std::size_t				   count = 0;
		};
		std::vector<DirectionSpans> spans(occluders.size());
		std::vector<std::uint32_t>	listStart(directions.size() + 1, 0);
		for (std::size_t index = 0; index < occluders.size(); ++index) {
			const OccluderCone& cone  = cones[index];
			DirectionSpans&		piece = spans[index];
			if (cone.throughObserver) {
				piece.first[0] = 0;
				piece.end[0]   = directions.size();
				piece.count	   = 1;
			} else {
				const std::size_t first =
					partitionPoint(directions, angles, roughAngle(cone.low), [&](const Vec2i64& direction) { return angleLess(direction, cone.low); });
				const std::size_t end =
					partitionPoint(directions, angles, roughAngle(cone.high), [&](const Vec2i64& direction) { return !angleLess(cone.high, direction); });
				if (angleLess(cone.high, cone.low)) {
					piece = {{first, 0}, {directions.size(), end}, 2};
				} else {
					piece = {{first, 0}, {end, 0}, 1};
				}
			}
			for (std::size_t part = 0; part < piece.count; ++part) {
				for (std::size_t position = piece.first[part]; position < piece.end[part]; ++position) {
					++listStart[position + 1];
				}
			}
		}
		for (std::size_t position = 0; position < directions.size(); ++position) {
			listStart[position + 1] += listStart[position];
		}
		// Each list nearest occluder first: by how near any of it comes to the observer,
		// less 2 mm for a hit's snapping to the millimetre and the rounding here. A ray's
		// hit on an occluder is never nearer than that, so once it passes both sides'
		// nearest hits so far, the rest of the list cannot change them.
		std::vector<double>		   nearest(occluders.size());
		std::vector<std::uint32_t> byNearness(occluders.size());
		for (std::size_t index = 0; index < occluders.size(); ++index) {
			const double distance = distanceToSegment(Vec2i64{0, 0}, occluders[index].a - observer, occluders[index].b - observer);
			nearest[index]		  = distance - 2.0 - distance * 1e-9;
			byNearness[index]	  = static_cast<std::uint32_t>(index);
		}
		std::sort(byNearness.begin(), byNearness.end(), [&](std::uint32_t first, std::uint32_t second) {
			return nearest[first] < nearest[second] || (nearest[first] == nearest[second] && first < second);
		});
		std::vector<std::uint32_t> reachable(listStart.back());
		std::vector<std::uint32_t> listEnd(listStart.begin(), listStart.end() - 1);
		for (const std::uint32_t index : byNearness) {
			const DirectionSpans& piece = spans[index];
			for (std::size_t part = 0; part < piece.count; ++part) {
				for (std::size_t position = piece.first[part]; position < piece.end[part]; ++position) {
					reachable[listEnd[position]++] = index;
				}
			}
		}

		// Two boundary vertices per distinct direction, evaluated on each angular
		// side: the CW-side hit (the trailing boundary of the sector ending at this
		// ray) then the CCW-side hit (the leading boundary of the next sector).
		// Emitting them in that order, while walking directions CCW, lays the star
		// boundary down without any sort: at a grazed endpoint the CW side gives the
		// far field arriving into the corner and the CCW side gives the near corner
		// departing it (or vice versa), so the near/far transition is two adjacent
		// vertices, not two vertices fighting over one angle.
		result.reserve(directions.size() * 2);
		std::size_t rimSide = 0;
		for (std::size_t directionIndex = 0; directionIndex < directions.size(); ++directionIndex) {
			const Vec2i64& direction = directions[directionIndex];
			// Exact far point: an integer multiple of `direction` whose length exceeds the
			// sight radius, so the ray segment observer->farExact lies precisely on
			// the ray and reaches past the rim. The multiplier is an integer count
			// (the only float here, and it cannot move the ray off `direction`).
			const double directionLength = std::sqrt(static_cast<double>(direction.x) * direction.x + static_cast<double>(direction.y) * direction.y);
			std::int64_t scale			 = static_cast<std::int64_t>(static_cast<double>(sightRadiusMm) / directionLength) + 2;
			// Cap the multiplier so direction*scale (and observer+direction*scale) stay within
			// int64 -- Vec2i64::operator* is unchecked. In practice scale ~
			// radius/|direction| keeps the product near the radius; this only guards
			// pathological inputs and never trims a scale we actually need.
			const std::int64_t safeLimit		= (std::numeric_limits<std::int64_t>::max)() / 4;
			const std::int64_t largestComponent = std::max<std::int64_t>(1, std::max(std::llabs(direction.x), std::llabs(direction.y)));
			scale								= std::max<std::int64_t>(1, std::min(scale, safeLimit / largestComponent));
			const Vec2i64 farExact				= observer + direction * scale;

			// Where the ray leaves the sight polygon: the rim vertex in this exact
			// direction, or the snapped crossing with the rim side whose angular span
			// holds it. The directions rise in angle, so the side only moves forward.
			while (rimSide + 1 < rimDirections.size() && !angleLess(direction, rimDirections[rimSide + 1])) {
				++rimSide;
			}
			Vec2i64 rimPoint = rim[rimSide];
			if (!less.sameAngle(rimDirections[rimSide], direction)) {
				rimPoint = intersectSegments(observer, farExact, rim[rimSide], rim[(rimSide + 1) % rim.size()]).point;
			}

			// Occluder hits nearer than the rim override it; hits past it are ignored.
			NearestHit clockwise(observer, rimPoint);
			NearestHit counterClockwise = clockwise;
			for (std::uint32_t slot = listStart[directionIndex]; slot < listStart[directionIndex + 1]; ++slot) {
				const std::uint32_t index = reachable[slot];
				if (nearest[index] > std::max(clockwise.reach, counterClockwise.reach)) {
					break;
				}
				castBothSides(observer, direction, farExact, occluders[index], index, clockwise, counterClockwise);
			}
			if (clockwise.point != observer) {
				result.push_back(clockwise.point);
			}
			if (counterClockwise.point != clockwise.point && counterClockwise.point != observer) {
				result.push_back(counterClockwise.point);
			}
		}

		// Dedup consecutive coincident vertices and the wrap seam, then drop any
		// vertex at the observer. The direction walk already emits CCW.
		Ring ring;
		ring.reserve(result.size());
		for (const Vec2i64& vertex : result) {
			if (vertex == observer) {
				continue;
			}
			if (!ring.empty() && ring.back() == vertex) {
				continue;
			}
			ring.push_back(vertex);
		}
		if (ring.size() >= 2 && ring.front() == ring.back()) {
			ring.pop_back();
		}

		// Collapse exactly-collinear triples. Adjacent directions along one flat
		// blocker face emit a run of vertices that are world-collinear and so add no
		// shape; simplifyRing with epsilon 0 drops only those exact-collinear middles,
		// leaving the minimal star polygon.
		simplifyRing(ring, 0);
		return ring;
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
