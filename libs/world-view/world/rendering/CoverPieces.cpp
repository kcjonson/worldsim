#include "CoverPieces.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <numbers>

namespace worldview {

	namespace {

		constexpr double kDegree = std::numbers::pi / 180.0;
		// The band's far arc is drawn in steps no wider than this, so its chords stay well past the far reach.
		constexpr double kBandArcStep = 30.0 * kDegree;
		// The deepest a triangle's slices are halved.
		constexpr int kMostSliceHalvings = 12;

		glm::dvec2 toDouble(const geometry::Vec2i64& point) { return {static_cast<double>(point.x), static_cast<double>(point.y)}; }

		double crossOf(glm::dvec2 first, glm::dvec2 second) { return first.x * second.y - first.y * second.x; }

		glm::dvec2 turned(glm::dvec2 direction, double angle) {
			const double cosine = std::cos(angle);
			const double sine	= std::sin(angle);
			return {direction.x * cosine - direction.y * sine, direction.x * sine + direction.y * cosine};
		}

		glm::dvec2 leftOf(glm::dvec2 direction) { return {-direction.y, direction.x}; }

		void appendTriangle(glm::dvec2 first, float firstValue, glm::dvec2 second, float secondValue, glm::dvec2 third, float thirdValue, std::vector<ValuePolygon>& out) {
			out.push_back({.points = {first, second, third}, .values = {firstValue, secondValue, thirdValue}});
		}

		// The part of `polygon` on the left of the line from `from` along `along`, its values linear along
		// each cut side, as Sutherland and Hodgman clip.
		ValuePolygon clippedLeftOf(const ValuePolygon& polygon, glm::dvec2 from, glm::dvec2 along) {
			ValuePolygon kept;
			const std::size_t count = polygon.points.size();
			for (std::size_t k = 0; k < count; ++k) {
				const glm::dvec2 point	   = polygon.points[k];
				const glm::dvec2 next	   = polygon.points[(k + 1) % count];
				const double	 here	   = crossOf(along, point - from);
				const double	 there	   = crossOf(along, next - from);
				const float		 value	   = polygon.values[k];
				const float		 nextValue = polygon.values[(k + 1) % count];
				if (here >= 0.0) {
					kept.points.push_back(point);
					kept.values.push_back(value);
				}
				if ((here >= 0.0) != (there >= 0.0)) {
					const double t = here / (here - there);
					kept.points.push_back(point + t * (next - point));
					kept.values.push_back(static_cast<float>(static_cast<double>(value) + t * static_cast<double>(nextValue - value)));
				}
			}
			return kept;
		}

		// The convex hull of the body's corners, counter-clockwise.
		std::vector<glm::dvec2> hullOf(const vision::SightBody& body) {
			std::vector<glm::dvec2> points;
			points.reserve(body.edges.size());
			for (const geometry::OccluderSegment& edge : body.edges) {
				points.push_back(toDouble(edge.a));
			}
			std::ranges::sort(points, [](glm::dvec2 first, glm::dvec2 second) { return first.x < second.x || (first.x == second.x && first.y < second.y); });
			points.erase(std::unique(points.begin(), points.end()), points.end());
			if (points.size() < 3) {
				return points;
			}
			std::vector<glm::dvec2> hull(2 * points.size());
			std::size_t				size = 0;
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
			return hull;
		}

	} // namespace

	std::vector<renderer::TessellatedMesh> valueMeshes(std::span<const ValuePolygon> pieces) {
		constexpr std::size_t				   kMeshVertexLimit = 65535;
		std::vector<renderer::TessellatedMesh> meshes;
		renderer::TessellatedMesh			   mesh;
		for (const ValuePolygon& piece : pieces) {
			if (piece.points.size() < 3) {
				continue;
			}
			if (mesh.vertices.size() + 3 * (piece.points.size() - 2) > kMeshVertexLimit) {
				meshes.push_back(std::move(mesh));
				mesh = renderer::TessellatedMesh{};
			}
			for (std::size_t corner = 1; corner + 1 < piece.points.size(); ++corner) {
				for (const std::size_t index : {std::size_t{0}, corner, corner + 1}) {
					mesh.indices.push_back(static_cast<std::uint16_t>(mesh.vertices.size()));
					mesh.vertices.push_back({static_cast<float>(piece.points[index].x / 1000.0), static_cast<float>(piece.points[index].y / 1000.0)});
					mesh.colors.push_back({1.0F, 1.0F, 1.0F, piece.values[index]});
				}
			}
		}
		if (!mesh.vertices.empty()) {
			meshes.push_back(std::move(mesh));
		}
		return meshes;
	}

	void appendFeatherFan(glm::dvec2 corner, glm::dvec2 incoming, glm::dvec2 outgoing, double featherMm, std::vector<ValuePolygon>& out) {
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
		const double	  sweep = std::atan2(-turn, along);
		const auto		  steps = static_cast<int>(std::ceil(sweep / (kFeatherFanStepDegrees * kDegree) - 1e-9));
		const glm::dvec2  start = leftOf(arriving);
		for (int step = 0; step < steps; ++step) {
			const glm::dvec2 from = turned(start, -sweep * static_cast<double>(step) / static_cast<double>(steps));
			const glm::dvec2 to	  = turned(start, -sweep * static_cast<double>(step + 1) / static_cast<double>(steps));
			appendTriangle(corner, 1.0F, corner + featherMm * from, 0.0F, corner + featherMm * to, 0.0F, out);
		}
	}

	void appendFeather(std::span<const DirectedEdge> edges, double featherMm, std::vector<ValuePolygon>& out) {
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
			out.push_back({.points = {start, end, end + inward, start + inward}, .values = {1.0F, 1.0F, 0.0F, 0.0F}});
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
	appendSoftShadow(const vision::SightBody& body, glm::dvec2 eyeMm, const vision::SightTunables& tunables, std::vector<ValuePolygon>& out) {
		const std::optional<vision::Silhouette> silhouette = vision::silhouetteFrom(body, eyeMm);
		const std::optional<double>				width	   = vision::widthAsSeen(body, eyeMm);
		if (!silhouette.has_value() || !width.has_value()) {
			return std::nullopt;
		}
		const double rayWidth = static_cast<double>(tunables.softShadowRayWidthMm);
		if (*width <= 0.0 || *width >= rayWidth) {
			return std::nullopt;
		}
		SoftShadow shadow;
		shadow.widthAsSeen	= *width;
		shadow.distance		= vision::distanceTo(body, eyeMm);
		shadow.footValue	= static_cast<double>(tunables.softShadowStrengthPercent) / 100.0 * *width / rayWidth *
							  static_cast<double>(body.arrival) / static_cast<double>(geometry::kFractionOne);
		shadow.shadowLength = shadow.distance * *width / (rayWidth - *width);
		if (shadow.footValue <= 0.0 || shadow.shadowLength <= 0.0) {
			return shadow;
		}

		const glm::dvec2 clockwiseCorner		= toDouble(body.edges[silhouette->clockwise].a);
		const glm::dvec2 counterClockwiseCorner = toDouble(body.edges[silhouette->counterClockwise].a);
		const glm::dvec2 bisector = glm::normalize(glm::normalize(clockwiseCorner - eyeMm) + glm::normalize(counterClockwiseCorner - eyeMm));
		const std::array<double, 2> endAngle = {
			std::atan2(crossOf(bisector, clockwiseCorner - eyeMm), glm::dot(bisector, clockwiseCorner - eyeMm)),
			std::atan2(crossOf(bisector, counterClockwiseCorner - eyeMm), glm::dot(bisector, counterClockwiseCorner - eyeMm)),
		};
		const std::array<double, 2> cornerRange = {glm::length(clockwiseCorner - eyeMm), glm::length(counterClockwiseCorner - eyeMm)};
		const double				nearRange	= shadow.distance;
		const double				tipRange	= shadow.distance + shadow.shadowLength;
		const auto progressAt = [&](double range) { return std::clamp((range - nearRange) / shadow.shadowLength, 0.0, 1.0); };
		const auto valueAt	  = [&](double range) { return static_cast<float>(shadow.footValue * (1.0 - progressAt(range))); };
		const auto sideAt	  = [&](std::size_t side, double range) {
			 return eyeMm + range * turned(bisector, endAngle[side] * (1.0 - progressAt(range)));
		};

		// Slices along the length, halved until both sides lie within the tolerance of their chords,
		// with a boundary at each fin's start.
		std::vector<double> ranges = {nearRange, tipRange};
		for (const double corner : cornerRange) {
			if (corner > nearRange && corner < tipRange) {
				ranges.push_back(corner);
			}
		}
		std::ranges::sort(ranges);
		std::vector<double> sliced;
		const auto			refine = [&](auto& self, double from, double to, int depth) -> void {
			 const double middle   = (from + to) / 2.0;
			 double		  straying = 0.0;
			 for (std::size_t side = 0; side < 2; ++side) {
				 straying = std::max(straying, glm::length(sideAt(side, middle) - (sideAt(side, from) + sideAt(side, to)) / 2.0));
			 }
			 if (straying > kDrawnShadowSliceToleranceMm && depth < kMostSliceHalvings) {
				 self(self, from, middle, depth + 1);
				 self(self, middle, to, depth + 1);
			 } else {
				 sliced.push_back(to);
			 }
		};
		sliced.push_back(ranges.front());
		for (std::size_t k = 0; k + 1 < ranges.size(); ++k) {
			refine(refine, ranges[k], ranges[k + 1], 0);
		}

		// From where each ray first meets the body outward: the far side of every hull face that faces the eye.
		const std::vector<glm::dvec2> hull = hullOf(body);
		std::vector<std::pair<glm::dvec2, glm::dvec2>> nearFaces;
		double										   nearFacesReach = nearRange;
		for (std::size_t k = 0; k < hull.size(); ++k) {
			const glm::dvec2 from  = hull[k];
			const glm::dvec2 along = hull[(k + 1) % hull.size()] - from;
			if (crossOf(along, eyeMm - from) < 0.0) {
				nearFaces.emplace_back(from, along);
				nearFacesReach = std::max({nearFacesReach, glm::length(from - eyeMm), glm::length(hull[(k + 1) % hull.size()] - eyeMm)});
			}
		}

		for (std::size_t k = 0; k + 1 < sliced.size(); ++k) {
			const double	 from	   = sliced[k];
			const double	 to		   = sliced[k + 1];
			const float		 fromValue = valueAt(from);
			const float		 toValue   = valueAt(to);
			const glm::dvec2 nearRight = sideAt(0, from);
			const glm::dvec2 farRight  = sideAt(0, to);
			const glm::dvec2 farLeft   = sideAt(1, to);
			const glm::dvec2 nearLeft  = sideAt(1, from);
			for (ValuePolygon half : {ValuePolygon{.points = {nearRight, farRight, farLeft}, .values = {fromValue, toValue, toValue}},
									  ValuePolygon{.points = {nearRight, farLeft, nearLeft}, .values = {fromValue, toValue, fromValue}}}) {
				if (from < nearFacesReach) {
					for (const auto& [faceFrom, faceAlong] : nearFaces) {
						half = clippedLeftOf(half, faceFrom, faceAlong);
					}
				}
				if (half.points.size() >= 3) {
					out.push_back(std::move(half));
				}
			}

			// The fins, past their corner's range: a side's fin lies outside it, away from the bisector.
			for (std::size_t side = 0; side < 2; ++side) {
				const double corner = cornerRange[side];
				if (from < corner) {
					continue;
				}
				const double	 outward  = side == 0 ? -1.0 : 1.0;
				const double	 halfRay  = static_cast<double>(tunables.shadowEdgeWidthMm) / 2.0;
				const auto		 outerAt  = [&](double range) {
					  const glm::dvec2 point  = sideAt(side, range);
					  const glm::dvec2 across = leftOf(glm::normalize(point - eyeMm)) * outward;
					  return point + across * (halfRay * (range - corner) / corner);
				};
				const glm::dvec2 innerFrom = sideAt(side, from);
				const glm::dvec2 innerTo   = sideAt(side, to);
				const glm::dvec2 outerFrom = outerAt(from);
				const glm::dvec2 outerTo   = outerAt(to);
				appendTriangle(innerFrom, fromValue, innerTo, toValue, outerTo, 0.0F, out);
				appendTriangle(innerFrom, fromValue, outerTo, 0.0F, outerFrom, 0.0F, out);
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

	void appendShadowBand(const ShadowBand& band, double farMm, std::vector<ValuePolygon>& out) {
		if (band.cornerDistance <= 0.0) {
			return;
		}
		const double turn = static_cast<double>(band.litTurn);
		// The band: from its lit line round through the ray to its shadow line, its arc's chords held past
		// `farMm`.
		const double litEdge	= turn * band.litAngle;
		const double shadowEdge = -turn * band.shadowAngle;
		const double sweep		= litEdge - shadowEdge;
		const auto	 arcSteps	= std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / kBandArcStep)));
		const double arcReach	= farMm / std::cos(std::abs(sweep) / static_cast<double>(arcSteps) / 2.0);
		ValuePolygon wedge;
		wedge.points.push_back(band.apex);
		for (int step = 0; step <= arcSteps; ++step) {
			const double angle = litEdge - sweep * static_cast<double>(step) / static_cast<double>(arcSteps);
			wedge.points.push_back(band.apex + arcReach * turned(band.ray, angle));
		}
		wedge.values.assign(wedge.points.size(), 1.0F);
		out.push_back(std::move(wedge));

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
		const std::size_t		   count = body.edges.size();
		std::vector<std::uint32_t> nextEdge(count, 0);
		for (std::uint32_t edge = 0; edge < count; ++edge) {
			nextEdge[body.previousEdge[edge]] = edge;
		}
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
			std::uint32_t onward = corner;
			if (nextLit && nextEdge[corner] != came) {
				onward = nextEdge[corner];
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
