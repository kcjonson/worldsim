#include "assets/ItemOverlays.h"

#include "assets/PointTurn.h"
#include "assets/TurnQuanta.h"

#include <utils/Log.h>
#include <vector/Tessellator.h>

#include <algorithm>
#include <cmath>
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

		OverlayArea turnedArea(const ItemArea& area, float radians) {
			OverlayArea result{.name = area.name, .polygons = {}, .fill = {}, .labelPoint = glm::vec2{0.0F}};
			std::vector<renderer::VectorPath> contours;
			const std::vector<glm::vec2>*	  largest = nullptr;
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
					largest = &turned.outer;
				}
			}
			if (!contours.empty()) {
				renderer::Tessellator tessellator;
				if (!tessellator.Tessellate(std::span<const renderer::VectorPath>(contours), result.fill)) {
					LOG_ERROR(Engine, "Item overlays: area %s didn't tessellate", area.name.c_str());
					result.fill.clear();
				}
			}
			if (largest != nullptr && !largest->empty()) {
				glm::vec2 low = largest->front();
				glm::vec2 high = low;
				for (const glm::vec2& point : *largest) {
					low = glm::min(low, point);
					high = glm::max(high, point);
				}
				result.labelPoint = (low + high) * 0.5F;
			}
			return result;
		}

	} // namespace

	std::vector<std::array<glm::vec2, 2>> hatchLines(const std::vector<AreaPolygon>& polygons, float spacingMeters) {
		std::vector<std::array<glm::vec2, 2>> lines;
		std::vector<const std::vector<glm::vec2>*> rings;
		float lowest = std::numeric_limits<float>::max();
		float highest = std::numeric_limits<float>::lowest();
		for (const AreaPolygon& polygon : polygons) {
			rings.push_back(&polygon.outer);
			for (const std::vector<glm::vec2>& hole : polygon.holes) {
				rings.push_back(&hole);
			}
			for (const glm::vec2& point : polygon.outer) {
				lowest = std::min(lowest, point.x - point.y);
				highest = std::max(highest, point.x - point.y);
			}
		}
		if (rings.empty() || !(spacingMeters > 0.0F)) {
			return lines;
		}
		// Along a line x - y = c, a point is (c + s, s) / 1 for the parameter s = y; lines of c apart by step are
		// spacingMeters apart perpendicular to them.
		const double		step = static_cast<double>(spacingMeters) * std::numbers::sqrt2;
		std::vector<double> crossings;
		for (double offset = std::ceil(lowest / step) * step; offset <= highest; offset += step) {
			crossings.clear();
			for (const std::vector<glm::vec2>* ring : rings) {
				for (size_t i = 0; i < ring->size(); ++i) {
					const glm::vec2& from = (*ring)[i];
					const glm::vec2& to = (*ring)[(i + 1) % ring->size()];
					const double	 fromSide = static_cast<double>(from.x) - from.y - offset;
					const double	 toSide = static_cast<double>(to.x) - to.y - offset;
					// Half-open, so a line through a vertex crosses one of its two edges, never both or neither.
					if ((fromSide > 0.0) == (toSide > 0.0)) {
						continue;
					}
					const double along = fromSide / (fromSide - toSide);
					crossings.push_back(static_cast<double>(from.y) + along * (static_cast<double>(to.y) - from.y));
				}
			}
			std::sort(crossings.begin(), crossings.end());
			for (size_t i = 0; i + 1 < crossings.size(); i += 2) {
				if (crossings[i + 1] - crossings[i] <= 0.0) {
					continue;
				}
				lines.push_back(
					{glm::vec2{static_cast<float>(offset + crossings[i]), static_cast<float>(crossings[i])},
					 glm::vec2{static_cast<float>(offset + crossings[i + 1]), static_cast<float>(crossings[i + 1])}}
				);
			}
		}
		return lines;
	}

	ItemOverlays itemOverlays(const AssetDefinition& def, uint32_t turnQuanta) {
		ItemOverlays overlays{.turnQuanta = turnQuanta};
		if (def.parts == nullptr) {
			return overlays;
		}
		const PartsAsset& parts = *def.parts;
		const float		  radians = turnRadiansFromQuanta(turnQuanta);
		for (const ItemArea& area : parts.useAreas) {
			overlays.useAreas.push_back(turnedArea(area, radians));
		}
		overlays.clearance = turnedArea(parts.clearance, radians);
		overlays.clearanceHatch = hatchLines(overlays.clearance.polygons, kHatchSpacingMeters);
		for (size_t placement = 0; placement < parts.placements.size(); ++placement) {
			overlays.footprints.push_back(turnedFootprint(parts, placement, radians));
		}
		for (const CollisionShape& ring : def.partsColliders) {
			overlays.collider.push_back(turnedRing(ring.pointsMeters, radians));
		}
		return overlays;
	}

} // namespace assets
