#include "OpeningGeometry.h"

#include <assets/ConstructionRegistry.h>
#include <offset/WallOffset.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace construction {

	namespace {
		using Trims = std::pair<std::int64_t, std::int64_t>;

		// Segment length in mm between its two vertices, 0 if a vertex is missing.
		double segmentLengthMm(const Vertex* v0, const Vertex* v1) {
			if (v0 == nullptr || v1 == nullptr) {
				return 0.0;
			}
			const double dx = static_cast<double>(v1->pos.x - v0->pos.x);
			const double dy = static_cast<double>(v1->pos.y - v0->pos.y);
			return std::sqrt(dx * dx + dy * dy);
		}

		// The world's walls and vertices by id, one pass over each, so walking a wall graph
		// does not scan the world again for every wall and vertex it reaches.
		struct WallIndex {
			std::unordered_map<SegmentId, const WallSegment*> segments;
			std::unordered_map<VertexId, const Vertex*>		  vertices;

			explicit WallIndex(const ConstructionWorld& world) {
				segments.reserve(world.segments().size());
				for (const WallSegment& wall : world.segments()) {
					segments.emplace(wall.id, &wall);
				}
				vertices.reserve(world.vertices().size());
				for (const Vertex& vertex : world.vertices()) {
					vertices.emplace(vertex.id, &vertex);
				}
			}

			[[nodiscard]] const WallSegment* segment(SegmentId id) const {
				const auto found = segments.find(id);
				return found == segments.end() ? nullptr : found->second;
			}
			[[nodiscard]] const Vertex* vertex(VertexId id) const {
				const auto found = vertices.find(id);
				return found == vertices.end() ? nullptr : found->second;
			}
		};

		// A wall as the renderer feeds it to the offsetter, or nothing for a wall it
		// leaves out (a missing vertex, no thickness).
		std::optional<geometry::WallSegment> drawnWall(const WallIndex& index, const WallSegment& wall) {
			const Vertex*	   v0 = index.vertex(wall.v0);
			const Vertex*	   v1 = index.vertex(wall.v1);
			const std::int64_t half = halfThicknessMm(wall);
			if (v0 == nullptr || v1 == nullptr || half <= 0) {
				return std::nullopt;
			}
			return geometry::WallSegment{v0->pos, v1->pos, half};
		}

		struct WallGraph {
			std::vector<geometry::WallSegment> walls; // `wall` first
			bool							   othersBuilt = true;
		};

		// The connected wall graph holding `wall` (which has a band), its walls chained
		// through shared vertices: every drawn wall, or with `builtOnly` the built ones and
		// `wall` itself. Vertices are merged by exact position, so this is the group
		// geometry::connectedWallGraphs gives the renderer and nav.
		WallGraph wallGraph(const WallIndex& index, const WallSegment& wall, geometry::WallSegment own, bool builtOnly) {
			WallGraph						graph{{own}};
			std::vector<const WallSegment*> members{&wall};
			std::unordered_set<SegmentId>	reached{wall.id};
			for (std::size_t next = 0; next < members.size(); ++next) {
				for (const VertexId end : {members[next]->v0, members[next]->v1}) {
					for (const SegmentId id : index.vertex(end)->segments) {
						if (reached.contains(id)) {
							continue;
						}
						reached.insert(id);
						const WallSegment* other = index.segment(id);
						if (other == nullptr || (builtOnly && !isBuilt(*other))) {
							continue;
						}
						if (const std::optional<geometry::WallSegment> drawn = drawnWall(index, *other)) {
							graph.walls.push_back(*drawn);
							graph.othersBuilt = graph.othersBuilt && isBuilt(*other);
							members.push_back(other);
						}
					}
				}
			}
			return graph;
		}

		// The first wall's trims at its a and b ends, nothing for a graph the offsetter rejects.
		std::optional<Trims> firstWallTrims(const std::vector<geometry::WallSegment>& walls) {
			const geometry::WallBands bands = geometry::resolveWallBands(walls, geometry::kDefaultMiterLimit);
			if (bands.status != geometry::OffsetStatus::Ok || bands.trimAtA.empty() || bands.trimAtB.empty()) {
				return std::nullopt;
			}
			return Trims{bands.trimAtA.front(), bands.trimAtB.front()};
		}

		// The trims of `wall`'s band at its v0 and v1 ends that openingRange measures from
		// (OpeningGeometry.h): the larger of the drawn graph's and nav's built graph's.
		std::optional<Trims> openingTrims(const WallIndex& index, const WallSegment& wall) {
			const std::optional<geometry::WallSegment> own = drawnWall(index, wall);
			if (!own) {
				return Trims{0, 0};
			}
			const WallGraph				drawn = wallGraph(index, wall, *own, false);
			const std::optional<Trims> drawnTrims = firstWallTrims(drawn.walls);
			if (!drawnTrims || drawn.othersBuilt) {
				return drawnTrims;
			}
			const std::optional<Trims> builtTrims = firstWallTrims(wallGraph(index, wall, *own, true).walls);
			if (!builtTrims) {
				return std::nullopt;
			}
			return Trims{std::max(drawnTrims->first, builtTrims->first), std::max(drawnTrims->second, builtTrims->second)};
		}
	} // namespace

	std::int64_t halfThicknessMm(const WallSegment& wall) {
		const auto* preset = assets::ConstructionRegistry::Get().getThicknessPreset(wall.material, wall.thicknessPreset);
		return preset != nullptr ? preset->halfThicknessMm : 0;
	}

	std::optional<OpeningRange>
	openingRange(const ConstructionWorld& world, SegmentId segment, double widthMeters, double marginMeters) {
		const WallIndex	   index(world);
		const WallSegment* wall = index.segment(segment);
		if (wall == nullptr) {
			return std::nullopt;
		}
		const double lengthMm = segmentLengthMm(index.vertex(wall->v0), index.vertex(wall->v1));
		if (lengthMm <= 0.0) {
			return std::nullopt;
		}
		const std::optional<Trims> trims = openingTrims(index, *wall);
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
		const double lengthMm = segmentLengthMm(v0, v1);
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

	namespace {
		// The opening's span on its wall `lengthMm` long into `gaps`, when the filter reads it.
		void addGap(const Opening& opening, double lengthMm, OpeningGapFilter filter, OpeningGaps& gaps) {
			if (filter.builtOnly && opening.state != FoundationState::Built) {
				return;
			}
			const auto* type = assets::ConstructionRegistry::Get().getOpeningType(opening.type);
			if (type == nullptr || type->widthMm <= 0 || (filter.doorsOnly && !type->pathable)) {
				return;
			}
			const double centre = static_cast<double>(opening.t) * lengthMm;
			const double half	= static_cast<double>(type->widthMm) * 0.5;
			gaps.gaps.push_back({std::llround(centre - half), std::llround(centre + half)});
			gaps.openings.push_back(opening.id);
		}

		double lengthOf(const ConstructionWorld& world, const WallSegment& segment) {
			const Vertex* v0 = world.getVertex(segment.v0);
			const Vertex* v1 = world.getVertex(segment.v1);
			return std::hypot(static_cast<double>(v1->pos.x - v0->pos.x), static_cast<double>(v1->pos.y - v0->pos.y));
		}
	} // namespace

	OpeningGaps openingGaps(const ConstructionWorld& world, const WallSegment& segment, OpeningGapFilter filter) {
		OpeningGaps result;
		if (world.getVertex(segment.v0) == nullptr || world.getVertex(segment.v1) == nullptr) {
			return result;
		}
		const double lengthMm = lengthOf(world, segment);
		for (const Opening& opening : world.openings()) {
			if (opening.segment == segment.id) {
				addGap(opening, lengthMm, filter, result);
			}
		}
		return result;
	}

	std::unordered_map<SegmentId, OpeningGaps> openingGapsBySegment(const ConstructionWorld& world, OpeningGapFilter filter) {
		std::unordered_map<SegmentId, OpeningGaps> result;
		for (const Opening& opening : world.openings()) {
			const WallSegment* segment = world.getSegment(opening.segment);
			if (segment == nullptr || world.getVertex(segment->v0) == nullptr || world.getVertex(segment->v1) == nullptr) {
				continue;
			}
			addGap(opening, lengthOf(world, *segment), filter, result[segment->id]);
		}
		return result;
	}

} // namespace construction
