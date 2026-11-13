#include "SightSkirt.h"

#include "../offset/WallOffset.h" // simplifyRing
#include "../predicates/Predicates.h"
#include "SightPolygon.h"
#include "Visibility.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

// Doubles here only pick a direction for an offset, which is then truncated and checked in
// integers, and a ray's length; both are + - x / and sqrt of integers, which round alike on every
// toolchain. Everything the result's shape depends on is exact or snapped to the millimetre.

namespace geometry {

	namespace {

		// One step of the moved boundary; `rim` when it is one of the shape's own sides on the rim.
		struct Step {
			Vec2i64 from;
			Vec2i64 to;
			bool	rim = false;
		};

		// A vertex of the result and what it lies inside: a step's index, a rim side as -2 - side,
		// or -1 for an end of either.
		struct Placed {
			Vec2i64		 point;
			std::int64_t source = -1;
		};

		constexpr std::int64_t kAtAnEnd = -1;

		// An integer vector along (x, y) no longer than `length`: the direction scaled and truncated,
		// then pulled in a millimetre at a time while longer.
		Vec2i64 alongAtMost(double x, double y, std::int64_t length) {
			const double size = std::sqrt(x * x + y * y);
			Vec2i64		 out{static_cast<std::int64_t>(x / size * static_cast<double>(length)), static_cast<std::int64_t>(y / size * static_cast<double>(length))};
			const Int128 limit = Int128::product(length, length);
			while (dot(out, out) > limit) {
				std::int64_t& larger = (out.x < 0 ? -out.x : out.x) >= (out.y < 0 ? -out.y : out.y) ? out.x : out.y;
				larger += larger > 0 ? -1 : 1;
			}
			return out;
		}

		// Angular order round the centre, ties broken by length and then coordinates, so the sort is
		// the same everywhere.
		bool directionBefore(const Vec2i64& first, const Vec2i64& second) {
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

		bool sameDirection(const Vec2i64& first, const Vec2i64& second) { return !angleLess(first, second) && !angleLess(second, first); }

	} // namespace

	Ring skirtedSightShape(const Ring& shape, const Vec2i64& centre, std::int64_t sightRadiusMm, std::int64_t skirtMm) {
		const std::size_t count = shape.size();
		if (count < 3 || skirtMm < 2 || sightRadiusMm <= 0) {
			return {};
		}
		Ring rim = sightPolygon(centre, sightRadiusMm);
		rim.erase(std::unique(rim.begin(), rim.end()), rim.end());
		if (rim.size() >= 2 && rim.front() == rim.back()) {
			rim.pop_back();
		}
		const std::size_t	 sides = rim.size();
		std::vector<Vec2i64> rimDirections;
		rimDirections.reserve(sides);
		for (const Vec2i64& vertex : rim) {
			rimDirections.push_back(vertex - centre);
		}
		// The rim side whose angular span holds `direction`; vertex 0 lies along +x, where angleLess
		// starts, so nothing comes before it.
		auto rimSideOf = [&](const Vec2i64& direction) {
			const auto after = std::upper_bound(rimDirections.begin(), rimDirections.end(), direction,
												[](const Vec2i64& first, const Vec2i64& second) { return angleLess(first, second); });
			return static_cast<std::size_t>(after - rimDirections.begin()) - 1;
		};
		auto onRimSide = [&](const Vec2i64& point, std::size_t side) { return withinDistanceOfSegment(point, rim[side], rim[(side + 1) % sides], 1); };

		// Each side of the shape off the rim moves out along its right-hand normal (the shape is
		// counter-clockwise) by just under the skirt, so a hit snapped to the millimetre beyond it
		// stays within the skirt.
		const std::int64_t	 reach = skirtMm - 1;
		std::vector<Vec2i64> offsets(count);
		std::vector<bool>	 onRim(count, false);
		for (std::size_t i = 0; i < count; ++i) {
			const Vec2i64&	  from	   = shape[i];
			const Vec2i64&	  to	   = shape[(i + 1) % count];
			const std::size_t fromSide = rimSideOf(from - centre);
			for (const std::size_t side : {fromSide, (fromSide + sides - 1) % sides, (fromSide + 1) % sides}) {
				onRim[i] = onRim[i] || (onRimSide(from, side) && onRimSide(to, side));
			}
			if (!onRim[i]) {
				const Vec2i64 along = to - from;
				offsets[i]			= alongAtMost(static_cast<double>(along.y), -static_cast<double>(along.x), reach);
			}
		}

		// The moved boundary: each moved side, joined to the next round a convex corner by a chord
		// (by two above 90 degrees of turn, through the outward bisector) and through the corner
		// itself at a reflex one.
		std::vector<Vec2i64> points;
		std::vector<bool>	 rimAfter; // the step from each point is a side of the shape on the rim
		auto				 push = [&](const Vec2i64& point, bool rimStep) {
			if (!points.empty() && points.back() == point) {
				rimAfter.back() = rimStep;
				return;
			}
			points.push_back(point);
			rimAfter.push_back(rimStep);
		};
		for (std::size_t i = 0; i < count; ++i) {
			const std::size_t before	= (i + count - 1) % count;
			const Vec2i64&	  corner	= shape[i];
			const Vec2i64&	  inOffset	= offsets[before];
			const Vec2i64&	  outOffset = offsets[i];
			push(corner + inOffset, false);
			if (cross(inOffset, outOffset).sign() > 0) {
				if (dot(inOffset, outOffset).sign() < 0) {
					const Vec2i64 inSide	= corner - shape[before];
					const Vec2i64 outSide	= shape[(i + 1) % count] - corner;
					const double  inLength	= std::sqrt(dot(inSide, inSide).toDouble());
					const double  outLength = std::sqrt(dot(outSide, outSide).toDouble());
					const Vec2i64 bisector	= alongAtMost(static_cast<double>(inSide.x) / inLength - static_cast<double>(outSide.x) / outLength,
														  static_cast<double>(inSide.y) / inLength - static_cast<double>(outSide.y) / outLength, reach);
					if (cross(inOffset, bisector).sign() > 0 && cross(bisector, outOffset).sign() > 0) {
						push(corner + bisector, false);
					}
				}
			} else {
				push(corner, false);
			}
			push(corner + outOffset, onRim[i]);
		}
		if (points.size() >= 2 && points.front() == points.back()) {
			points.pop_back();
			rimAfter.pop_back();
		}
		std::vector<Step> steps;
		steps.reserve(points.size());
		for (std::size_t i = 0; i < points.size(); ++i) {
			const Step	  step{points[i], points[(i + 1) % points.size()], rimAfter[i]};
			const Vec2i64 from = step.from - centre;
			const Vec2i64 to   = step.to - centre;
			if (from == Vec2i64{0, 0} || to == Vec2i64{0, 0} || (cross(from, to).sign() == 0 && dot(from, to).sign() < 0)) {
				return {};
			}
			steps.push_back(step);
		}

		// Steps by the rim sides their angular spans meet. A step along a ray from the centre has no
		// span: the boundary's farthest point on either side of the ray never lies on it.
		std::vector<std::vector<std::size_t>> stepsBySide(sides);
		for (std::size_t i = 0; i < steps.size(); ++i) {
			const Vec2i64 from = steps[i].from - centre;
			const Vec2i64 to   = steps[i].to - centre;
			const int	  turn = cross(from, to).sign();
			if (turn == 0) {
				continue;
			}
			const std::size_t last = rimSideOf(turn > 0 ? to : from);
			for (std::size_t side = rimSideOf(turn > 0 ? from : to);; side = (side + 1) % sides) {
				stepsBySide[side].push_back(i);
				if (side == last) {
					break;
				}
			}
		}

		// Sweep directions: the rim's vertices, the boundary's, and every crossing of two steps or of
		// a step and the rim. Between two neighbours no step ends and none crosses another or the
		// rim, so which is farthest does not change.
		std::vector<Vec2i64> directions = rimDirections;
		for (const Vec2i64& point : points) {
			directions.push_back(point - centre);
		}
		for (std::size_t side = 0; side < sides; ++side) {
			const std::vector<std::size_t>& near = stepsBySide[side];
			for (std::size_t first = 0; first < near.size(); ++first) {
				const Step&				  step		= steps[near[first]];
				const SegmentIntersection rimCross = intersectSegments(step.from, step.to, rim[side], rim[(side + 1) % sides]);
				if (rimCross.relation == SegmentRelation::ProperCrossing && rimCross.point != centre) {
					directions.push_back(rimCross.point - centre);
				}
				for (std::size_t second = first + 1; second < near.size(); ++second) {
					const Step&				  other	   = steps[near[second]];
					const SegmentIntersection crossing = intersectSegments(step.from, step.to, other.from, other.to);
					if (crossing.relation == SegmentRelation::ProperCrossing && crossing.point != centre) {
						directions.push_back(crossing.point - centre);
					}
				}
			}
		}
		std::sort(directions.begin(), directions.end(), directionBefore);
		directions.erase(std::unique(directions.begin(), directions.end(), sameDirection), directions.end());

		// Along each direction, just clockwise of it and just counter-clockwise, the boundary's
		// farthest point, held to the rim: past the rim by more than a millimetre it becomes the
		// rim's point there, or the shape's own side on the rim where that lies farther out.
		const std::int64_t extent	= sightRadiusMm + skirtMm + 2;
		const Int128	   rimSlack = Int128::product(2 * sightRadiusMm + 1, 1);
		std::vector<Placed> placed;
		placed.reserve(directions.size() * 2);
		for (const Vec2i64& direction : directions) {
			const double	   length  = std::sqrt(dot(direction, direction).toDouble());
			const std::int64_t scale   = static_cast<std::int64_t>(static_cast<double>(extent) / length) + 2;
			const Vec2i64	   farExact = centre + direction * scale;
			const std::size_t  side	   = rimSideOf(direction);
			const bool		   atRimVertex = sameDirection(rimDirections[side], direction);
			const Vec2i64	   rimPoint	   = atRimVertex ? rim[side] : intersectSegments(centre, farExact, rim[side], rim[(side + 1) % sides]).point;
			const Int128	   rimReach	   = dot(rimPoint - centre, rimPoint - centre);
			for (const int turn : {-1, 1}) {
				Placed best;
				Int128 bestReach   = Int128::product(0, 0);
				bool   found	   = false;
				Placed bestRim;
				Int128 bestRimReach = Int128::product(0, 0);
				bool   foundRim		= false;
				for (const std::size_t index : stepsBySide[side]) {
					const Step&				  step = steps[index];
					const SegmentIntersection hit  = intersectSegments(centre, farExact, step.from, step.to);
					Placed					  candidate;
					if (hit.relation == SegmentRelation::ProperCrossing) {
						candidate = {hit.point, static_cast<std::int64_t>(index)};
					} else if (hit.relation == SegmentRelation::EndpointTouch && (hit.point == step.from || hit.point == step.to)) {
						const Vec2i64& other = hit.point == step.from ? step.to : step.from;
						if (cross(direction, other - centre).sign() != turn) {
							continue;
						}
						candidate = {hit.point, kAtAnEnd};
					} else {
						continue;
					}
					const Int128 candidateReach = dot(candidate.point - centre, candidate.point - centre);
					if (!found || bestReach < candidateReach) {
						best	  = candidate;
						bestReach = candidateReach;
						found	  = true;
					}
					if (step.rim && (!foundRim || bestRimReach < candidateReach)) {
						bestRim		 = candidate;
						bestRimReach = candidateReach;
						foundRim	 = true;
					}
				}
				if (!found) {
					return {};
				}
				if (bestReach > rimReach + rimSlack) {
					best = foundRim && rimReach < bestRimReach ? bestRim : Placed{rimPoint, atRimVertex ? kAtAnEnd : -2 - static_cast<std::int64_t>(side)};
				}
				if (!placed.empty() && placed.back().point == best.point) {
					placed.back().source = placed.back().source == best.source ? best.source : kAtAnEnd;
					continue;
				}
				placed.push_back(best);
			}
		}
		if (placed.size() >= 2 && placed.front().point == placed.back().point) {
			placed.front().source = placed.front().source == placed.back().source ? placed.front().source : kAtAnEnd;
			placed.pop_back();
		}

		// A vertex inside a step or rim side whose neighbours both lie within a millimetre of it adds
		// nothing but snapping; it goes.
		Ring out;
		out.reserve(placed.size());
		for (std::size_t i = 0; i < placed.size(); ++i) {
			const Placed& vertex = placed[i];
			if (vertex.source != kAtAnEnd && !out.empty()) {
				const Vec2i64& from = vertex.source >= 0 ? steps[static_cast<std::size_t>(vertex.source)].from : rim[static_cast<std::size_t>(-2 - vertex.source)];
				const Vec2i64& to	= vertex.source >= 0 ? steps[static_cast<std::size_t>(vertex.source)].to
													 : rim[(static_cast<std::size_t>(-2 - vertex.source) + 1) % sides];
				const Vec2i64& next = i + 1 < placed.size() ? placed[i + 1].point : out.front();
				if (withinDistanceOfSegment(out.back(), from, to, 1) && withinDistanceOfSegment(next, from, to, 1)) {
					continue;
				}
			}
			out.push_back(vertex.point);
		}
		simplifyRing(out, 0);
		if (!simpleAround(out, centre)) {
			return {};
		}
		return out;
	}

} // namespace geometry
