#include "BuilderSpots.h"

#include <core/Vec2d.h>
#include <core/Vec2i64.h>
#include <predicates/Predicates.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace construction {

	namespace {

		// Float config values land a hair off their decimal (0.7F is 0.69999998), so a product that
		// should be a whole number of spots is lifted past the floor's edge before it is taken.
		constexpr double kWholeSpotTolerance = 1e-6;

		geometry::Vec2d toDouble(const geometry::Vec2i64& point) {
			return {static_cast<double>(point.x), static_cast<double>(point.y)};
		}

		double crossProduct(const geometry::Vec2d& first, const geometry::Vec2d& second) {
			return first.x * second.y - first.y * second.x;
		}

		// The ring's coordinates are whole millimetres, so a spot nearer a vertex than one of them is at
		// that vertex: it insets along the corner's bisector rather than along one edge's normal, which
		// would run along the other edge.
		constexpr double kVertexToleranceMm = 1.0;

		/// The unit normal of `edge` pointing into the footprint, zero for an edge of no length.
		geometry::Vec2d inwardNormal(const geometry::Ring& ring, const std::vector<double>& edgeLengths, std::size_t edge, double interiorSide) {
			if (edgeLengths[edge] == 0.0) {
				return {0.0, 0.0};
			}
			const geometry::Vec2d edgeVector = toDouble(ring[(edge + 1) % ring.size()]) - toDouble(ring[edge]);
			return geometry::Vec2d{-edgeVector.y, edgeVector.x} * (interiorSide / edgeLengths[edge]);
		}

		/// True when the inset from `onRing` to `inset` stays in the footprint: it ends strictly inside
		/// the ring and passes through no edge but the one or two it leaves from.
		bool insetStaysInside(
			const geometry::Ring& ring, std::size_t fromEdge, std::size_t otherFromEdge, const geometry::Vec2d& onRing, const geometry::Vec2d& inset
		) {
			const geometry::Vec2i64 insetMm{static_cast<std::int64_t>(std::llround(inset.x)), static_cast<std::int64_t>(std::llround(inset.y))};
			if (geometry::pointInPolygon(insetMm, ring) != geometry::PointInPolygon::Inside) {
				return false;
			}
			// Parameters along the probe within this of its start are an edge it leaves from (or the
			// neighbour sharing a vertex it sits on); within this of its end, the probe only touches.
			constexpr double	  kProbeTolerance = 1e-9;
			const geometry::Vec2d probe = inset - onRing;
			const std::size_t	  vertexCount = ring.size();
			for (std::size_t edge = 0; edge < vertexCount; ++edge) {
				if (edge == fromEdge || edge == otherFromEdge) {
					continue;
				}
				const geometry::Vec2d edgeStart = toDouble(ring[edge]);
				const geometry::Vec2d edgeVector = toDouble(ring[(edge + 1) % vertexCount]) - edgeStart;
				const double		  denominator = crossProduct(probe, edgeVector);
				if (denominator == 0.0) {
					continue;
				}
				const geometry::Vec2d startOffset = edgeStart - onRing;
				const double		  alongProbe = crossProduct(startOffset, edgeVector) / denominator;
				const double		  alongEdge = crossProduct(startOffset, probe) / denominator;
				if (alongProbe > kProbeTolerance && alongProbe < 1.0 - kProbeTolerance && alongEdge >= 0.0 && alongEdge <= 1.0) {
					return false;
				}
			}
			return true;
		}

	} // namespace

	double ringPerimeterMeters(const geometry::Ring& ring) {
		const std::size_t vertexCount = ring.size();
		if (vertexCount < 2) {
			return 0.0;
		}
		double perimeterMm = 0.0;
		for (std::size_t index = 0; index < vertexCount; ++index) {
			perimeterMm += geometry::length(toDouble(ring[(index + 1) % vertexCount]) - toDouble(ring[index]));
		}
		return perimeterMm / static_cast<double>(geometry::kMillimetersPerMeter);
	}

	int builderSpotCount(double areaSquareMeters, double perimeterMeters, int capBase, float capPerSquareMeter, float minSpacingMeters) {
		const double bySize = std::floor(std::abs(areaSquareMeters) * static_cast<double>(capPerSquareMeter) + kWholeSpotTolerance);
		const double bySpacing = std::floor(std::max(perimeterMeters, 0.0) / static_cast<double>(minSpacingMeters) + kWholeSpotTolerance);
		// fmin and fmax pass over a NaN (a zero perimeter over a zero spacing) instead of returning it.
		const double count = std::fmin(static_cast<double>(capBase) + std::max(bySize, 0.0), bySpacing);
		return static_cast<int>(std::fmax(count, 1.0));
	}

	std::vector<glm::vec2> builderSpots(const geometry::Ring& ring, int count, float insetMeters) {
		const std::size_t vertexCount = ring.size();
		std::vector<glm::vec2> spots;
		if (vertexCount < 3 || count < 1) {
			return spots;
		}

		std::vector<double> edgeLengths(vertexCount);
		double				perimeterMm = 0.0;
		for (std::size_t index = 0; index < vertexCount; ++index) {
			edgeLengths[index] = geometry::length(toDouble(ring[(index + 1) % vertexCount]) - toDouble(ring[index]));
			perimeterMm += edgeLengths[index];
		}
		if (perimeterMm <= 0.0) {
			return spots;
		}

		// The interior lies left of each edge on a counter-clockwise ring, right of it on a clockwise one.
		const double interiorSide = geometry::windingOrder(ring) == geometry::Winding::Clockwise ? -1.0 : 1.0;
		const double millimetersPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);
		const double insetMm = static_cast<double>(insetMeters) * millimetersPerMeter;

		spots.reserve(static_cast<std::size_t>(count));
		std::size_t edge = 0;
		double		edgeStartArc = 0.0;
		for (int spot = 0; spot < count; ++spot) {
			const double arc = (static_cast<double>(spot) + 0.5) * perimeterMm / static_cast<double>(count);
			while (edge + 1 < vertexCount && (edgeLengths[edge] == 0.0 || arc >= edgeStartArc + edgeLengths[edge])) {
				edgeStartArc += edgeLengths[edge];
				++edge;
			}
			const geometry::Vec2d edgeStart = toDouble(ring[edge]);
			const geometry::Vec2d edgeVector = toDouble(ring[(edge + 1) % vertexCount]) - edgeStart;
			if (edgeLengths[edge] == 0.0) {
				continue;
			}
			const double		  along = std::clamp((arc - edgeStartArc) / edgeLengths[edge], 0.0, 1.0);
			const geometry::Vec2d onRing = edgeStart + edgeVector * along;

			// A spot at a vertex leaves along the sum of its two edges' inward normals, which bisects the
			// interior angle whether the corner is convex or reflex, and is the shared normal at a vertex
			// the ring runs straight through.
			std::size_t fromEdge = edge;
			std::size_t otherFromEdge = edge;
			if (along * edgeLengths[edge] <= kVertexToleranceMm) {
				fromEdge = (edge + vertexCount - 1) % vertexCount;
			} else if ((1.0 - along) * edgeLengths[edge] <= kVertexToleranceMm) {
				otherFromEdge = (edge + 1) % vertexCount;
			}
			const geometry::Vec2d direction = inwardNormal(ring, edgeLengths, fromEdge, interiorSide) +
											  inwardNormal(ring, edgeLengths, otherFromEdge, interiorSide);
			const double		  directionLength = geometry::length(direction);

			geometry::Vec2d standing = onRing;
			if (insetMm > 0.0 && directionLength > 0.0) {
				const geometry::Vec2d inset = onRing + direction * (insetMm / directionLength);
				if (insetStaysInside(ring, fromEdge, otherFromEdge, onRing, inset)) {
					standing = inset;
				}
			}
			spots.push_back({static_cast<float>(standing.x / millimetersPerMeter), static_cast<float>(standing.y / millimetersPerMeter)});
		}
		return spots;
	}

} // namespace construction
