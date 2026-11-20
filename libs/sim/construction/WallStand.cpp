#include "WallStand.h"

#include "RoomDetection.h"

#include <assets/ConstructionRegistry.h>
#include <predicates/Predicates.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <unordered_set>

namespace construction {

	namespace {

		constexpr float kMillimetersPerMeter = 1000.0F;
		// A stepped-out disc lands this far past the band's edge, so rounding never leaves it touching.
		constexpr float kStepOutSlackMeters = 0.001F;
		// How far off a centreline a point is taken to read the region on that side: well inside the 0.8 m
		// clearance walls keep from one another, so no other wall lies between.
		constexpr double kSideProbeMm = 50.0;

		glm::vec2 toMeters(const geometry::Vec2i64& millimeters) {
			return {static_cast<float>(static_cast<double>(millimeters.x) / kMillimetersPerMeter),
					static_cast<float>(static_cast<double>(millimeters.y) / kMillimetersPerMeter)};
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

		// A point kSideProbeMm off the centreline from `a` to `b` at parameter `t`, to the left of a to b
		// (+90 degrees) or to the right.
		geometry::Vec2i64 sideProbe(const geometry::Vec2i64& a, const geometry::Vec2i64& b, double t, bool left) {
			const double dx = static_cast<double>(b.x - a.x);
			const double dy = static_cast<double>(b.y - a.y);
			const double scale = (left ? kSideProbeMm : -kSideProbeMm) / std::hypot(dx, dy);
			return {a.x + std::llround(dx * t - dy * scale), a.y + std::llround(dy * t + dx * scale)};
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

	glm::vec2 WallBand::closestPoint(glm::vec2 position) const {
		const glm::vec2 along = end - start;
		const float		lengthSquared = glm::dot(along, along);
		if (lengthSquared <= 0.0F) {
			return start;
		}
		return start + along * std::clamp(glm::dot(position - start, along) / lengthSquared, 0.0F, 1.0F);
	}

	bool WallBand::overlaps(glm::vec2 position, float radiusMeters) const {
		return glm::distance(position, closestPoint(position)) < halfThicknessMeters + radiusMeters;
	}

	glm::vec2 WallBand::stepOut(glm::vec2 position, float radiusMeters, glm::vec2 outward) const {
		return closestPoint(position) + outward * (halfThicknessMeters + radiusMeters + kStepOutSlackMeters);
	}

	std::optional<WallBand> wallBand(const ConstructionWorld& world, const WallSegment& segment) {
		const Vertex* first = world.getVertex(segment.v0);
		const Vertex* second = world.getVertex(segment.v1);
		const float	  halfThickness = halfThicknessMeters(segment);
		if (first == nullptr || second == nullptr || halfThickness <= 0.0F) {
			return std::nullopt;
		}
		return WallBand{.start = toMeters(first->pos), .end = toMeters(second->pos), .halfThicknessMeters = halfThickness};
	}

	glm::vec2 WallStand::standPoint(float agentRadiusMeters) const {
		return midpoint + outward * (halfThicknessMeters + agentRadiusMeters + kStandMarginMeters);
	}

	bool WallStand::onSide(glm::vec2 position) const {
		return glm::dot(position - midpoint, outward) > 0.0F;
	}

	std::vector<WallStand> wallStands(const ConstructionWorld& world, SegmentId segmentId) {
		const WallSegment* segment = world.getSegment(segmentId);
		const Vertex*	   first = segment != nullptr ? world.getVertex(segment->v0) : nullptr;
		const Vertex*	   second = segment != nullptr ? world.getVertex(segment->v1) : nullptr;
		if (first == nullptr || second == nullptr) {
			return {};
		}
		const glm::vec2 start = toMeters(first->pos);
		const glm::vec2 end = toMeters(second->pos);
		const glm::vec2 direction = glm::normalize(end - start);
		const glm::vec2 left{-direction.y, direction.x};
		const float		halfThickness = halfThicknessMeters(*segment);
		const WallStand leftStand{.midpoint = (start + end) * 0.5F, .outward = left, .halfThicknessMeters = halfThickness};
		const WallStand rightStand{.midpoint = leftStand.midpoint, .outward = -left, .halfThicknessMeters = halfThickness};
		if (!endsJoined(world, *segment)) {
			return {leftStand, rightStand};
		}

		// The regions once the segment is built: each room, and open ground last. Built doors join the
		// regions either side of them.
		const std::vector<RoomFace> rooms = detectRooms(world, segmentId);
		std::vector<std::size_t>	parent(rooms.size() + 1);
		std::iota(parent.begin(), parent.end(), std::size_t{0});
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
			const std::size_t one = rootOf(parent, regionAt(rooms, sideProbe(hostFirst->pos, hostSecond->pos, opening.t, true)));
			const std::size_t other = rootOf(parent, regionAt(rooms, sideProbe(hostFirst->pos, hostSecond->pos, opening.t, false)));
			parent[one] = other;
		}
		const std::size_t open = rootOf(parent, rooms.size());
		const bool		  leftOut = rootOf(parent, regionAt(rooms, sideProbe(first->pos, second->pos, 0.5, true))) == open;
		const bool		  rightOut = rootOf(parent, regionAt(rooms, sideProbe(first->pos, second->pos, 0.5, false))) == open;
		if (leftOut == rightOut) {
			return {leftStand, rightStand};
		}
		return {leftOut ? leftStand : rightStand};
	}

} // namespace construction
