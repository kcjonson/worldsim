#include "WallSides.h"

#include <assets/ConstructionRegistry.h>
#include <predicates/Predicates.h>

#include <glm/geometric.hpp>

#include <cmath>
#include <cstdint>
#include <numeric>
#include <unordered_set>

namespace construction {

	namespace {

		constexpr float kMillimetersPerMeter = 1000.0F;
		// How far off a centreline a point is taken to read the region on that side: well inside the 0.8 m
		// clearance walls keep from one another, so no other wall lies between.
		constexpr double kSideProbeMillimeters = 50.0;

		glm::vec2 toMeters(const geometry::Vec2i64& millimeters) {
			return {static_cast<float>(static_cast<double>(millimeters.x) / kMillimetersPerMeter),
					static_cast<float>(static_cast<double>(millimeters.y) / kMillimetersPerMeter)};
		}

		geometry::Vec2i64 toMillimeters(glm::vec2 meters) {
			return {std::llround(static_cast<double>(meters.x) * kMillimetersPerMeter),
					std::llround(static_cast<double>(meters.y) * kMillimetersPerMeter)};
		}

		float halfThicknessMeters(const WallSegment& segment) {
			const auto* preset = assets::ConstructionRegistry::Get().getThicknessPreset(segment.material, segment.thicknessPreset);
			return preset != nullptr ? static_cast<float>(preset->halfThicknessMm) / kMillimetersPerMeter : 0.0F;
		}

		// Whether the segment's two ends are joined through built segments other than itself.
		bool endsJoined(const ConstructionWorld& world, const WallSegment& segment) {
			std::vector<VertexId>		 frontier{segment.v0};
			std::unordered_set<VertexId> reached{segment.v0};
			while (!frontier.empty()) {
				const VertexId at = frontier.back();
				frontier.pop_back();
				const Vertex* vertex = world.getVertex(at);
				if (vertex == nullptr) {
					continue;
				}
				for (const SegmentId id : vertex->segments) {
					const WallSegment* next = world.getSegment(id);
					if (id == segment.id || next == nullptr || !isBuilt(*next)) {
						continue;
					}
					const VertexId other = next->v0 == at ? next->v1 : next->v0;
					if (other == segment.v1) {
						return true;
					}
					if (reached.insert(other).second) {
						frontier.push_back(other);
					}
				}
			}
			return false;
		}

		// A point kSideProbeMillimeters off the centreline from `from` to `to` at `fraction` of its length, to
		// the left of from-to (+90 degrees) or to the right.
		geometry::Vec2i64 sideProbe(const geometry::Vec2i64& from, const geometry::Vec2i64& to, double fraction, bool left) {
			const double alongX = static_cast<double>(to.x - from.x);
			const double alongY = static_cast<double>(to.y - from.y);
			const double scale = (left ? kSideProbeMillimeters : -kSideProbeMillimeters) / std::hypot(alongX, alongY);
			return {from.x + std::llround(alongX * fraction - alongY * scale), from.y + std::llround(alongY * fraction + alongX * scale)};
		}

		// The room `point` lies in, or rooms.size() for open ground.
		std::size_t regionAt(const std::vector<RoomFace>& rooms, const geometry::Vec2i64& point) {
			for (std::size_t room = 0; room < rooms.size(); ++room) {
				if (pointInRoom(point, rooms[room].ring, rooms[room].holes) == geometry::PointInPolygon::Inside) {
					return room;
				}
			}
			return rooms.size();
		}

		std::size_t rootOf(std::vector<std::size_t>& parent, std::size_t region) {
			while (parent[region] != region) {
				parent[region] = parent[parent[region]];
				region = parent[region];
			}
			return region;
		}

	} // namespace

	std::optional<WallBand> wallBand(const ConstructionWorld& world, const WallSegment& segment) {
		const Vertex* first = world.getVertex(segment.v0);
		const Vertex* second = world.getVertex(segment.v1);
		const float	  halfThickness = halfThicknessMeters(segment);
		if (first == nullptr || second == nullptr || halfThickness <= 0.0F) {
			return std::nullopt;
		}
		return WallBand{.start = toMeters(first->pos), .end = toMeters(second->pos), .halfThicknessMeters = halfThickness};
	}

	WallClosure::WallClosure(const ConstructionWorld& world, SegmentId segmentId) {
		const WallSegment* segment = world.getSegment(segmentId);
		const Vertex*	   first = segment != nullptr ? world.getVertex(segment->v0) : nullptr;
		const Vertex*	   second = segment != nullptr ? world.getVertex(segment->v1) : nullptr;
		if (first == nullptr || second == nullptr) {
			return;
		}
		const glm::vec2 start = toMeters(first->pos);
		const glm::vec2 end = toMeters(second->pos);
		const glm::vec2 direction = glm::normalize(end - start);
		const glm::vec2 left{-direction.y, direction.x};
		const float		halfThickness = halfThicknessMeters(*segment);
		const float		halfLength = glm::distance(start, end) * 0.5F;
		const WallStand leftStand{.midpoint = (start + end) * 0.5F, .outward = left, .halfThicknessMeters = halfThickness, .halfLengthMeters = halfLength};
		const WallStand rightStand{.midpoint = leftStand.midpoint, .outward = -left, .halfThicknessMeters = halfThickness, .halfLengthMeters = halfLength};
		sides = {leftStand, rightStand};
		if (!endsJoined(world, *segment)) {
			return;
		}

		// The regions once the segment is built: each room, and open ground last. Built doors join the
		// regions either side of them.
		rooms = detectRooms(world, segmentId);
		regionRoots.resize(rooms.size() + 1);
		std::iota(regionRoots.begin(), regionRoots.end(), std::size_t{0});
		const auto& registry = assets::ConstructionRegistry::Get();
		for (const Opening& opening : world.openings()) {
			const auto*		   type = registry.getOpeningType(opening.type);
			const WallSegment* host = world.getSegment(opening.segment);
			if (opening.state != FoundationState::Built || type == nullptr || !type->pathable || host == nullptr) {
				continue;
			}
			const Vertex* hostFirst = world.getVertex(host->v0);
			const Vertex* hostSecond = world.getVertex(host->v1);
			if (hostFirst == nullptr || hostSecond == nullptr) {
				continue;
			}
			const std::size_t one = rootOf(regionRoots, regionAt(rooms, sideProbe(hostFirst->pos, hostSecond->pos, opening.t, true)));
			const std::size_t other = rootOf(regionRoots, regionAt(rooms, sideProbe(hostFirst->pos, hostSecond->pos, opening.t, false)));
			regionRoots[one] = other;
		}
		for (std::size_t region = 0; region < regionRoots.size(); ++region) {
			regionRoots[region] = rootOf(regionRoots, region);
		}
		const std::size_t open = regionRoots[rooms.size()];
		const std::size_t leftRoot = regionRoots[regionAt(rooms, sideProbe(first->pos, second->pos, 0.5, true))];
		const std::size_t rightRoot = regionRoots[regionAt(rooms, sideProbe(first->pos, second->pos, 0.5, false))];
		oneSideOut = (leftRoot == open) != (rightRoot == open);
		if (!oneSideOut) {
			return;
		}
		shutRoot = leftRoot == open ? rightRoot : leftRoot;
		WallStand outside = leftRoot == open ? leftStand : rightStand;
		for (std::size_t room = 0; room < rooms.size(); ++room) {
			if (regionRoots[room] == shutRoot) {
				outside.shutRegion.push_back({.ring = rooms[room].ring, .holes = rooms[room].holes});
			}
		}
		sides = {std::move(outside)};
	}

	bool WallClosure::shutsIn(glm::vec2 position) const {
		return oneSideOut && regionRoots[regionAt(rooms, toMillimeters(position))] == shutRoot;
	}

	std::vector<WallStand> wallStands(const ConstructionWorld& world, SegmentId segment) {
		return WallClosure(world, segment).stands();
	}

} // namespace construction
