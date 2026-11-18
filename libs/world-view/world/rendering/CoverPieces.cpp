#include "CoverPieces.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>

namespace worldview {

	namespace {

		constexpr double kDegree = std::numbers::pi / 180.0;
		// The band's far arc is drawn in steps no wider than this, so its chords stay well past the far reach.
		constexpr double kBandArcStep = 30.0 * kDegree;

		glm::dvec2 toDouble(const geometry::Vec2i64& point) {
			return {static_cast<double>(point.x), static_cast<double>(point.y)};
		}

		double crossOf(glm::dvec2 first, glm::dvec2 second) {
			return first.x * second.y - first.y * second.x;
		}

		glm::dvec2 turned(glm::dvec2 direction, double angle) {
			const double cosine = std::cos(angle);
			const double sine = std::sin(angle);
			return {direction.x * cosine - direction.y * sine, direction.x * sine + direction.y * cosine};
		}

		glm::dvec2 leftOf(glm::dvec2 direction) {
			return {-direction.y, direction.x};
		}

		void appendTriangle(
			glm::dvec2					first,
			float						firstValue,
			glm::dvec2					second,
			float						secondValue,
			glm::dvec2					third,
			float						thirdValue,
			std::vector<ValueTriangle>& out
		) {
			out.push_back({.points = {first, second, third}, .values = {firstValue, secondValue, thirdValue}});
		}

		// A convex polygon with a linear value, its corners in reused storage.
		struct ClipPolygon {
			std::vector<glm::dvec2> points;
			std::vector<float>		values;
		};

		// The part of `polygon` on the left of the line from `from` along `along` into `kept`, its values
		// linear along each cut side, as Sutherland and Hodgman clip. Any number of corners.
		void clipLeftOf(const ClipPolygon& polygon, glm::dvec2 from, glm::dvec2 along, ClipPolygon& kept) {
			kept.points.clear();
			kept.values.clear();
			const std::size_t count = polygon.points.size();
			for (std::size_t corner = 0; corner < count; ++corner) {
				const std::size_t next = (corner + 1) % count;
				const double	  here = crossOf(along, polygon.points[corner] - from);
				const double	  there = crossOf(along, polygon.points[next] - from);
				if (here >= 0.0) {
					kept.points.push_back(polygon.points[corner]);
					kept.values.push_back(polygon.values[corner]);
				}
				if ((here >= 0.0) != (there >= 0.0)) {
					const double share = here / (here - there);
					kept.points.push_back(polygon.points[corner] + share * (polygon.points[next] - polygon.points[corner]));
					kept.values.push_back(
						static_cast<float>(
							static_cast<double>(polygon.values[corner]) +
							share * static_cast<double>(polygon.values[next] - polygon.values[corner])
						)
					);
				}
			}
		}

		void appendFan(const ClipPolygon& polygon, std::vector<ValueTriangle>& out) {
			for (std::size_t corner = 1; corner + 1 < polygon.points.size(); ++corner) {
				appendTriangle(
					polygon.points[0],
					polygon.values[0],
					polygon.points[corner],
					polygon.values[corner],
					polygon.points[corner + 1],
					polygon.values[corner + 1],
					out
				);
			}
		}

		// How far along `direction` (unit) from `eye` the ray first meets the outline, or none.
		std::optional<double> rangeToOutline(std::span<const geometry::Vec2i64> outline, glm::dvec2 eye, glm::dvec2 direction) {
			std::optional<double> nearest;
			for (std::size_t side = 0; side < outline.size(); ++side) {
				const glm::dvec2 from = toDouble(outline[side]) - eye;
				const glm::dvec2 along = toDouble(outline[(side + 1) % outline.size()]) - toDouble(outline[side]);
				const double	 facing = crossOf(direction, along);
				if (facing == 0.0) {
					continue;
				}
				const double range = crossOf(from, along) / facing;
				const double onSide = crossOf(from, direction) / facing;
				if (range > 0.0 && onSide >= 0.0 && onSide <= 1.0 && (!nearest.has_value() || range < *nearest)) {
					nearest = range;
				}
			}
			return nearest;
		}

		// The convex hull of the body's corners, counter-clockwise, into `hull` (`points` is scratch).
		void hullOf(const vision::SightBody& body, std::vector<glm::dvec2>& points, std::vector<glm::dvec2>& hull) {
			points.clear();
			for (const geometry::OccluderSegment& edge : body.edges) {
				points.push_back(toDouble(edge.a));
			}
			std::ranges::sort(points, [](glm::dvec2 first, glm::dvec2 second) {
				return first.x < second.x || (first.x == second.x && first.y < second.y);
			});
			points.erase(std::unique(points.begin(), points.end()), points.end());
			hull.assign(2 * points.size() + 1, glm::dvec2{0.0});
			if (points.size() < 3) {
				hull.assign(points.begin(), points.end());
				return;
			}
			std::size_t size = 0;
			for (const glm::dvec2 point : points) {
				while (size >= 2 && crossOf(hull[size - 1] - hull[size - 2], point - hull[size - 1]) <= 0.0) {
					--size;
				}
				hull[size++] = point;
			}
			const std::size_t lower = size + 1;
			for (auto it = points.rbegin() + 1; it != points.rend(); ++it) {
				while (size >= lower && crossOf(hull[size - 1] - hull[size - 2], *it - hull[size - 1]) <= 0.0) {
					--size;
				}
				hull[size++] = *it;
			}
			hull.resize(size - 1);
		}

	} // namespace

	std::vector<renderer::TessellatedMesh> valueMeshes(std::span<const ValueTriangle> pieces, glm::dvec2 anchorMm) {
		constexpr std::size_t				   kTrianglesPerMesh = 65535 / 3;
		std::vector<renderer::TessellatedMesh> meshes;
		for (std::size_t first = 0; first < pieces.size(); first += kTrianglesPerMesh) {
			const std::size_t		  count = std::min(kTrianglesPerMesh, pieces.size() - first);
			renderer::TessellatedMesh mesh;
			mesh.vertices.reserve(3 * count);
			mesh.indices.reserve(3 * count);
			mesh.colors.reserve(3 * count);
			for (std::size_t piece = first; piece < first + count; ++piece) {
				for (std::size_t corner = 0; corner < 3; ++corner) {
					const glm::dvec2 metres = (pieces[piece].points[corner] - anchorMm) / 1000.0;
					mesh.indices.push_back(static_cast<std::uint16_t>(mesh.vertices.size()));
					mesh.vertices.push_back({static_cast<float>(metres.x), static_cast<float>(metres.y)});
					mesh.colors.push_back({1.0F, 1.0F, 1.0F, pieces[piece].values[corner]});
				}
			}
			meshes.push_back(std::move(mesh));
		}
		return meshes;
	}

	void appendFeatherFan(glm::dvec2 corner, glm::dvec2 incoming, glm::dvec2 outgoing, double featherMm, std::vector<ValueTriangle>& out) {
		const double incomingLength = glm::length(incoming);
		const double outgoingLength = glm::length(outgoing);
		if (incomingLength == 0.0 || outgoingLength == 0.0) {
			return;
		}
		const glm::dvec2 arriving = incoming / incomingLength;
		const glm::dvec2 leaving = outgoing / outgoingLength;
		const double	 turn = crossOf(arriving, leaving);
		const double	 along = glm::dot(arriving, leaving);
		if (turn > 0.0 || (turn == 0.0 && along > 0.0)) {
			return;
		}
		// Clockwise from the arriving edge's inward normal to the leaving one's.
		const double	 sweep = std::atan2(-turn, along);
		const auto		 steps = static_cast<int>(std::ceil(sweep / (kFeatherFanStepDegrees * kDegree) - 1e-9));
		const glm::dvec2 start = leftOf(arriving);
		for (int step = 0; step < steps; ++step) {
			const glm::dvec2 from = turned(start, -sweep * static_cast<double>(step) / static_cast<double>(steps));
			const glm::dvec2 to = turned(start, -sweep * static_cast<double>(step + 1) / static_cast<double>(steps));
			appendTriangle(corner, 1.0F, corner + featherMm * from, 0.0F, corner + featherMm * to, 0.0F, out);
		}
	}

	void appendFeather(std::span<const DirectedEdge> edges, double featherMm, std::vector<ValueTriangle>& out) {
		std::multimap<geometry::Vec2i64, std::size_t> starting;
		for (std::size_t index = 0; index < edges.size(); ++index) {
			const glm::dvec2 start = toDouble(edges[index].first);
			const glm::dvec2 end = toDouble(edges[index].second);
			const glm::dvec2 along = end - start;
			const double	 length = glm::length(along);
			if (length == 0.0) {
				continue;
			}
			const glm::dvec2 inward = leftOf(along / length) * featherMm;
			appendTriangle(start, 1.0F, end, 1.0F, end + inward, 0.0F, out);
			appendTriangle(start, 1.0F, end + inward, 0.0F, start + inward, 0.0F, out);
			starting.emplace(edges[index].first, index);
		}
		for (const DirectedEdge& edge : edges) {
			const auto [first, last] = starting.equal_range(edge.second);
			for (auto next = first; next != last; ++next) {
				const DirectedEdge& leaving = edges[next->second];
				appendFeatherFan(
					toDouble(edge.second),
					toDouble(edge.second) - toDouble(edge.first),
					toDouble(leaving.second) - toDouble(leaving.first),
					featherMm,
					out
				);
			}
		}
	}

	std::optional<SoftShadow> appendSoftShadow(
		const vision::SightBody&		   body,
		glm::dvec2						   eyeMm,
		const vision::SightTunables&	   tunables,
		std::span<const geometry::Vec2i64> bright,
		std::vector<ValueTriangle>&		   out
	) {
		const std::optional<vision::Silhouette> silhouette = vision::silhouetteFrom(body, eyeMm);
		if (!silhouette.has_value()) {
			return std::nullopt;
		}
		const glm::dvec2 clockwiseCorner = toDouble(body.edges[silhouette->clockwise].a);
		const glm::dvec2 counterClockwiseCorner = toDouble(body.edges[silhouette->counterClockwise].a);
		const glm::dvec2 bisector =
			glm::normalize(glm::normalize(clockwiseCorner - eyeMm) + glm::normalize(counterClockwiseCorner - eyeMm));

		// The width across the bisector and the nearest point, in one pass over the outline (2.3).
		const glm::dvec2 across = leftOf(bisector);
		double			 least = std::numeric_limits<double>::max();
		double			 most = std::numeric_limits<double>::lowest();
		double			 nearestSquared = std::numeric_limits<double>::max();
		for (const geometry::OccluderSegment& edge : body.edges) {
			const glm::dvec2 from = toDouble(edge.a) - eyeMm;
			const glm::dvec2 along = toDouble(edge.b) - toDouble(edge.a);
			const double	 offset = glm::dot(from, across);
			least = std::min(least, offset);
			most = std::max(most, offset);
			const double	 length = glm::dot(along, along);
			const double	 share = length > 0.0 ? std::clamp(-glm::dot(from, along) / length, 0.0, 1.0) : 0.0;
			const glm::dvec2 nearest = from + share * along;
			nearestSquared = std::min(nearestSquared, glm::dot(nearest, nearest));
		}
		const double width = most - least;
		const double rayWidth = static_cast<double>(tunables.softShadowRayWidthMm);
		if (width <= 0.0 || width >= rayWidth) {
			return std::nullopt;
		}
		SoftShadow shadow;
		shadow.widthAsSeen = width;
		shadow.distance = std::sqrt(nearestSquared);
		shadow.footValue = static_cast<double>(tunables.softShadowStrengthPercent) / 100.0 * width / rayWidth *
						   static_cast<double>(body.arrival) / static_cast<double>(geometry::kFractionOne);
		shadow.shadowLength = shadow.distance * width / (rayWidth - width);
		const double nearRange = shadow.distance;
		const double tipRange = shadow.distance + shadow.shadowLength;
		// Where the bright rim cuts a triangle short, it may carry at most kSoftShadowRimMostValue there
		// (limit 3): a triangle that would carry more fades by the share that brings it down.
		if (const std::optional<double> rim = rangeToOutline(bright, eyeMm, bisector); rim.has_value() && *rim < tipRange) {
			const double atRim = shadow.footValue * (1.0 - std::max(0.0, *rim - nearRange) / shadow.shadowLength);
			if (atRim > kSoftShadowRimMostValue) {
				shadow.footValue *= kSoftShadowRimMostValue / atRim;
			}
		}
		if (shadow.footValue <= 0.0 || shadow.shadowLength <= 0.0) {
			return shadow;
		}

		const std::array<double, 2> endAngle = {
			std::atan2(crossOf(bisector, clockwiseCorner - eyeMm), glm::dot(bisector, clockwiseCorner - eyeMm)),
			std::atan2(crossOf(bisector, counterClockwiseCorner - eyeMm), glm::dot(bisector, counterClockwiseCorner - eyeMm)),
		};
		const std::array<double, 2> cornerRange = {glm::length(clockwiseCorner - eyeMm), glm::length(counterClockwiseCorner - eyeMm)};
		const auto					progressAt = [&](double range) {
			 return std::clamp((range - nearRange) / shadow.shadowLength, 0.0, 1.0);
		};
		const auto valueAt = [&](double range) {
			return shadow.footValue * (1.0 - progressAt(range));
		};
		const double halfRay = static_cast<double>(tunables.shadowEdgeWidthMm) / 2.0;
		const double tolerance = kDrawnValueTolerancePercent / 100.0;

		// Slices along the length (7.3). A side at range r lies r x angle x (1 - progress) across the
		// bisector, a parabola whose chord over h strays angle x h^2 / (4 x shadowLength) from it. Where the
		// side is hard (before its fin starts) the stray is held to kDrawnShadowSliceToleranceMm; past it
		// the fin carries the value from the side to 0 across its width, so a stray s moves the value at a
		// point by at most value x s / finWidth, and the slice may run until that reaches
		// kDrawnValueTolerancePercent. A triangle fainter than the tolerance needs no slice for either.
		const double widest = std::max(std::abs(endAngle[0]), std::abs(endAngle[1]));
		const auto	 allowedStray = [&](double range) {
			  const double value = valueAt(range);
			  if (value <= tolerance) {
				  return std::numeric_limits<double>::infinity();
			  }
			  double allowed = std::numeric_limits<double>::infinity();
			  for (std::size_t side = 0; side < 2; ++side) {
				  const double fin = range > cornerRange[side] ? halfRay * (range - cornerRange[side]) / cornerRange[side] : 0.0;
				  allowed = std::min(allowed, std::max(kDrawnShadowSliceToleranceMm, tolerance * fin / value));
			  }
			  return allowed;
		};
		thread_local std::vector<double> ranges;
		ranges.clear();
		ranges.push_back(nearRange);
		for (double range = nearRange; range < tipRange;) {
			const double stray = allowedStray(range);
			double		 length =
				  widest > 0.0 && std::isfinite(stray) ? 2.0 * std::sqrt(stray * shadow.shadowLength / widest) : shadow.shadowLength;
			double next = std::min(tipRange, range + std::max(length, 1.0));
			for (const double corner : cornerRange) {
				if (corner > range && corner < next) {
					next = corner;
				}
			}
			ranges.push_back(next);
			range = next;
		}

		// Each boundary's two side points and value.
		thread_local std::vector<std::array<glm::dvec2, 2>> sides;
		thread_local std::vector<float>						values;
		sides.resize(ranges.size());
		values.resize(ranges.size());
		for (std::size_t boundary = 0; boundary < ranges.size(); ++boundary) {
			const double shrink = 1.0 - progressAt(ranges[boundary]);
			values[boundary] = static_cast<float>(shadow.footValue * shrink);
			for (std::size_t side = 0; side < 2; ++side) {
				sides[boundary][side] = eyeMm + ranges[boundary] * turned(bisector, endAngle[side] * shrink);
			}
		}

		// From where each ray first meets the body outward: the far side of every hull face that faces the eye.
		thread_local std::vector<glm::dvec2>						hullPoints;
		thread_local std::vector<glm::dvec2>						hull;
		thread_local std::vector<std::pair<glm::dvec2, glm::dvec2>> nearFaces;
		hullOf(body, hullPoints, hull);
		nearFaces.clear();
		double nearFacesReach = nearRange;
		for (std::size_t corner = 0; corner < hull.size(); ++corner) {
			const glm::dvec2 from = hull[corner];
			const glm::dvec2 along = hull[(corner + 1) % hull.size()] - from;
			if (crossOf(along, eyeMm - from) < 0.0) {
				nearFaces.emplace_back(from, along);
				nearFacesReach =
					std::max({nearFacesReach, glm::length(from - eyeMm), glm::length(hull[(corner + 1) % hull.size()] - eyeMm)});
			}
		}

		thread_local ClipPolygon half;
		thread_local ClipPolygon clipped;
		for (std::size_t slice = 0; slice + 1 < ranges.size(); ++slice) {
			const double from = ranges[slice];
			const double to = ranges[slice + 1];
			if (to <= from) {
				continue;
			}
			const glm::dvec2 nearRight = sides[slice][0];
			const glm::dvec2 farRight = sides[slice + 1][0];
			const glm::dvec2 farLeft = sides[slice + 1][1];
			const glm::dvec2 nearLeft = sides[slice][1];
			const float		 nearValue = values[slice];
			const float		 farValue = values[slice + 1];
			if (from < nearFacesReach) {
				for (const bool first : {true, false}) {
					half.points.assign({nearRight, first ? farRight : farLeft, first ? farLeft : nearLeft});
					half.values.assign({nearValue, farValue, first ? farValue : nearValue});
					for (const auto& [faceFrom, faceAlong] : nearFaces) {
						if (half.points.size() < 3) {
							break;
						}
						clipLeftOf(half, faceFrom, faceAlong, clipped);
						std::swap(half, clipped);
					}
					appendFan(half, out);
				}
			} else {
				appendTriangle(nearRight, nearValue, farRight, farValue, farLeft, farValue, out);
				appendTriangle(nearRight, nearValue, farLeft, farValue, nearLeft, nearValue, out);
			}

			// The fins, past their corner's range: a side's fin lies outside it, away from the bisector.
			for (std::size_t side = 0; side < 2; ++side) {
				const double corner = cornerRange[side];
				if (from < corner) {
					continue;
				}
				const double outward = side == 0 ? -1.0 : 1.0;
				const auto	 outerAt = [&](glm::dvec2 point, double range) {
					  return point + leftOf(glm::normalize(point - eyeMm)) * (outward * halfRay * (range - corner) / corner);
				};
				const glm::dvec2 innerFrom = sides[slice][side];
				const glm::dvec2 innerTo = sides[slice + 1][side];
				const glm::dvec2 outerFrom = outerAt(innerFrom, from);
				const glm::dvec2 outerTo = outerAt(innerTo, to);
				appendTriangle(innerFrom, nearValue, innerTo, farValue, outerTo, 0.0F, out);
				appendTriangle(innerFrom, nearValue, outerTo, 0.0F, outerFrom, 0.0F, out);
			}
		}
		return shadow;
	}

	ShadowBand shadowBandAt(glm::dvec2 eyeMm, glm::dvec2 apexMm, int litTurn, double wideningMm, const vision::SightTunables& tunables) {
		ShadowBand band;
		band.apex = apexMm;
		band.litTurn = litTurn;
		band.widening = wideningMm;
		band.cornerDistance = glm::length(apexMm - eyeMm);
		band.ray = band.cornerDistance > 0.0 ? (apexMm - eyeMm) / band.cornerDistance : glm::dvec2{1.0, 0.0};
		const double reach = band.cornerDistance > 0.0 ? wideningMm / band.cornerDistance : 1.0;
		band.litAngle = std::asin(std::min(reach, std::sin(static_cast<double>(tunables.shadowWideningMostDegrees) * kDegree)));
		const double ramp = wideningMm > 0.0 ? std::clamp(2.0 - band.cornerDistance / wideningMm, 0.0, 1.0) : 0.0;
		band.shadowAngle = std::min(std::numbers::pi, std::asin(std::min(1.0, reach)) + std::numbers::pi / 2.0 * ramp);
		band.finAngle = band.cornerDistance > 0.0
							? std::min(
								  static_cast<double>(tunables.shadowFinMostDegrees) * kDegree,
								  std::atan(static_cast<double>(tunables.shadowEdgeWidthMm) / (2.0 * band.cornerDistance))
							  )
							: 0.0;
		return band;
	}

	void appendShadowBand(const ShadowBand& band, double farMm, std::vector<ValueTriangle>& out) {
		if (band.cornerDistance <= 0.0) {
			return;
		}
		const double turn = static_cast<double>(band.litTurn);
		// The band: a fan at the apex from its lit line round through the ray to its shadow line, its arc's
		// chords held past `farMm`.
		const double litEdge = turn * band.litAngle;
		const double shadowEdge = -turn * band.shadowAngle;
		const double sweep = litEdge - shadowEdge;
		const auto	 arcSteps = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / kBandArcStep)));
		const double arcReach = farMm / std::cos(std::abs(sweep) / static_cast<double>(arcSteps) / 2.0);
		glm::dvec2	 previous = band.apex + arcReach * turned(band.ray, litEdge);
		for (int step = 1; step <= arcSteps; ++step) {
			const glm::dvec2 next =
				band.apex + arcReach * turned(band.ray, litEdge - sweep * static_cast<double>(step) / static_cast<double>(arcSteps));
			appendTriangle(band.apex, 1.0F, previous, 1.0F, next, 1.0F, out);
			previous = next;
		}

		// The fin: from the lit line a further finAngle on, its value 1 there and 0 at its outer line,
		// constant along each ray from the apex, cut across into kShadowFinSlices triangles, each exact on
		// its far side and at the mean of its two values at the apex.
		if (band.finAngle <= 0.0) {
			return;
		}
		const double sliceAngle = band.finAngle / static_cast<double>(kShadowFinSlices);
		const double finReach = farMm / std::cos(sliceAngle / 2.0);
		for (std::size_t slice = 0; slice < kShadowFinSlices; ++slice) {
			const double	 innerAngle = litEdge + turn * sliceAngle * static_cast<double>(slice);
			const double	 outerAngle = litEdge + turn * sliceAngle * static_cast<double>(slice + 1);
			const auto		 innerValue = static_cast<float>(1.0 - static_cast<double>(slice) / static_cast<double>(kShadowFinSlices));
			const auto		 outerValue = static_cast<float>(1.0 - static_cast<double>(slice + 1) / static_cast<double>(kShadowFinSlices));
			const glm::dvec2 inner = band.apex + finReach * turned(band.ray, innerAngle);
			const glm::dvec2 outer = band.apex + finReach * turned(band.ray, outerAngle);
			appendTriangle(band.apex, (innerValue + outerValue) / 2.0F, inner, innerValue, outer, outerValue, out);
		}
	}

	std::uint32_t followedCorner(const vision::SightBody& body, std::uint32_t buildCorner, int litTurn, glm::dvec2 eyeMm) {
		const std::size_t count = body.edges.size();
		// The edge that starts where `corner`'s edge ends: outlines list each loop's edges in order, so it
		// is the next one round its loop, found by the previous-edge links.
		const auto nextOf = [&](std::uint32_t corner) {
			const std::uint32_t guess = static_cast<std::uint32_t>((corner + 1) % count);
			if (body.previousEdge[guess] == corner) {
				return guess;
			}
			for (std::uint32_t edge = 0; edge < count; ++edge) {
				if (body.previousEdge[edge] == corner) {
					return edge;
				}
			}
			return corner;
		};
		std::uint32_t corner = buildCorner;
		std::uint32_t came = buildCorner;
		for (std::size_t step = 0; step < count; ++step) {
			const glm::dvec2 vertex = toDouble(body.edges[corner].a);
			const glm::dvec2 next = toDouble(body.edges[corner].b);
			const glm::dvec2 previous = toDouble(body.edges[body.previousEdge[corner]].a);
			const auto		 side = [&](glm::dvec2 neighbour) {
				  const double turn = crossOf(vertex - eyeMm, neighbour - eyeMm);
				  return turn > 0.0 ? 1 : (turn < 0.0 ? -1 : 0);
			};
			const bool nextLit = side(next) == litTurn;
			const bool previousLit = side(previous) == litTurn;
			if (!nextLit && !previousLit) {
				return corner;
			}
			const std::uint32_t after = nextOf(corner);
			std::uint32_t		onward = corner;
			if (nextLit && after != came) {
				onward = after;
			} else if (previousLit && body.previousEdge[corner] != came) {
				onward = body.previousEdge[corner];
			} else {
				return corner;
			}
			came = corner;
			corner = onward;
		}
		return buildCorner;
	}

} // namespace worldview
