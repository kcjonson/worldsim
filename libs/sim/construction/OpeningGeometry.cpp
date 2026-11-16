#include "OpeningGeometry.h"

#include <assets/ConstructionRegistry.h>
#include <offset/WallOffset.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace construction {

	namespace {
		// Segment length in mm from its two vertex positions, 0 if a vertex is missing.
		double segmentLengthMm(const ConstructionWorld& world, const WallSegment& wall) {
			const Vertex* v0 = world.getVertex(wall.v0);
			const Vertex* v1 = world.getVertex(wall.v1);
			if (v0 == nullptr || v1 == nullptr) {
				return 0.0;
			}
			const double dx = static_cast<double>(v1->pos.x - v0->pos.x);
			const double dy = static_cast<double>(v1->pos.y - v0->pos.y);
			return std::sqrt(dx * dx + dy * dy);
		}

		// How far `wall`'s band is cut back at `vertexId` for the corner or junction
		// there. Walls the renderer leaves out (no vertex, no thickness) are left out.
		std::int64_t trimAtVertex(const ConstructionWorld& world, const WallSegment& wall, VertexId vertexId) {
			const Vertex* vertex = world.getVertex(vertexId);
			if (vertex == nullptr || vertex->segments.size() < 2 || halfThicknessMm(wall) <= 0) {
				return 0;
			}
			std::vector<geometry::IncidentSegment> incidents;
			incidents.reserve(vertex->segments.size());
			std::size_t ownIndex = 0;
			for (const SegmentId id : vertex->segments) {
				const WallSegment* other = world.getSegment(id);
				if (other == nullptr) {
					continue;
				}
				const Vertex*	   farVertex = world.getVertex(other->v0 == vertexId ? other->v1 : other->v0);
				const std::int64_t half = halfThicknessMm(*other);
				if (farVertex == nullptr || half <= 0) {
					continue;
				}
				if (id == wall.id) {
					ownIndex = incidents.size();
				}
				incidents.push_back({farVertex->pos, half, incidents.size()});
			}
			const geometry::JunctionResolution junction = geometry::resolveJunction(vertex->pos, incidents, geometry::kDefaultMiterLimit);
			if (junction.status != geometry::OffsetStatus::Ok) {
				return 0;
			}
			for (const geometry::JunctionTrim& trim : junction.trims) {
				if (trim.index == ownIndex) {
					return trim.trimMm;
				}
			}
			return 0;
		}
	} // namespace

	std::int64_t halfThicknessMm(const WallSegment& wall) {
		const auto* preset = assets::ConstructionRegistry::Get().getThicknessPreset(wall.material, wall.thicknessPreset);
		return preset != nullptr ? preset->halfThicknessMm : 0;
	}

	std::optional<OpeningRange>
	openingRange(const ConstructionWorld& world, SegmentId segment, double widthMeters, double marginMeters) {
		const WallSegment* wall = world.getSegment(segment);
		if (wall == nullptr) {
			return std::nullopt;
		}
		const double lengthMm = segmentLengthMm(world, *wall);
		if (lengthMm <= 0.0) {
			return std::nullopt;
		}
		const auto	 mmPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);
		OpeningRange range;
		range.lengthMeters = lengthMm / mmPerMeter;
		range.trimAtV0Meters = static_cast<double>(trimAtVertex(world, *wall, wall->v0)) / mmPerMeter;
		range.trimAtV1Meters = static_cast<double>(trimAtVertex(world, *wall, wall->v1)) / mmPerMeter;
		const double halfWidth = widthMeters / 2.0;
		range.minT = static_cast<float>((range.trimAtV0Meters + marginMeters + halfWidth) / range.lengthMeters);
		range.maxT = static_cast<float>((range.lengthMeters - range.trimAtV1Meters - marginMeters - halfWidth) / range.lengthMeters);
		return range;
	}

	geometry::Ring openingFootprint(const ConstructionWorld& world, const Opening& opening) {
		const WallSegment* wall = world.getSegment(opening.segment);
		if (wall == nullptr) {
			return {};
		}
		const Vertex* v0 = world.getVertex(wall->v0);
		const Vertex* v1 = world.getVertex(wall->v1);
		if (v0 == nullptr || v1 == nullptr) {
			return {};
		}
		const auto* type = assets::ConstructionRegistry::Get().getOpeningType(opening.type);
		if (type == nullptr || type->widthMm <= 0) {
			return {};
		}
		const std::int64_t halfThick = halfThicknessMm(*wall);
		if (halfThick <= 0) {
			return {};
		}
		const double lengthMm = segmentLengthMm(world, *wall);
		if (lengthMm <= 0.0) {
			return {};
		}

		// Clear width maps to a centerline half-extent of (widthMm/2)/L; clamp the
		// span to [0,1] so an opening near a vertex doesn't run off the segment.
		const float half = static_cast<float>((static_cast<double>(type->widthMm) * 0.5) / lengthMm);
		const float t0 = std::clamp(opening.t - half, 0.0F, 1.0F);
		const float t1 = std::clamp(opening.t + half, 0.0F, 1.0F);
		auto		lerpMm = [&](float t) -> geometry::Vec2i64 {
			   const double ax = static_cast<double>(v0->pos.x);
			   const double ay = static_cast<double>(v0->pos.y);
			   const double bx = static_cast<double>(v1->pos.x);
			   const double by = static_cast<double>(v1->pos.y);
			   return {
				   static_cast<std::int64_t>(std::llround(ax + (bx - ax) * t)),
				   static_cast<std::int64_t>(std::llround(ay + (by - ay) * t)),
			   };
		};
		return geometry::band(lerpMm(t0), lerpMm(t1), halfThick);
	}

	OpeningGaps openingGaps(const ConstructionWorld& world, const WallSegment& segment, OpeningGapFilter filter) {
		OpeningGaps	  result;
		const Vertex* v0 = world.getVertex(segment.v0);
		const Vertex* v1 = world.getVertex(segment.v1);
		if (v0 == nullptr || v1 == nullptr) {
			return result;
		}
		const double lengthMm = std::hypot(static_cast<double>(v1->pos.x - v0->pos.x), static_cast<double>(v1->pos.y - v0->pos.y));
		for (const Opening& opening : world.openings()) {
			if (opening.segment != segment.id || (filter.builtOnly && opening.state != FoundationState::Built)) {
				continue;
			}
			const auto* type = assets::ConstructionRegistry::Get().getOpeningType(opening.type);
			if (type == nullptr || type->widthMm <= 0 || (filter.doorsOnly && !type->pathable)) {
				continue;
			}
			const double centre = static_cast<double>(opening.t) * lengthMm;
			const double half	= static_cast<double>(type->widthMm) * 0.5;
			result.gaps.push_back({std::llround(centre - half), std::llround(centre + half)});
			result.openings.push_back(opening.id);
		}
		return result;
	}

} // namespace construction
