#include "assets/ItemOverlays.h"

#include "assets/PointTurn.h"
#include "assets/TurnQuanta.h"

#include <utils/Log.h>
#include <vector/Tessellator.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <numbers>

namespace assets {

	namespace {

		std::vector<glm::vec2> turnedRing(const std::vector<glm::vec2>& ring, float radians) {
			std::vector<glm::vec2> turned;
			turned.reserve(ring.size());
			for (const glm::vec2& point : ring) {
				turned.push_back(turnedPoint(point, radians));
			}
			return turned;
		}

		double ringArea(const std::vector<glm::vec2>& ring) {
			double twice = 0.0;
			for (size_t i = 0; i < ring.size(); ++i) {
				const glm::vec2& from = ring[i];
				const glm::vec2& to = ring[(i + 1) % ring.size()];
				twice += static_cast<double>(from.x) * to.y - static_cast<double>(to.x) * from.y;
			}
			return std::abs(twice) * 0.5;
		}

		/// Where a line crosses a polygon's rings, as a parameter along it, sorted. `side(point)` is the point's signed
		/// offset from the line; `along(from, to, t)` the parameter of the crossing at fraction t of an edge.
		template <typename Side, typename Along>
		void crossingsOf(const AreaPolygon& polygon, Side side, Along along, std::vector<double>& crossings) {
			crossings.clear();
			const auto ringCrossings = [&](const std::vector<glm::vec2>& ring) {
				for (size_t i = 0; i < ring.size(); ++i) {
					const glm::vec2& from = ring[i];
					const glm::vec2& to = ring[(i + 1) % ring.size()];
					const double	 fromSide = side(from);
					const double	 toSide = side(to);
					// Half-open, so a line through a vertex crosses one of its two edges, never both or neither.
					if ((fromSide > 0.0) == (toSide > 0.0)) {
						continue;
					}
					crossings.push_back(along(from, to, fromSide / (fromSide - toSide)));
				}
			};
			ringCrossings(polygon.outer);
			for (const std::vector<glm::vec2>& hole : polygon.holes) {
				ringCrossings(hole);
			}
			std::sort(crossings.begin(), crossings.end());
		}

		OverlayArea turnedArea(const ItemArea& area, float radians) {
			OverlayArea result{.name = area.name, .polygons = {}, .fill = {}, .labelPoint = glm::vec2{0.0F}};
			result.polygons.reserve(area.polygons.size());
			std::vector<renderer::VectorPath> contours;
			size_t							  largest = 0;
			double							  largestArea = -1.0;
			for (const AreaPolygon& polygon : area.polygons) {
				AreaPolygon& turned = result.polygons.emplace_back();
				turned.outer = turnedRing(polygon.outer, radians);
				for (const std::vector<glm::vec2>& hole : polygon.holes) {
					turned.holes.push_back(turnedRing(hole, radians));
				}
				const auto contour = [&contours](const std::vector<glm::vec2>& ring) {
					renderer::VectorPath& path = contours.emplace_back();
					for (const glm::vec2& point : ring) {
						path.addVertex({point.x, point.y});
					}
				};
				contour(turned.outer);
				for (const std::vector<glm::vec2>& hole : turned.holes) {
					contour(hole);
				}
				if (const double size = ringArea(turned.outer); size > largestArea) {
					largestArea = size;
					largest = result.polygons.size() - 1;
				}
			}
			if (!contours.empty()) {
				renderer::Tessellator tessellator;
				if (!tessellator.Tessellate(std::span<const renderer::VectorPath>(contours), result.fill)) {
					LOG_ERROR(Engine, "Item overlays: area %s didn't tessellate", area.name.c_str());
					result.fill.clear();
				}
			}
			if (!result.polygons.empty()) {
				result.labelPoint = pointInside(result.polygons[largest]);
			}
			return result;
		}

	} // namespace

	glm::vec2 pointInside(const AreaPolygon& polygon) {
		if (polygon.outer.empty()) {
			return glm::vec2{0.0F};
		}
		float lowY = polygon.outer.front().y;
		float highY = lowY;
		for (const glm::vec2& point : polygon.outer) {
			lowY = std::min(lowY, point.y);
			highY = std::max(highY, point.y);
		}
		// A few heights about the middle, none through a vertex: a line along a horizontal edge would put the span's
		// middle on the boundary. The middle of a span crossing at no vertex lies strictly inside.
		const auto onVertex = [&polygon](double y) {
			const auto ringHas = [y](const std::vector<glm::vec2>& ring) {
				return std::any_of(ring.begin(), ring.end(), [y](glm::vec2 point) { return static_cast<double>(point.y) == y; });
			};
			return ringHas(polygon.outer) || std::any_of(polygon.holes.begin(), polygon.holes.end(), ringHas);
		};
		const double		middleY = (static_cast<double>(lowY) + highY) * 0.5;
		const double		height = static_cast<double>(highY) - lowY;
		std::vector<double> crossings;
		double				bestFrom = polygon.outer.front().x;
		double				bestTo = bestFrom;
		double				bestY = middleY;
		for (const double offset : {0.0, 0.07, -0.07, 0.14, -0.14, 0.21, -0.21}) {
			const double y = middleY + offset * height;
			if (onVertex(y)) {
				continue;
			}
			crossingsOf(
				polygon,
				[y](glm::vec2 point) { return static_cast<double>(point.y) - y; },
				[](glm::vec2 from, glm::vec2 to, double fraction) { return static_cast<double>(from.x) + fraction * (static_cast<double>(to.x) - from.x); },
				crossings
			);
			for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
				if (crossings[i + 1] - crossings[i] > bestTo - bestFrom) {
					bestFrom = crossings[i];
					bestTo = crossings[i + 1];
					bestY = y;
				}
			}
		}
		return {static_cast<float>((bestFrom + bestTo) * 0.5), static_cast<float>(bestY)};
	}

	std::vector<HatchLine> hatchLines(const std::vector<AreaPolygon>& polygons, float spacingMeters) {
		std::vector<HatchLine> lines;
		if (!(spacingMeters > 0.0F)) {
			return lines;
		}
		std::vector<double> crossings;
		for (const AreaPolygon& polygon : polygons) {
			if (polygon.outer.empty()) {
				continue;
			}
			double lowest = std::numeric_limits<double>::max();
			double highest = std::numeric_limits<double>::lowest();
			for (const glm::vec2& point : polygon.outer) {
				lowest = std::min(lowest, static_cast<double>(point.x) - point.y);
				highest = std::max(highest, static_cast<double>(point.x) - point.y);
			}
			// Lines x - y = c whose c are step apart lie spacingMeters apart, perpendicular to them; a point on one is
			// (c + s, s) for the parameter s, its y.
			double step = static_cast<double>(spacingMeters) * std::numbers::sqrt2;
			step = std::max(step, (highest - lowest) / static_cast<double>(kMaxHatchLinesPerPolygon));
			const auto piecesAt = [&](double offset, int64_t index) {
				crossingsOf(
					polygon,
					[offset](glm::vec2 point) { return static_cast<double>(point.x) - point.y - offset; },
					[](glm::vec2 from, glm::vec2 to, double fraction) { return static_cast<double>(from.y) + fraction * (static_cast<double>(to.y) - from.y); },
					crossings
				);
				for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
					if (crossings[i + 1] - crossings[i] <= 0.0) {
						continue;
					}
					lines.push_back(
						{.from = {static_cast<float>(offset + crossings[i]), static_cast<float>(crossings[i])},
						 .to = {static_cast<float>(offset + crossings[i + 1]), static_cast<float>(crossings[i + 1])},
						 .index = index}
					);
				}
			};
			const size_t  first = lines.size();
			const int64_t lowIndex = static_cast<int64_t>(std::ceil(lowest / step));
			const int64_t highIndex = static_cast<int64_t>(std::floor(highest / step));
			for (int64_t index = lowIndex; index <= highIndex; ++index) {
				piecesAt(static_cast<double>(index) * step, index);
			}
			if (lines.size() == first) {
				// Too thin across the lines to meet one: the line through its middle.
				piecesAt((lowest + highest) * 0.5, 0);
			}
			if (lines.size() > first) {
				const double middle = (lowest + highest) * 0.5 / step;
				auto		 nearest = std::min_element(lines.begin() + static_cast<std::ptrdiff_t>(first), lines.end(), [middle](const HatchLine& left, const HatchLine& right) {
					return std::abs(static_cast<double>(left.index) - middle) < std::abs(static_cast<double>(right.index) - middle);
				});
				nearest->middle = true;
			}
		}
		return lines;
	}

	ItemOverlays itemOverlays(const AssetDefinition& def, uint32_t turnQuanta, float hatchSpacingMeters) {
		ItemOverlays overlays{.turnQuanta = turnQuanta, .hatchSpacingMeters = hatchSpacingMeters};
		if (def.parts == nullptr) {
			return overlays;
		}
		const PartsAsset& parts = *def.parts;
		const float		  radians = turnRadiansFromQuanta(turnQuanta);
		for (const ItemArea& area : parts.useAreas) {
			overlays.useAreas.push_back(turnedArea(area, radians));
		}
		overlays.clearance = turnedArea(parts.clearance, radians);
		overlays.clearanceHatch = hatchLines(overlays.clearance.polygons, hatchSpacingMeters);
		for (size_t placement = 0; placement < parts.placements.size(); ++placement) {
			overlays.footprints.push_back(turnedFootprint(parts, placement, radians));
		}
		for (const CollisionShape& ring : def.partsColliders) {
			overlays.collider.push_back(turnedRing(ring.pointsMeters, radians));
		}
		return overlays;
	}

} // namespace assets
