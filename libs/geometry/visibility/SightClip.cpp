#include "SightClip.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace geometry {

	namespace {

		// The convex ring turned to start at its vertex nearest +x counter-clockwise round the centre, so
		// the directions of its vertices from the centre rise in angleLess order.
		struct ConvexRound {
			Vec2i64				 centre;
			Ring				 vertices;
			std::vector<Vec2i64> directions;
			// The squared distance from the centre to the nearest side's line, a little short: a point
			// nearer the centre than this is inside without the search for its side.
			double				 innerReachSquared = 0.0;
			// The squared distance to the farthest vertex, a little long: a segment that keeps farther
			// from the centre than this misses the ring.
			double				 outerReachSquared = 0.0;

			// The segment keeps farther from the centre than every vertex of the ring.
			[[nodiscard]] bool beyond(const Vec2i64& from, const Vec2i64& to) const {
				const SegmentPoint nearest = closestOnSegment(centre, from, to);
				return nearest.distanceMm * nearest.distanceMm > outerReachSquared;
			}

			// The side whose angular span round the centre holds `direction`: side k runs from vertex k to
			// vertex k + 1, and the last side spans +x.
			[[nodiscard]] std::size_t sideOf(const Vec2i64& direction) const {
				const auto after = std::upper_bound(directions.begin(), directions.end(), direction,
													[](const Vec2i64& first, const Vec2i64& second) { return angleLess(first, second); });
				return after == directions.begin() ? directions.size() - 1 : static_cast<std::size_t>(after - directions.begin()) - 1;
			}

			[[nodiscard]] const Vec2i64& sideStart(std::size_t side) const { return vertices[side]; }
			[[nodiscard]] const Vec2i64& sideEnd(std::size_t side) const { return vertices[(side + 1) % vertices.size()]; }

			// Inside or on the boundary: on the inner side of the one side whose span holds the point.
			[[nodiscard]] bool holds(const Vec2i64& point) const {
				const double offsetX = static_cast<double>(point.x - centre.x);
				const double offsetY = static_cast<double>(point.y - centre.y);
				const double reachSquared = offsetX * offsetX + offsetY * offsetY;
				if (reachSquared < innerReachSquared) {
					return true;
				}
				if (reachSquared > outerReachSquared) {
					return false;
				}
				const std::size_t side = sideOf(point - centre);
				return orientation(sideStart(side), sideEnd(side), point) != Orientation::Clockwise;
			}

			// The convex vertices passed going from `from` (in side `fromSide`) to `to` (in `toSide`) round the
			// centre the short way: how many lie in the angular span (from, to], negative when the step runs
			// clockwise.
			[[nodiscard]] std::int64_t advance(const Vec2i64& from, std::size_t fromSide, const Vec2i64& to, std::size_t toSide) const {
				const int turn = cross(from - centre, to - centre).sign();
				if (turn == 0) {
					return 0;
				}
				const auto count = static_cast<std::int64_t>(vertices.size());
				const auto from64 = static_cast<std::int64_t>(fromSide);
				const auto to64	  = static_cast<std::int64_t>(toSide);
				return turn > 0 ? (to64 - from64 + count) % count : -((from64 - to64 + count) % count);
			}
		};

		bool roundOf(const Ring& convex, const Vec2i64& centre, ConvexRound& out) {
			out.centre = centre;
			out.vertices.clear();
			for (const Vec2i64& vertex : convex) {
				if (out.vertices.empty() || out.vertices.back() != vertex) {
					out.vertices.push_back(vertex);
				}
			}
			while (out.vertices.size() >= 2 && out.vertices.front() == out.vertices.back()) {
				out.vertices.pop_back();
			}
			const std::size_t count = out.vertices.size();
			if (count < 3) {
				return false;
			}
			for (std::size_t side = 0; side < count; ++side) {
				if (out.vertices[side] == centre || orientation(out.vertices[side], out.vertices[(side + 1) % count], centre) != Orientation::CounterClockwise) {
					return false;
				}
			}
			std::size_t first = 0;
			for (std::size_t k = 1; k < count; ++k) {
				if (angleLess(out.vertices[k] - centre, out.vertices[first] - centre)) {
					first = k;
				}
			}
			std::rotate(out.vertices.begin(), out.vertices.begin() + static_cast<std::ptrdiff_t>(first), out.vertices.end());
			out.directions.clear();
			for (const Vec2i64& vertex : out.vertices) {
				out.directions.push_back(vertex - centre);
			}
			for (std::size_t k = 1; k < count; ++k) {
				if (!angleLess(out.directions[k - 1], out.directions[k])) {
					return false;
				}
			}
			// Both reaches are off by a millionth and a square millimetre, far more than the doubles'
			// rounding, so the shortcuts never answer differently from the search.
			out.innerReachSquared = std::numeric_limits<double>::max();
			for (std::size_t side = 0; side < count; ++side) {
				const Vec2i64 along	 = out.vertices[(side + 1) % count] - out.vertices[side];
				const double  height = cross(along, centre - out.vertices[side]).toDouble();
				out.innerReachSquared = std::min(out.innerReachSquared, height * height / dot(along, along).toDouble());
			}
			out.innerReachSquared = out.innerReachSquared * (1.0 - 1e-6) - 1.0;
			out.outerReachSquared = 0.0;
			for (const Vec2i64& direction : out.directions) {
				out.outerReachSquared = std::max(out.outerReachSquared, dot(direction, direction).toDouble());
			}
			out.outerReachSquared = out.outerReachSquared * (1.0 + 1e-6) + 1.0;
			return true;
		}

		bool onSegment(const Vec2i64& point, const Vec2i64& from, const Vec2i64& to) {
			return orientation(from, to, point) == Orientation::Collinear && point.x >= std::min(from.x, to.x) && point.x <= std::max(from.x, to.x) &&
				   point.y >= std::min(from.y, to.y) && point.y <= std::max(from.y, to.y);
		}

		// One millimetre inward across the line (from, to), counter-clockwise inside, along the axis its
		// inward normal leans on most.
		void stepInward(Vec2i64& point, const Vec2i64& from, const Vec2i64& to) {
			const Vec2i64 inward{-(to.y - from.y), to.x - from.x};
			if ((inward.x < 0 ? -inward.x : inward.x) >= (inward.y < 0 ? -inward.y : inward.y)) {
				point.x += inward.x > 0 ? 1 : -1;
			} else {
				point.y += inward.y > 0 ? 1 : -1;
			}
		}

		// A rounded crossing of the star's edge and a side of the convex ring, moved a millimetre at a
		// time onto the inner side of both, so it lies in both rings' closed areas near the crossing.
		Vec2i64 insideBoth(Vec2i64 point, const Vec2i64& edgeFrom, const Vec2i64& edgeTo, const Vec2i64& sideFrom, const Vec2i64& sideTo) {
			for (int step = 0; step < 8; ++step) {
				if (orientation(edgeFrom, edgeTo, point) == Orientation::Clockwise) {
					stepInward(point, edgeFrom, edgeTo);
				} else if (orientation(sideFrom, sideTo, point) == Orientation::Clockwise) {
					stepInward(point, sideFrom, sideTo);
				} else {
					break;
				}
			}
			return point;
		}

		enum class Event { Inside, Outside, Entry, Exit };

		struct Placed {
			Vec2i64 point;
			Event	event = Event::Inside;
		};

	} // namespace

	Ring starClippedToConvex(const Ring& star, const Vec2i64& centre, const Ring& convex) {
		ConvexRound round;
		if (star.size() < 3 || !roundOf(convex, centre, round)) {
			return {};
		}
		const std::size_t count = star.size();
		std::vector<bool> held(count);
		bool			  anyHeld	 = false;
		bool			  anyOutside = false;
		for (std::size_t i = 0; i < count; ++i) {
			if (star[i] == centre) {
				return {};
			}
			held[i] = round.holds(star[i]);
			anyHeld = anyHeld || held[i];
			anyOutside = anyOutside || !held[i];
		}

		// Each edge's meetings with the boundary, from the sides its angular span covers: a segment
		// between two points inside a convex ring stays inside, one from inside to outside leaves once,
		// and one between two points outside passes through or misses.
		const std::size_t	 sides = round.vertices.size();
		std::vector<Placed>	 events;
		std::vector<Vec2i64> meetings;
		events.reserve(count + 8);
		for (std::size_t i = 0; i < count; ++i) {
			const Vec2i64& from = star[i];
			const Vec2i64& to	= star[(i + 1) % count];
			events.push_back({from, held[i] ? Event::Inside : Event::Outside});
			const bool toHeld = held[(i + 1) % count];
			if ((held[i] && toHeld) || (!held[i] && !toHeld && round.beyond(from, to))) {
				continue;
			}
			meetings.clear();
			const std::size_t fromSide = round.sideOf(from - centre);
			const std::size_t toSide   = round.sideOf(to - centre);
			const int		  turn	   = cross(from - centre, to - centre).sign();
			for (std::size_t side = fromSide;; side = turn >= 0 ? (side + 1) % sides : (side + sides - 1) % sides) {
				const Vec2i64&			  sideFrom = round.sideStart(side);
				const Vec2i64&			  sideTo   = round.sideEnd(side);
				const SegmentIntersection hit	   = intersectSegments(from, to, sideFrom, sideTo);
				switch (hit.relation) {
					case SegmentRelation::ProperCrossing:
						meetings.push_back(insideBoth(hit.point, from, to, sideFrom, sideTo));
						break;
					case SegmentRelation::EndpointTouch:
						for (const Vec2i64& end : {from, to}) {
							if (onSegment(end, sideFrom, sideTo)) {
								meetings.push_back(end);
							}
						}
						for (const Vec2i64& end : {sideFrom, sideTo}) {
							if (onSegment(end, from, to)) {
								meetings.push_back(end);
							}
						}
						break;
					case SegmentRelation::CollinearOverlap:
						meetings.push_back(hit.overlapStart);
						meetings.push_back(hit.overlapEnd);
						break;
					case SegmentRelation::Disjoint:
						break;
				}
				if (side == toSide || turn == 0) {
					break;
				}
			}
			if (turn == 0 && toSide != fromSide) {
				const SegmentIntersection hit = intersectSegments(from, to, round.sideStart(toSide), round.sideEnd(toSide));
				if (hit.relation == SegmentRelation::ProperCrossing) {
					meetings.push_back(insideBoth(hit.point, from, to, round.sideStart(toSide), round.sideEnd(toSide)));
				}
			}
			if (meetings.empty()) {
				if (held[i] != toHeld) {
					return {};
				}
				continue;
			}
			const Vec2i64 along = to - from;
			const auto	  byReach = [&](const Vec2i64& first, const Vec2i64& second) { return dot(first - from, along) < dot(second - from, along); };
			const Vec2i64 nearest  = *std::min_element(meetings.begin(), meetings.end(), byReach);
			const Vec2i64 farthest = *std::max_element(meetings.begin(), meetings.end(), byReach);
			if (held[i]) {
				events.push_back({farthest, Event::Exit});
			} else if (toHeld) {
				events.push_back({nearest, Event::Entry});
			} else if (nearest != farthest) {
				events.push_back({nearest, Event::Entry});
				events.push_back({farthest, Event::Exit});
			}
		}

		const auto firstExit = std::find_if(events.begin(), events.end(), [](const Placed& placed) { return placed.event == Event::Exit; });
		if (firstExit == events.end()) {
			if (!anyOutside) {
				return star;
			}
			return anyHeld ? Ring{} : round.vertices;
		}

		// From the first exit round: inside, the star's own vertices and crossings; outside, the
		// convex vertices the run passes, counted with their signs so a run that steps back a hair
		// adds none.
		Ring		 out;
		bool		 outside   = false;
		std::size_t	 exitSide  = 0;
		Vec2i64		 last;
		std::size_t	 lastSide  = 0;
		std::int64_t passed	   = 0;
		const auto	 start	   = static_cast<std::size_t>(firstExit - events.begin());
		for (std::size_t k = 0; k < events.size(); ++k) {
			const Placed& placed = events[(start + k) % events.size()];
			switch (placed.event) {
				case Event::Exit:
					if (outside) {
						return {};
					}
					out.push_back(placed.point);
					outside	 = true;
					exitSide = round.sideOf(placed.point - centre);
					last	 = placed.point;
					lastSide = exitSide;
					passed	 = 0;
					break;
				case Event::Outside:
					if (!outside) {
						return {};
					}
					{
						const std::size_t side = round.sideOf(placed.point - centre);
						passed += round.advance(last, lastSide, placed.point, side);
						last	 = placed.point;
						lastSide = side;
					}
					break;
				case Event::Entry:
					if (!outside) {
						return {};
					}
					passed += round.advance(last, lastSide, placed.point, round.sideOf(placed.point - centre));
					if (passed > static_cast<std::int64_t>(sides)) {
						return {};
					}
					for (std::int64_t step = 1; step <= passed; ++step) {
						out.push_back(round.vertices[(exitSide + static_cast<std::size_t>(step)) % sides]);
					}
					out.push_back(placed.point);
					outside = false;
					break;
				case Event::Inside:
					if (outside) {
						return {};
					}
					out.push_back(placed.point);
					break;
			}
		}
		if (outside) {
			return {};
		}

		Ring ring;
		ring.reserve(out.size());
		for (const Vec2i64& point : out) {
			if (ring.empty() || ring.back() != point) {
				ring.push_back(point);
			}
		}
		while (ring.size() >= 2 && ring.front() == ring.back()) {
			ring.pop_back();
		}
		return ring.size() >= 3 ? ring : Ring{};
	}

} // namespace geometry
