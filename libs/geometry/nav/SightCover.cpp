#include "SightCover.h"

#include "../predicates/Predicates.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

// Doubles here are differences of integer millimetres a few tens of metres across, so every
// product is exact and the rest is + - x / and sqrt, which round alike on every toolchain.

namespace geometry::nav {

	namespace {

		double crossOf(double firstX, double firstY, double secondX, double secondY) {
			return firstX * secondY - firstY * secondX;
		}

		// Narrows [first, last] to where from + t (to - from) > bound.
		void narrow(double from, double to, double bound, double& first, double& last) {
			const double change = to - from;
			if (change == 0.0) {
				if (from <= bound) {
					first = 1.0;
					last  = 0.0;
				}
				return;
			}
			const double at = (bound - from) / change;
			if (change > 0.0) {
				first = std::max(first, at);
			} else {
				last = std::min(last, at);
			}
		}

	} // namespace

	bool sightShapeCovered(const PatchableMesh& record, const std::vector<Vec2i64>& shape, const Vec2i64& centre, SightCoverScratch& scratch) {
		scratch.readRings = false;
		if (record.locate(centre) < 0) {
			return false;
		}
		const std::size_t			 count		 = shape.size();
		std::vector<Vec2i64>&		 offsets	 = scratch.offsets;
		std::vector<double>&		 sideLengths = scratch.sideLengths;
		std::vector<UnwrappedAngle>& angles		 = scratch.angles;
		std::vector<UnwrappedAngle>& highest	 = scratch.highestSoFar;
		std::vector<UnwrappedAngle>& lowest		 = scratch.lowestFromHere;
		unwrapAngles(shape, centre, angles);
		offsets.clear();
		sideLengths.clear();
		highest.clear();
		lowest.assign(count, UnwrappedAngle{});
		Vec2i64 boxLow			 = shape.front();
		Vec2i64 boxHigh			 = boxLow;
		double	reachSquared = 0.0;
		for (std::size_t i = 0; i < count; ++i) {
			const Vec2i64& vertex = shape[i];
			const Vec2i64  offset = vertex - centre;
			offsets.push_back(offset);
			boxLow			 = {std::min(boxLow.x, vertex.x), std::min(boxLow.y, vertex.y)};
			boxHigh			 = {std::max(boxHigh.x, vertex.x), std::max(boxHigh.y, vertex.y)};
			reachSquared = std::max(reachSquared, static_cast<double>(offset.x) * static_cast<double>(offset.x) + static_cast<double>(offset.y) * static_cast<double>(offset.y));
			const Vec2i64 side = shape[(i + 1) % count] - vertex;
			sideLengths.push_back(std::sqrt(static_cast<double>(side.x) * static_cast<double>(side.x) + static_cast<double>(side.y) * static_cast<double>(side.y)));
			const UnwrappedAngle& high = angleBefore(angles[i], angles[i + 1]) ? angles[i + 1] : angles[i];
			highest.push_back(i == 0 || angleBefore(highest.back(), high) ? high : highest.back());
		}
		for (std::size_t i = count; i-- > 0;) {
			const UnwrappedAngle& low = angleBefore(angles[i + 1], angles[i]) ? angles[i + 1] : angles[i];
			lowest[i]				  = i + 1 == count || angleBefore(low, lowest[i + 1]) ? low : lowest[i + 1];
		}
		// Side i's wedge runs from the highest vertex angle up to vertex i to the lowest from vertex
		// i + 1 on, every vertex also counted a turn back and a turn on.
		std::vector<UnwrappedAngle>& wedgeStart = scratch.wedgeStart;
		std::vector<UnwrappedAngle>& wedgeEnd	= scratch.wedgeEnd;
		wedgeStart.assign(count, UnwrappedAngle{});
		wedgeEnd.assign(count, UnwrappedAngle{});
		UnwrappedAngle running{angles.front().turns - 1, angles.front().direction};
		for (std::size_t i = 0; i < count; ++i) {
			const UnwrappedAngle turnBack{angles[i].turns - 1, angles[i].direction};
			running = angleBefore(running, turnBack) ? turnBack : running;
		}
		for (std::size_t i = 0; i < count; ++i) {
			running		  = angleBefore(running, angles[i]) ? angles[i] : running;
			wedgeStart[i] = running;
		}
		running = UnwrappedAngle{angles[1].turns + 1, angles[1].direction};
		for (std::size_t i = 1; i <= count; ++i) {
			const UnwrappedAngle turnOn{angles[i].turns + 1, angles[i].direction};
			running = angleBefore(turnOn, running) ? turnOn : running;
		}
		for (std::size_t i = count; i-- > 0;) {
			running		= angleBefore(angles[i + 1], running) ? angles[i + 1] : running;
			wedgeEnd[i] = running;
		}

		const double tolerance = static_cast<double>(kSeenCoverToleranceMm);
		const double reach	   = std::sqrt(reachSquared);
		// Whether the mesh edge from + t (to - from), t in [0, 1], meets the core of side i's
		// triangle, its wedge narrowed by `margin` millimetres on either side.
		auto meetsCore = [&](std::size_t i, double fromX, double fromY, double toX, double toY, double margin) {
			if (cross(offsets[i], offsets[(i + 1) % count]).sign() <= 0 || !angleBefore(wedgeStart[i], wedgeEnd[i])) {
				return false;
			}
			const double leftX	= static_cast<double>(offsets[i].x);
			const double leftY	= static_cast<double>(offsets[i].y);
			const double rightX = static_cast<double>(offsets[(i + 1) % count].x);
			const double rightY = static_cast<double>(offsets[(i + 1) % count].y);
			const double startX = static_cast<double>(wedgeStart[i].direction.x);
			const double startY = static_cast<double>(wedgeStart[i].direction.y);
			const double endX	= static_cast<double>(wedgeEnd[i].direction.x);
			const double endY	= static_cast<double>(wedgeEnd[i].direction.y);
			double		 first	= 0.0;
			double		 last	= 1.0;
			narrow(crossOf(startX, startY, fromX, fromY), crossOf(startX, startY, toX, toY), margin * std::sqrt(startX * startX + startY * startY), first, last);
			narrow(crossOf(fromX, fromY, endX, endY), crossOf(toX, toY, endX, endY), margin * std::sqrt(endX * endX + endY * endY), first, last);
			narrow(crossOf(rightX - leftX, rightY - leftY, fromX - leftX, fromY - leftY), crossOf(rightX - leftX, rightY - leftY, toX - leftX, toY - leftY),
				   tolerance * sideLengths[i], first, last);
			return first < last;
		};

		// Set when the mesh edge meets a core only within the tolerance of a ray from the centre.
		bool nearRay = false;
		record.meshEdgesNear(boxLow, boxHigh, scratch.edgeIds, scratch.meshEdges);
		for (const auto& [edgeStart, edgeEnd] : scratch.meshEdges) {
			if (std::max(edgeStart.x, edgeEnd.x) < boxLow.x || boxHigh.x < std::min(edgeStart.x, edgeEnd.x) || std::max(edgeStart.y, edgeEnd.y) < boxLow.y ||
				boxHigh.y < std::min(edgeStart.y, edgeEnd.y)) {
				continue;
			}
			if (closestOnSegment(centre, edgeStart, edgeEnd).distanceMm > reach) {
				continue;
			}
			Vec2i64 from = edgeStart - centre;
			Vec2i64 to	 = edgeEnd - centre;
			if (from == Vec2i64{0, 0} || to == Vec2i64{0, 0}) {
				return false;
			}
			const int turn = cross(from, to).sign();
			if (turn == 0 && dot(from, to).sign() < 0) {
				return false;
			}
			if (turn < 0) {
				std::swap(from, to);
			}
			// The edge's angular span, starting in the shape's first turn.
			const UnwrappedAngle start{angleLess(from, angles.front().direction) ? 1 : 0, from};
			const UnwrappedAngle end{start.turns + (angleLess(to, from) ? 1 : 0), to};
			const double		 fromX = static_cast<double>(from.x);
			const double		 fromY = static_cast<double>(from.y);
			const double		 toX   = static_cast<double>(to.x);
			const double		 toY   = static_cast<double>(to.y);
			for (const std::int64_t shift : {std::int64_t{-1}, std::int64_t{0}, std::int64_t{1}}) {
				const UnwrappedAngle low{start.turns + shift, start.direction};
				const UnwrappedAngle high{end.turns + shift, end.direction};
				// Every side before `first` ends before the span, and every side from `past` on
				// starts after it.
				const auto first = static_cast<std::size_t>(
					std::partition_point(highest.begin(), highest.end(), [&](const UnwrappedAngle& angle) { return angleBefore(angle, low); }) - highest.begin());
				const auto past = static_cast<std::size_t>(
					std::partition_point(lowest.begin(), lowest.end(), [&](const UnwrappedAngle& angle) { return !angleBefore(high, angle); }) - lowest.begin());
				for (std::size_t i = first; i < past; ++i) {
					if (meetsCore(i, fromX, fromY, toX, toY, tolerance)) {
						return false;
					}
					nearRay = nearRay || meetsCore(i, fromX, fromY, toX, toY, 0.0);
				}
			}
		}
		scratch.readRings = nearRay;
		return !nearRay || record.groundCovers(shape);
	}

} // namespace geometry::nav
