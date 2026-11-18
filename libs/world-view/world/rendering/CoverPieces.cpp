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
		// The most slices one soft triangle is cut into, and the most corners a clipped slice can have.
		constexpr std::size_t kMostSlices		   = 64;
		constexpr std::size_t kMostClippedCorners = 16;

		glm::dvec2 toDouble(const geometry::Vec2i64& point) { return {static_cast<double>(point.x), static_cast<double>(point.y)}; }

		double crossOf(glm::dvec2 first, glm::dvec2 second) { return first.x * second.y - first.y * second.x; }

		glm::dvec2 turned(glm::dvec2 direction, double angle) {
			const double cosine = std::cos(angle);
			const double sine	= std::sin(angle);
			return {direction.x * cosine - direction.y * sine, direction.x * sine + direction.y * cosine};
		}

		glm::dvec2 leftOf(glm::dvec2 direction) { return {-direction.y, direction.x}; }

		void appendTriangle(glm::dvec2 first, float firstValue, glm::dvec2 second, float secondValue, glm::dvec2 third, float thirdValue, std::vector<ValueTriangle>& out) {
			out.push_back({.points = {first, second, third}, .values = {firstValue, secondValue, thirdValue}});
		}

		// A convex polygon of a few corners with a linear value, on the stack.
		struct SmallPolygon {
			std::array<glm::dvec2, kMostClippedCorners> points{};
			std::array<float, kMostClippedCorners>		values{};
			std::size_t									count = 0;
		};

		// The part of `polygon` on the left of the line from `from` along `along`, its values linear along
		// each cut side, as Sutherland and Hodgman clip.
		SmallPolygon clippedLeftOf(const SmallPolygon& polygon, glm::dvec2 from, glm::dvec2 along) {
			SmallPolygon kept;
			for (std::size_t k = 0; k < polygon.count && kept.count + 2 <= kMostClippedCorners; ++k) {
				const std::size_t next	= (k + 1) % polygon.count;
				const double	  here	= crossOf(along, polygon.points[k] - from);
				const double	  there = crossOf(along, polygon.points[next] - from);
				if (here >= 0.0) {
					kept.points[kept.count] = polygon.points[k];
					kept.values[kept.count] = polygon.values[k];
					++kept.count;
				}
				if ((here >= 0.0) != (there >= 0.0)) {
					const double t			= here / (here - there);
					kept.points[kept.count] = polygon.points[k] + t * (polygon.points[next] - polygon.points[k]);
					kept.values[kept.count] = static_cast<float>(static_cast<double>(polygon.values[k]) + t * static_cast<double>(polygon.values[next] - polygon.values[k]));
					++kept.count;
				}
			}
			return kept;
		}

		void appendFan(const SmallPolygon& polygon, std::vector<ValueTriangle>& out) {
			for (std::size_t corner = 1; corner + 1 < polygon.count; ++corner) {
				appendTriangle(polygon.points[0], polygon.values[0], polygon.points[corner], polygon.values[corner], polygon.points[corner + 1], polygon.values[corner + 1], out);
			}
		}

		// The convex hull of the body's corners, counter-clockwise, into `hull` (`points` is scratch).
		void hullOf(const vision::SightBody& body, std::vector<glm::dvec2>& points, std::vector<glm::dvec2>& hull) {
			points.clear();
			for (const geometry::OccluderSegment& edge : body.edges) {
				points.push_back(toDouble(edge.a));
			}
			std::ranges::sort(points, [](glm::dvec2 first, glm::dvec2 second) { return first.x < second.x || (first.x == second.x && first.y < second.y); });
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
		const glm::dvec2 leaving  = outgoing / outgoingLength;
		const double	 turn	  = crossOf(arriving, leaving);
		const double	 along	  = glm::dot(arriving, leaving);
		if (turn > 0.0 || (turn == 0.0 && along > 0.0)) {
			return;
		}
		// Clockwise from the arriving edge's inward normal to the leaving one's.
		const double	 sweep = std::atan2(-turn, along);
		const auto		 steps = static_cast<int>(std::ceil(sweep / (kFeatherFanStepDegrees * kDegree) - 1e-9));
		const glm::dvec2 start = leftOf(arriving);
		for (int step = 0; step < steps; ++step) {
			const glm::dvec2 from = turned(start, -sweep * static_cast<double>(step) / static_cast<double>(steps));
			const glm::dvec2 to	  = turned(start, -sweep * static_cast<double>(step + 1) / static_cast<double>(steps));
			appendTriangle(corner, 1.0F, corner + featherMm * from, 0.0F, corner + featherMm * to, 0.0F, out);
		}
	}

	void appendFeather(std::span<const DirectedEdge> edges, double featherMm, std::vector<ValueTriangle>& out) {
		std::multimap<geometry::Vec2i64, std::size_t> starting;
		for (std::size_t index = 0; index < edges.size(); ++index) {
			const glm::dvec2 start	= toDouble(edges[index].first);
			const glm::dvec2 end	= toDouble(edges[index].second);
			const glm::dvec2 along	= end - start;
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
				appendFeatherFan(toDouble(edge.second), toDouble(edge.second) - toDouble(edge.first), toDouble(leaving.second) - toDouble(leaving.first), featherMm, out);
			}
		}
	}

	std::optional<SoftShadow>
	appendSoftShadow(const vision::SightBody& body, glm::dvec2 eyeMm, const vision::SightTunables& tunables, std::vector<ValueTriangle>& out) {
		const std::optional<vision::Silhouette> silhouette = vision::silhouetteFrom(body, eyeMm);
		if (!silhouette.has_value()) {
			return std::nullopt;
		}
		const glm::dvec2 clockwiseCorner		= toDouble(body.edges[silhouette->clockwise].a);
		const glm::dvec2 counterClockwiseCorner = toDouble(body.edges[silhouette->counterClockwise].a);
		const glm::dvec2 bisector = glm::normalize(glm::normalize(clockwiseCorner - eyeMm) + glm::normalize(counterClockwiseCorner - eyeMm));

		// The width across the bisector and the nearest point, in one pass over the outline (2.3).
		const glm::dvec2 across = leftOf(bisector);
		double			 least	= std::numeric_limits<double>::max();
		double			 most	= std::numeric_limits<double>::lowest();
		double			 nearestSquared = std::numeric_limits<double>::max();
		for (const geometry::OccluderSegment& edge : body.edges) {
			const glm::dvec2 from	= toDouble(edge.a) - eyeMm;
			const glm::dvec2 along	= toDouble(edge.b) - toDouble(edge.a);
			const double	 offset = glm::dot(from, across);
			least					= std::min(least, offset);
			most					= std::max(most, offset);
			const double length		= glm::dot(along, along);
			const double t			= length > 0.0 ? std::clamp(-glm::dot(from, along) / length, 0.0, 1.0) : 0.0;
			const glm::dvec2 nearest = from + t * along;
			nearestSquared			= std::min(nearestSquared, glm::dot(nearest, nearest));
		}
		const double width	  = most - least;
		const double rayWidth = static_cast<double>(tunables.softShadowRayWidthMm);
		if (width <= 0.0 || width >= rayWidth) {
			return std::nullopt;
		}
		SoftShadow shadow;
		shadow.widthAsSeen	= width;
		shadow.distance		= std::sqrt(nearestSquared);
		shadow.footValue	= static_cast<double>(tunables.softShadowStrengthPercent) / 100.0 * width / rayWidth *
							  static_cast<double>(body.arrival) / static_cast<double>(geometry::kFractionOne);
		shadow.shadowLength = shadow.distance * width / (rayWidth - width);
		if (shadow.footValue <= 0.0 || shadow.shadowLength <= 0.0) {
			return shadow;
		}

		const std::array<double, 2> endAngle = {
			std::atan2(crossOf(bisector, clockwiseCorner - eyeMm), glm::dot(bisector, clockwiseCorner - eyeMm)),
			std::atan2(crossOf(bisector, counterClockwiseCorner - eyeMm), glm::dot(bisector, counterClockwiseCorner - eyeMm)),
		};
		const std::array<double, 2> cornerRange = {glm::length(clockwiseCorner - eyeMm), glm::length(counterClockwiseCorner - eyeMm)};
		const double				nearRange	= shadow.distance;
		const double				tipRange	= shadow.distance + shadow.shadowLength;
		const auto progressAt = [&](double range) { return std::clamp((range - nearRange) / shadow.shadowLength, 0.0, 1.0); };

		// A side at range r lies r x angle x (1 - progress) across the bisector, a parabola in r whose
		// second derivative is 2 x angle / shadowLength, so a chord h long strays angle x h^2 /
		// (4 x shadowLength) from it: equal slices that short keep both sides within the tolerance. A slice
		// boundary also falls at each fin's start.
		const double widest = std::max(std::abs(endAngle[0]), std::abs(endAngle[1]));
		const double longest =
			widest > 0.0 ? 2.0 * std::sqrt(kDrawnShadowSliceToleranceMm * shadow.shadowLength / widest) : shadow.shadowLength;
		const auto slices =
			std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(shadow.shadowLength / longest)), 1, kMostSlices - 3);
		std::array<double, kMostSlices> ranges{};
		std::size_t						rangeCount = 0;
		for (std::size_t slice = 0; slice <= slices; ++slice) {
			ranges[rangeCount++] = nearRange + shadow.shadowLength * static_cast<double>(slice) / static_cast<double>(slices);
		}
		for (const double corner : cornerRange) {
			if (corner > nearRange && corner < tipRange) {
				ranges[rangeCount++] = corner;
			}
		}
		std::sort(ranges.begin(), ranges.begin() + static_cast<std::ptrdiff_t>(rangeCount));

		// Each boundary's two side points and value.
		std::array<std::array<glm::dvec2, 2>, kMostSlices> sides{};
		std::array<float, kMostSlices>					   values{};
		for (std::size_t k = 0; k < rangeCount; ++k) {
			const double shrink = 1.0 - progressAt(ranges[k]);
			values[k]			= static_cast<float>(shadow.footValue * shrink);
			for (std::size_t side = 0; side < 2; ++side) {
				sides[k][side] = eyeMm + ranges[k] * turned(bisector, endAngle[side] * shrink);
			}
		}

		// From where each ray first meets the body outward: the far side of every hull face that faces the eye.
		thread_local std::vector<glm::dvec2> hullPoints;
		thread_local std::vector<glm::dvec2> hull;
		hullOf(body, hullPoints, hull);
		std::array<std::pair<glm::dvec2, glm::dvec2>, kMostClippedCorners> nearFaces{};
		std::size_t														   faceCount	  = 0;
		double															   nearFacesReach = nearRange;
		for (std::size_t k = 0; k < hull.size() && faceCount < kMostClippedCorners; ++k) {
			const glm::dvec2 from  = hull[k];
			const glm::dvec2 along = hull[(k + 1) % hull.size()] - from;
			if (crossOf(along, eyeMm - from) < 0.0) {
				nearFaces[faceCount++] = {from, along};
				nearFacesReach		   = std::max({nearFacesReach, glm::length(from - eyeMm), glm::length(hull[(k + 1) % hull.size()] - eyeMm)});
			}
		}

		const double halfRay = static_cast<double>(tunables.shadowEdgeWidthMm) / 2.0;
		for (std::size_t k = 0; k + 1 < rangeCount; ++k) {
			const double from = ranges[k];
			const double to	  = ranges[k + 1];
			if (to <= from) {
				continue;
			}
			const glm::dvec2 nearRight = sides[k][0];
			const glm::dvec2 farRight  = sides[k + 1][0];
			const glm::dvec2 farLeft   = sides[k + 1][1];
			const glm::dvec2 nearLeft  = sides[k][1];
			const float		 nearValue = values[k];
			const float		 farValue  = values[k + 1];
			if (from < nearFacesReach) {
				for (SmallPolygon half : {SmallPolygon{.points = {nearRight, farRight, farLeft}, .values = {nearValue, farValue, farValue}, .count = 3},
										  SmallPolygon{.points = {nearRight, farLeft, nearLeft}, .values = {nearValue, farValue, nearValue}, .count = 3}}) {
					for (std::size_t face = 0; face < faceCount && half.count >= 3; ++face) {
						half = clippedLeftOf(half, nearFaces[face].first, nearFaces[face].second);
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
				const glm::dvec2 innerFrom = sides[k][side];
				const glm::dvec2 innerTo   = sides[k + 1][side];
				const glm::dvec2 outerFrom = outerAt(innerFrom, from);
				const glm::dvec2 outerTo   = outerAt(innerTo, to);
				appendTriangle(innerFrom, nearValue, innerTo, farValue, outerTo, 0.0F, out);
				appendTriangle(innerFrom, nearValue, outerTo, 0.0F, outerFrom, 0.0F, out);
			}
		}
		return shadow;
	}

	ShadowBand shadowBandAt(glm::dvec2 eyeMm, glm::dvec2 apexMm, int litTurn, double wideningMm, const vision::SightTunables& tunables) {
		ShadowBand band;
		band.apex			= apexMm;
		band.litTurn		= litTurn;
		band.widening		= wideningMm;
		band.cornerDistance = glm::length(apexMm - eyeMm);
		band.ray			= band.cornerDistance > 0.0 ? (apexMm - eyeMm) / band.cornerDistance : glm::dvec2{1.0, 0.0};
		const double reach	= band.cornerDistance > 0.0 ? wideningMm / band.cornerDistance : 1.0;
		band.litAngle		= std::asin(std::min(reach, std::sin(static_cast<double>(tunables.shadowWideningMostDegrees) * kDegree)));
		const double ramp	= wideningMm > 0.0 ? std::clamp(2.0 - band.cornerDistance / wideningMm, 0.0, 1.0) : 0.0;
		band.shadowAngle	= std::min(std::numbers::pi, std::asin(std::min(1.0, reach)) + std::numbers::pi / 2.0 * ramp);
		band.finAngle		= band.cornerDistance > 0.0
								  ? std::min(static_cast<double>(tunables.shadowFinMostDegrees) * kDegree, std::atan(static_cast<double>(tunables.shadowEdgeWidthMm) / (2.0 * band.cornerDistance)))
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
		const double litEdge	= turn * band.litAngle;
		const double shadowEdge = -turn * band.shadowAngle;
		const double sweep		= litEdge - shadowEdge;
		const auto	 arcSteps	= std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / kBandArcStep)));
		const double arcReach	= farMm / std::cos(std::abs(sweep) / static_cast<double>(arcSteps) / 2.0);
		glm::dvec2	 previous	= band.apex + arcReach * turned(band.ray, litEdge);
		for (int step = 1; step <= arcSteps; ++step) {
			const glm::dvec2 next = band.apex + arcReach * turned(band.ray, litEdge - sweep * static_cast<double>(step) / static_cast<double>(arcSteps));
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
		const double finReach	= farMm / std::cos(sliceAngle / 2.0);
		for (std::size_t slice = 0; slice < kShadowFinSlices; ++slice) {
			const double	 innerAngle = litEdge + turn * sliceAngle * static_cast<double>(slice);
			const double	 outerAngle = litEdge + turn * sliceAngle * static_cast<double>(slice + 1);
			const auto		 innerValue = static_cast<float>(1.0 - static_cast<double>(slice) / static_cast<double>(kShadowFinSlices));
			const auto		 outerValue = static_cast<float>(1.0 - static_cast<double>(slice + 1) / static_cast<double>(kShadowFinSlices));
			const glm::dvec2 inner		= band.apex + finReach * turned(band.ray, innerAngle);
			const glm::dvec2 outer		= band.apex + finReach * turned(band.ray, outerAngle);
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
		std::uint32_t came	 = buildCorner;
		for (std::size_t step = 0; step < count; ++step) {
			const glm::dvec2 vertex	  = toDouble(body.edges[corner].a);
			const glm::dvec2 next	  = toDouble(body.edges[corner].b);
			const glm::dvec2 previous = toDouble(body.edges[body.previousEdge[corner]].a);
			const auto		 side	  = [&](glm::dvec2 neighbour) {
				  const double turn = crossOf(vertex - eyeMm, neighbour - eyeMm);
				  return turn > 0.0 ? 1 : (turn < 0.0 ? -1 : 0);
			};
			const bool nextLit	   = side(next) == litTurn;
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
			came   = corner;
			corner = onward;
		}
		return buildCorner;
	}

} // namespace worldview
