#include "OpeningGeometry.h"

#include <assets/ConstructionRegistry.h>
#include <offset/WallOffset.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_set>
#include <utility>
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

		// A wall as the renderer feeds it to the offsetter, or nothing for a wall it
		// leaves out (a missing vertex, no thickness).
		std::optional<geometry::WallSegment> drawnWall(const ConstructionWorld& world, const WallSegment& wall) {
			const Vertex*	   v0 = world.getVertex(wall.v0);
			const Vertex*	   v1 = world.getVertex(wall.v1);
			const std::int64_t half = halfThicknessMm(wall);
			if (v0 == nullptr || v1 == nullptr || half <= 0) {
				return std::nullopt;
			}
			return geometry::WallSegment{v0->pos, v1->pos, half};
		}

		// The trims of `wall`'s band at its v0 and v1 ends as the renderer draws it:
		// resolveWallBands over the connected wall graph holding it, every drawn wall,
		// blueprint or built, chained through shared vertices. Vertices are merged by
		// exact position, so this is the group geometry::connectedWallGraphs gives the
		// renderer. A wall with no thickness has no band to cut back, so no trims;
		// nothing for a graph the offsetter rejects.
		std::optional<std::pair<std::int64_t, std::int64_t>> drawnTrims(const ConstructionWorld& world, const WallSegment& wall) {
			const std::optional<geometry::WallSegment> own = drawnWall(world, wall);
			if (!own) {
				return std::pair<std::int64_t, std::int64_t>{0, 0};
			}
			std::vector<geometry::WallSegment>	 graph{*own};
			std::vector<const WallSegment*>		 members{&wall};
			std::unordered_set<SegmentId>		 reached{wall.id};
			for (std::size_t next = 0; next < members.size(); ++next) {
				for (const VertexId end : {members[next]->v0, members[next]->v1}) {
					for (const SegmentId id : world.getVertex(end)->segments) {
						if (reached.contains(id)) {
							continue;
						}
						reached.insert(id);
						const WallSegment* other = world.getSegment(id);
						if (other == nullptr) {
							continue;
						}
						if (const std::optional<geometry::WallSegment> drawn = drawnWall(world, *other)) {
							graph.push_back(*drawn);
							members.push_back(other);
						}
					}
				}
			}
			const geometry::WallBands bands = geometry::resolveWallBands(graph, geometry::kDefaultMiterLimit);
			if (bands.status != geometry::OffsetStatus::Ok || bands.trimAtA.empty() || bands.trimAtB.empty()) {
				return std::nullopt;
			}
			return std::pair{bands.trimAtA.front(), bands.trimAtB.front()};
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
		const std::optional<std::pair<std::int64_t, std::int64_t>> trims = drawnTrims(world, *wall);
		if (!trims) {
			return std::nullopt;
		}
		const auto	 mmPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);
		OpeningRange range;
		range.lengthMeters = lengthMm / mmPerMeter;
		range.trimAtV0Meters = static_cast<double>(trims->first) / mmPerMeter;
		range.trimAtV1Meters = static_cast<double>(trims->second) / mmPerMeter;
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
