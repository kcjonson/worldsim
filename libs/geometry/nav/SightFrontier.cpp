#include "SightFrontier.h"

#include "SightCover.h"

#include "../polygon/Polygon.h"
#include "../predicates/Predicates.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace geometry::nav {

	namespace {

		double crossOf(double firstX, double firstY, double secondX, double secondY) {
			return firstX * secondY - firstY * secondX;
		}

		// Narrows [first, last] to where from + t (to - from) > bound, or >= bound when `closed`.
		void narrow(double from, double to, double bound, bool closed, double& first, double& last) {
			const double change = to - from;
			if (change == 0.0) {
				if (closed ? from < bound : from <= bound) {
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

		// Inner points closer together than this are merged; the overlap absorbs it.
		constexpr double kInnerPointSpacingMm = 5.0;

	} // namespace

	SightFrontier sightShapeFrontier(const PatchableMesh& record, const std::vector<Vec2i64>& shape, const Vec2i64& centre, SightFrontierScratch& scratch,
									 std::vector<std::vector<Vec2i64>>& pieces) {
		pieces.clear();
		auto whole = [&] {
			pieces.push_back(shape);
			return SightFrontier::Whole;
		};
		if (record.locate(centre) < 0) {
			return whole();
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
		std::vector<std::uint8_t>&		 reached  = scratch.reached;
		std::vector<SightFrontierScratch::Contact>& contacts = scratch.contacts;
		reached.assign(count, 0);
		contacts.clear();
		// For the mesh edge from + t (to - from), t in [0, 1], and side i's triangle: whether the edge
		// touches the closed triangle (a contact, kept for the pieces) and whether it meets the core.
		auto measure = [&](std::size_t i, double fromX, double fromY, double toX, double toY) {
			if (cross(offsets[i], offsets[(i + 1) % count]).sign() <= 0) {
				return;
			}
			const double leftX	  = static_cast<double>(offsets[i].x);
			const double leftY	  = static_cast<double>(offsets[i].y);
			const double rightX	  = static_cast<double>(offsets[(i + 1) % count].x);
			const double rightY	  = static_cast<double>(offsets[(i + 1) % count].y);
			const double sideFrom = crossOf(rightX - leftX, rightY - leftY, fromX - leftX, fromY - leftY);
			const double sideTo	  = crossOf(rightX - leftX, rightY - leftY, toX - leftX, toY - leftY);
			double		 first	  = 0.0;
			double		 last	  = 1.0;
			narrow(crossOf(leftX, leftY, fromX, fromY), crossOf(leftX, leftY, toX, toY), 0.0, true, first, last);
			narrow(crossOf(fromX, fromY, rightX, rightY), crossOf(toX, toY, rightX, rightY), 0.0, true, first, last);
			narrow(sideFrom, sideTo, 0.0, true, first, last);
			if (first > last) {
				return;
			}
			contacts.push_back({i, fromX, fromY, toX, toY});
			if (!angleBefore(wedgeStart[i], wedgeEnd[i])) {
				return;
			}
			const double startX = static_cast<double>(wedgeStart[i].direction.x);
			const double startY = static_cast<double>(wedgeStart[i].direction.y);
			const double endX	= static_cast<double>(wedgeEnd[i].direction.x);
			const double endY	= static_cast<double>(wedgeEnd[i].direction.y);
			first				= 0.0;
			last				= 1.0;
			narrow(crossOf(startX, startY, fromX, fromY), crossOf(startX, startY, toX, toY), 0.0, false, first, last);
			narrow(crossOf(fromX, fromY, endX, endY), crossOf(toX, toY, endX, endY), 0.0, false, first, last);
			narrow(sideFrom, sideTo, tolerance * sideLengths[i], false, first, last);
			reached[i] = reached[i] != 0 || first < last ? 1 : 0;
		};

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
				return whole();
			}
			const int turn = cross(from, to).sign();
			if (turn == 0 && dot(from, to).sign() < 0) {
				return whole();
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
					measure(i, fromX, fromY, toX, toY);
				}
			}
		}

		std::size_t met = 0;
		for (std::size_t i = 0; i < count; ++i) {
			met += reached[i];
		}
		if (met == 0) {
			return SightFrontier::Covered;
		}
		if (met == count) {
			return whole();
		}
		std::stable_sort(contacts.begin(), contacts.end(), [](const SightFrontierScratch::Contact& first, const SightFrontierScratch::Contact& second) { return first.side < second.side; });

		// Split each met side's wedge at every contact's end inside it and wherever a contact crosses
		// the side's line or the tolerance line. Between two neighbouring rays no contact ends, and
		// contacts never cross, so the nearest contact along the middle ray is the nearest all the
		// way across: everything nearer the centre than it is on the record, and the wedge adds the
		// ground between it and the side, when that is deeper than the tolerance.
		std::vector<SightFrontierScratch::Wedge>& wedges = scratch.wedges;
		std::vector<std::array<double, 2>>& rays = scratch.rays;
		wedges.clear();
		std::size_t contactAt = 0;
		for (std::size_t i = 0; i < count; ++i) {
			const std::size_t contactsFrom = contactAt;
			while (contactAt < contacts.size() && contacts[contactAt].side == i) {
				++contactAt;
			}
			if (reached[i] == 0) {
				continue;
			}
			const double leftX	= static_cast<double>(offsets[i].x);
			const double leftY	= static_cast<double>(offsets[i].y);
			const double rightX = static_cast<double>(offsets[(i + 1) % count].x);
			const double rightY = static_cast<double>(offsets[(i + 1) % count].y);
			const double sideX	= rightX - leftX;
			const double sideY	= rightY - leftY;
			const double startX = static_cast<double>(wedgeStart[i].direction.x);
			const double startY = static_cast<double>(wedgeStart[i].direction.y);
			const double endX	= static_cast<double>(wedgeEnd[i].direction.x);
			const double endY	= static_cast<double>(wedgeEnd[i].direction.y);
			auto		 insideWedge = [&](double pointX, double pointY) {
				return crossOf(startX, startY, pointX, pointY) > 0.0 && crossOf(pointX, pointY, endX, endY) > 0.0;
			};
			rays.clear();
			for (std::size_t c = contactsFrom; c < contactAt; ++c) {
				const SightFrontierScratch::Contact& contact = contacts[c];
				for (const auto& [pointX, pointY] : {std::array<double, 2>{contact.fromX, contact.fromY}, std::array<double, 2>{contact.toX, contact.toY}}) {
					if (insideWedge(pointX, pointY)) {
						rays.push_back({pointX, pointY});
					}
				}
				const double depthFrom = crossOf(sideX, sideY, contact.fromX - leftX, contact.fromY - leftY);
				const double depthTo   = crossOf(sideX, sideY, contact.toX - leftX, contact.toY - leftY);
				for (const double bound : {0.0, tolerance * sideLengths[i]}) {
					if ((depthFrom - bound) * (depthTo - bound) < 0.0) {
						const double along	= (bound - depthFrom) / (depthTo - depthFrom);
						const double pointX = contact.fromX + along * (contact.toX - contact.fromX);
						const double pointY = contact.fromY + along * (contact.toY - contact.fromY);
						if (insideWedge(pointX, pointY)) {
							rays.push_back({pointX, pointY});
						}
					}
				}
			}
			std::sort(rays.begin(), rays.end(), [](const std::array<double, 2>& first, const std::array<double, 2>& second) {
				return crossOf(first[0], first[1], second[0], second[1]) > 0.0;
			});
			rays.insert(rays.begin(), {startX, startY});
			rays.push_back({endX, endY});
			// Where a ray meets a line through `point` along `along`, as a distance along the ray's unit
			// direction; 0 when it does not meet it ahead.
			auto distanceAlong = [](double rayX, double rayY, double pointX, double pointY, double alongX, double alongY) {
				const double length	 = std::hypot(rayX, rayY);
				const double turning = crossOf(rayX / length, rayY / length, alongX, alongY);
				const double reachAlong = turning != 0.0 ? crossOf(pointX, pointY, alongX, alongY) / turning : 0.0;
				return std::isfinite(reachAlong) && reachAlong > 0.0 ? reachAlong : 0.0;
			};
			for (std::size_t r = 0; r + 1 < rays.size(); ++r) {
				const std::array<double, 2>& from = rays[r];
				const std::array<double, 2>& to	  = rays[r + 1];
				if (crossOf(from[0], from[1], to[0], to[1]) <= 0.0) {
					continue;
				}
				const double fromLength = std::hypot(from[0], from[1]);
				const double toLength	= std::hypot(to[0], to[1]);
				const double middleX	= from[0] / fromLength + to[0] / toLength;
				const double middleY	= from[1] / fromLength + to[1] / toLength;
				double		 nearestAt	   = std::numeric_limits<double>::infinity();
				std::size_t	 nearestContact = contactAt;
				for (std::size_t c = contactsFrom; c < contactAt; ++c) {
					const SightFrontierScratch::Contact& contact = contacts[c];
					const double			 alongX	 = contact.toX - contact.fromX;
					const double			 alongY	 = contact.toY - contact.fromY;
					const double			 turning = crossOf(middleX, middleY, alongX, alongY);
					if (turning == 0.0) {
						continue;
					}
					const double rayParameter  = crossOf(contact.fromX, contact.fromY, alongX, alongY) / turning;
					const double edgeParameter = crossOf(contact.fromX, contact.fromY, middleX, middleY) / turning;
					const double length		   = rayParameter * std::hypot(middleX, middleY);
					if (rayParameter > 0.0 && edgeParameter >= 0.0 && edgeParameter <= 1.0 && length < nearestAt) {
						nearestAt	   = length;
						nearestContact = c;
					}
				}
				const double middleLength = std::hypot(middleX, middleY);
				const bool	 adds		  = nearestContact < contactAt &&
								  crossOf(sideX, sideY, middleX / middleLength * nearestAt - leftX, middleY / middleLength * nearestAt - leftY) > tolerance * sideLengths[i];
				if (!adds) {
					continue;
				}
				const SightFrontierScratch::Contact& contact = contacts[nearestContact];
				const double			 alongX	 = contact.toX - contact.fromX;
				const double			 alongY	 = contact.toY - contact.fromY;
				SightFrontierScratch::Wedge			 wedge;
				wedge.side		  = i;
				wedge.fromX		  = from[0];
				wedge.fromY		  = from[1];
				wedge.toX		  = to[0];
				wedge.toY		  = to[1];
				wedge.fromVertex  = r == 0 && wedgeStart[i].direction == offsets[i];
				wedge.toVertex	  = r + 2 == rays.size() && wedgeEnd[i].direction == offsets[(i + 1) % count];
				wedge.innerFrom	  = distanceAlong(from[0], from[1], contact.fromX, contact.fromY, alongX, alongY);
				wedge.innerTo	  = distanceAlong(to[0], to[1], contact.fromX, contact.fromY, alongX, alongY);
				wedge.outerFrom	  = distanceAlong(from[0], from[1], leftX, leftY, sideX, sideY);
				wedge.outerTo	  = distanceAlong(to[0], to[1], leftX, leftY, sideX, sideY);
				wedges.push_back(wedge);
			}
		}
		if (wedges.empty()) {
			return SightFrontier::Covered;
		}

		// Runs of wedges that share a ray become pieces: the shape's side over the run, then back
		// along the run's rays at the nearest contact beside each, less the overlap. A ray between
		// two sides joins them only at the shape's own vertex.
		auto sharesRay = [&](const SightFrontierScratch::Wedge& before, const SightFrontierScratch::Wedge& after) {
			if (after.side == before.side) {
				return before.toX == after.fromX && before.toY == after.fromY;
			}
			return after.side == (before.side + 1) % count && before.toVertex && after.fromVertex;
		};
		std::size_t first = 0;
		if (sharesRay(wedges.back(), wedges.front())) {
			first = wedges.size();
			for (std::size_t w = 1; w < wedges.size(); ++w) {
				if (!sharesRay(wedges[w - 1], wedges[w])) {
					first = w;
					break;
				}
			}
			if (first == wedges.size()) {
				return whole();
			}
		}
		const double overlap = static_cast<double>(kSeenPieceOverlapMm);
		auto		 pointAt = [&](double rayX, double rayY, double distance) {
			const double length = std::hypot(rayX, rayY);
			return Vec2i64{centre.x + std::llround(rayX / length * distance), centre.y + std::llround(rayY / length * distance)};
		};
		// Where a ray from the centre first meets the shape's boundary: the side and the distance.
		auto boundaryHit = [&](double rayX, double rayY, std::size_t& side, double& distance) {
			const double length = std::hypot(rayX, rayY);
			distance			= std::numeric_limits<double>::infinity();
			for (std::size_t j = 0; j < count; ++j) {
				const double fromX	 = static_cast<double>(offsets[j].x);
				const double fromY	 = static_cast<double>(offsets[j].y);
				const double alongX	 = static_cast<double>(offsets[(j + 1) % count].x) - fromX;
				const double alongY	 = static_cast<double>(offsets[(j + 1) % count].y) - fromY;
				const double turning = crossOf(rayX / length, rayY / length, alongX, alongY);
				if (turning == 0.0) {
					continue;
				}
				const double reachAlong = crossOf(fromX, fromY, alongX, alongY) / turning;
				const double onSide		= crossOf(fromX, fromY, rayX / length, rayY / length) / turning;
				if (reachAlong > 0.0 && onSide >= 0.0 && onSide <= 1.0 && reachAlong < distance) {
					distance = reachAlong;
					side	 = j;
				}
			}
			return std::isfinite(distance);
		};
		// The nearest contact along a ray from the centre, any side's; infinite when none.
		auto nearestContactAlong = [&](double unitX, double unitY) {
			double nearestAt = std::numeric_limits<double>::infinity();
			for (const SightFrontierScratch::Contact& contact : contacts) {
				const double alongX	 = contact.toX - contact.fromX;
				const double alongY	 = contact.toY - contact.fromY;
				const double turning = crossOf(unitX, unitY, alongX, alongY);
				if (turning == 0.0) {
					continue;
				}
				const double reachAlong = crossOf(contact.fromX, contact.fromY, alongX, alongY) / turning;
				const double onEdge		= crossOf(contact.fromX, contact.fromY, unitX, unitY) / turning;
				if (reachAlong > 0.0 && onEdge >= 0.0 && onEdge <= 1.0) {
					nearestAt = std::min(nearestAt, reachAlong);
				}
			}
			return nearestAt;
		};
		auto apartBy = [](const Vec2i64& first, const Vec2i64& second, double spacing) {
			const Vec2i64 gap = first - second;
			return static_cast<double>(gap.x) * static_cast<double>(gap.x) + static_cast<double>(gap.y) * static_cast<double>(gap.y) > spacing * spacing;
		};
		std::vector<SightFrontierScratch::Ray>& runRays = scratch.runRays;
		std::vector<Vec2i64>&	   piece  = scratch.piece;
		std::vector<std::uint8_t>& pinned = scratch.pinned; // per piece vertex: one of the shape's own
		std::vector<Vec2i64>&	   inner  = scratch.inner;
		for (std::size_t done = 0; done < wedges.size();) {
			const std::size_t runStart	= (first + done) % wedges.size();
			std::size_t		  runLength = 1;
			while (done + runLength < wedges.size() && sharesRay(wedges[(runStart + runLength - 1) % wedges.size()], wedges[(runStart + runLength) % wedges.size()])) {
				++runLength;
			}
			const SightFrontierScratch::Wedge& runFirst = wedges[runStart];
			const SightFrontierScratch::Wedge& runLast	= wedges[(runStart + runLength - 1) % wedges.size()];
			double				   reachOut = 0.0;
			for (std::size_t k = 0; k < runLength; ++k) {
				const SightFrontierScratch::Wedge& wedge = wedges[(runStart + k) % wedges.size()];
				reachOut					 = std::max({reachOut, wedge.outerFrom, wedge.outerTo});
			}
			// The run's two end rays turn outward by the overlap at its farthest reach (a slope, not an
			// angle, so no trigonometry runs), so the piece overlaps the record beside the run too
			// rather than abutting it along a ray. What it takes in there is shape the record holds.
			// A turned ray must still meet the boundary on a side at or before the run's first (or at
			// or after its last); where turning breaks that (a run round most of the shape), the run's
			// own rays serve.
			const std::size_t runSides	 = (runLast.side + count - runFirst.side) % count;
			double			  startX	 = runFirst.fromX;
			double			  startY	 = runFirst.fromY;
			double			  endX		 = runLast.toX;
			double			  endY		 = runLast.toY;
			std::size_t		  startSide	 = 0;
			std::size_t		  endSide	 = 0;
			double			  startReach = 0.0;
			double			  endReach	 = 0.0;
			bool			  found		 = false;
			for (const double slope : {overlap / std::max(reachOut, overlap), 0.0}) {
				startX = runFirst.fromX + slope * runFirst.fromY;
				startY = runFirst.fromY - slope * runFirst.fromX;
				endX   = runLast.toX - slope * runLast.toY;
				endY   = runLast.toY + slope * runLast.toX;
				found  = boundaryHit(startX, startY, startSide, startReach) && boundaryHit(endX, endY, endSide, endReach) &&
						(runFirst.side + count - startSide) % count + runSides + (endSide + count - runLast.side) % count == (endSide + count - startSide) % count;
				if (found) {
					break;
				}
			}
			if (!found) {
				pieces.clear();
				return whole();
			}
			// The outer chain is the shape's own boundary between the turned rays, a millimetre short
			// of it where they cross a side.
			piece.clear();
			pinned.clear();
			piece.push_back(pointAt(startX, startY, startReach - 1.0));
			pinned.push_back(0);
			for (std::size_t j = (startSide + 1) % count; j != (endSide + 1) % count; j = (j + 1) % count) {
				if (piece.back() != shape[j]) {
					piece.push_back(shape[j]);
					pinned.push_back(1);
				}
			}
			const Vec2i64 outerEnd = pointAt(endX, endY, endReach - 1.0);
			if (piece.back() != outerEnd) {
				piece.push_back(outerEnd);
				pinned.push_back(0);
			}

			// The inner chain stands the overlap back from every contact point near it, along the run's
			// rays and the two turned ones: where the record's edge runs nearly along the rays, a pull
			// along the ray alone would leave the piece abutting it. So a few millimetres either way
			// do not matter on it: points closer together than kInnerPointSpacingMm are merged,
			// keeping the nearer, and points within the overlap of the centre become the centre. A run
			// under the overlap wide is fanned from the centre.
			runRays.clear();
			for (std::size_t k = 0; k <= runLength; ++k) {
				const SightFrontierScratch::Wedge* before = k > 0 ? &wedges[(runStart + k - 1) % wedges.size()] : nullptr;
				const SightFrontierScratch::Wedge* after  = k < runLength ? &wedges[(runStart + k) % wedges.size()] : nullptr;
				const double		   rayX	  = after != nullptr ? after->fromX : before->toX;
				const double		   rayY	  = after != nullptr ? after->fromY : before->toY;
				const double		   length = std::hypot(rayX, rayY);
				SightFrontierScratch::Ray		   ray;
				ray.unitX  = rayX / length;
				ray.unitY  = rayY / length;
				ray.outer  = std::numeric_limits<double>::infinity();
				ray.nearer = std::numeric_limits<double>::infinity();
				if (before != nullptr) {
					ray.outer  = std::min(ray.outer, before->outerTo);
					ray.nearer = std::min(ray.nearer, before->innerTo);
				}
				if (after != nullptr) {
					ray.outer  = std::min(ray.outer, after->outerFrom);
					ray.nearer = std::min(ray.nearer, after->innerFrom);
				}
				ray.nearer = std::min(ray.nearer, ray.outer);
				runRays.push_back(ray);
			}
			const double			turn   = crossOf(runFirst.fromX, runFirst.fromY, runLast.toX, runLast.toY) /
									 (std::hypot(runFirst.fromX, runFirst.fromY) * std::hypot(runLast.toX, runLast.toY));
			const bool				sliver = turn * reachOut < overlap;
			const SightFrontierScratch::Ray		firstRay = runRays.front();
			const SightFrontierScratch::Ray		lastRay	 = runRays.back();
			const double			startLength = std::hypot(startX, startY);
			const double			endLength	= std::hypot(endX, endY);
			runRays.insert(runRays.begin(), SightFrontierScratch::Ray{startX / startLength, startY / startLength, count, startReach,
														  std::min({firstRay.nearer, startReach, nearestContactAlong(startX / startLength, startY / startLength)})});
			runRays.push_back(SightFrontierScratch::Ray{endX / endLength, endY / endLength, count, endReach, std::min({lastRay.nearer, endReach, nearestContactAlong(endX / endLength, endY / endLength)})});
			inner.clear();
			for (std::size_t k = 0; k < runRays.size() && !sliver; ++k) {
				const SightFrontierScratch::Ray& ray   = runRays[k];
				double				 depth = ray.nearer - overlap;
				for (const SightFrontierScratch::Ray& other : runRays) {
					const double sideways = std::abs(crossOf(ray.unitX, ray.unitY, other.unitX, other.unitY)) * other.nearer;
					const double along	  = (ray.unitX * other.unitX + ray.unitY * other.unitY) * other.nearer;
					if (along > 0.0 && sideways < overlap) {
						depth = std::min(depth, along - std::sqrt(overlap * overlap - sideways * sideways));
					}
				}
				const Vec2i64 innerPoint = depth >= overlap ? pointAt(ray.unitX, ray.unitY, depth) : centre;
				bool		  keep		 = true;
				while (keep && innerPoint != centre && !inner.empty() && inner.back() != centre) {
					const Vec2i64 lastPoint = inner.back() - centre;
					const Vec2i64 next		= innerPoint - centre;
					if (cross(lastPoint, next).sign() > 0 && apartBy(inner.back(), innerPoint, kInnerPointSpacingMm)) {
						break;
					}
					if (dot(next, next) < dot(lastPoint, lastPoint)) {
						inner.pop_back();
					} else {
						keep = false;
					}
				}
				if (keep && (inner.empty() || inner.back() != innerPoint)) {
					inner.push_back(innerPoint);
				}
			}
			if (inner.empty()) {
				inner.push_back(centre);
			}
			// Back to the centre more than once would pinch the piece there; everything between is
			// nearer the centre than the record's edge, so it goes too.
			const auto firstCentre = std::find(inner.begin(), inner.end(), centre);
			const auto lastCentre  = std::find(inner.rbegin(), inner.rend(), centre).base();
			if (firstCentre != inner.end() && firstCentre + 1 < lastCentre) {
				inner.erase(firstCentre + 1, lastCentre);
			}
			for (std::size_t k = inner.size(); k-- > 0;) {
				if (piece.back() != inner[k]) {
					piece.push_back(inner[k]);
					pinned.push_back(0);
				}
			}
			if (piece.size() >= 2 && piece.front() == piece.back()) {
				piece.pop_back();
				pinned.pop_back();
			}
			// No side shorter than the patch's snap reach: of two vertices that close, one that is not
			// the shape's own goes (an end ray's crossing beside a corner, or an inner point).
			for (bool merged = true; merged && piece.size() > 3;) {
				merged = false;
				for (std::size_t i = 0; i < piece.size(); ++i) {
					const std::size_t next = (i + 1) % piece.size();
					if (apartBy(piece[i], piece[next], static_cast<double>(kOffGroundReachMm))) {
						continue;
					}
					const std::size_t drop = pinned[next] == 0 ? next : (pinned[i] == 0 ? i : piece.size());
					if (drop < piece.size()) {
						piece.erase(piece.begin() + static_cast<std::ptrdiff_t>(drop));
						pinned.erase(pinned.begin() + static_cast<std::ptrdiff_t>(drop));
						merged = true;
						break;
					}
				}
			}
			if (piece.size() < 3 || !isSimple(piece).pass) {
				pieces.clear();
				return whole();
			}
			pieces.push_back(piece);
			done += runLength;
		}
		return pieces.empty() ? SightFrontier::Covered : SightFrontier::Pieces;
	}

} // namespace geometry::nav
