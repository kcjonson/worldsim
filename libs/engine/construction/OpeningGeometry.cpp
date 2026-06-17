#include "OpeningGeometry.h"

#include <assets/ConstructionRegistry.h>
#include <offset/WallOffset.h>

#include <algorithm>
#include <cmath>

namespace engine::construction {

	namespace {
		// Segment length in mm from its two vertex positions, 0 if a vertex is missing.
		double segmentLengthMm(const ConstructionWorld& world, const WallSegment& seg) {
			const Vertex* v0 = world.getVertex(seg.v0);
			const Vertex* v1 = world.getVertex(seg.v1);
			if (v0 == nullptr || v1 == nullptr) {
				return 0.0;
			}
			const double dx = static_cast<double>(v1->pos.x - v0->pos.x);
			const double dy = static_cast<double>(v1->pos.y - v0->pos.y);
			return std::sqrt(dx * dx + dy * dy);
		}
	} // namespace

	geometry::Ring openingFootprint(const ConstructionWorld& world, const Opening& opening) {
		const WallSegment* seg = world.getSegment(opening.segment);
		if (seg == nullptr) {
			return {};
		}
		const Vertex* v0 = world.getVertex(seg->v0);
		const Vertex* v1 = world.getVertex(seg->v1);
		if (v0 == nullptr || v1 == nullptr) {
			return {};
		}
		const auto* type = engine::assets::ConstructionRegistry::Get().getOpeningType(opening.type);
		if (type == nullptr || type->widthMm <= 0) {
			return {};
		}
		const auto*		   preset = engine::assets::ConstructionRegistry::Get().getThicknessPreset(seg->material, seg->thicknessPreset);
		const std::int64_t halfThick = (preset != nullptr) ? preset->halfThicknessMm : 0;
		if (halfThick <= 0) {
			return {};
		}
		const double lengthMm = segmentLengthMm(world, *seg);
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

} // namespace engine::construction
