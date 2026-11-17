#include "PlacedObstacleCollisionSystem.h"

#include "NavigationSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>

#include <nav/NavCoords.h>
#include <nav/NavInputBuilder.h>
#include <nav/PlacedObstacle.h>

#include <core/Vec2d.h>
#include <predicates/Predicates.h>

#include <utils/Log.h>

#include <world/chunk/ChunkCoordinate.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ecs {

namespace {

	constexpr double kMillimetersPerMeter = static_cast<double>(geometry::kMillimetersPerMeter);

	// How far past the boundary a push may step to find a landing whose mm rounding is outside: past
	// the half-diagonal of a mm cell (0.71 mm) with room for float position spacing far from the origin.
	constexpr double kLandingStepMm	 = 0.25;
	constexpr int	 kLandingStepsMax = 16;

	glm::vec2 metersFromMm(double xMm, double yMm) {
		return {static_cast<float>(xMm / kMillimetersPerMeter), static_cast<float>(yMm / kMillimetersPerMeter)};
	}

} // namespace

std::optional<glm::vec2> resolveCenterAgainstOutline(glm::vec2 center, const geometry::Ring& outlineMm) {
	const std::size_t count = outlineMm.size();
	if (count < 3) {
		return std::nullopt;
	}
	if (geometry::pointInPolygon(nav::toMm(center), outlineMm) != geometry::PointInPolygon::Inside) {
		return std::nullopt;
	}

	// The nearest boundary point to the centre as it stands, off the mm lattice, so the push leaves
	// the coordinate along the edge exactly where it was.
	const geometry::Vec2d centerMm{static_cast<double>(center.x) * kMillimetersPerMeter, static_cast<double>(center.y) * kMillimetersPerMeter};
	double				  bestDistance = std::numeric_limits<double>::max();
	geometry::Vec2d		  nearest;
	std::size_t			  nearestEdge = 0;
	for (std::size_t i = 0; i < count; ++i) {
		const geometry::Vec2i64&	 from	 = outlineMm[i];
		const geometry::Vec2i64&	 to		 = outlineMm[(i + 1) % count];
		const geometry::SegmentPoint closest = geometry::closestOnSegment(centerMm, from, to);
		if (closest.distanceMm < bestDistance) {
			bestDistance = closest.distanceMm;
			nearestEdge	 = i;
			nearest		 = {static_cast<double>(from.x) + static_cast<double>(to.x - from.x) * closest.t,
							static_cast<double>(from.y) + static_cast<double>(to.y - from.y) * closest.t};
		}
	}

	// Outward: from the centre to the nearest point, which at a reflex corner bisects its two edges.
	// A centre a hair inside has no usable direction there, so the nearest edge's own normal stands in,
	// turned by the ring's winding.
	geometry::Vec2d outward = nearest - centerMm;
	if (geometry::length(outward) < 1e-6) {
		const geometry::Vec2i64& from	   = outlineMm[nearestEdge];
		const geometry::Vec2i64& to		   = outlineMm[(nearestEdge + 1) % count];
		const double			 alongX	   = static_cast<double>(to.x - from.x);
		const double			 alongY	   = static_cast<double>(to.y - from.y);
		const bool				 clockwise = geometry::windingOrder(outlineMm) == geometry::Winding::Clockwise;
		outward							   = clockwise ? geometry::Vec2d{-alongY, alongX} : geometry::Vec2d{alongY, -alongX};
	}
	const double outwardLength = geometry::length(outward);
	if (outwardLength == 0.0) {
		return metersFromMm(nearest.x, nearest.y);
	}
	outward = outward * (1.0 / outwardLength);

	// Land where the next tick's rounding (the position as the float it is stored in, to the mm) is
	// strictly outside: on the boundary or a rounded hair inside, nav would take the mover as standing
	// in the obstacle and the forward snap would move it on.
	glm::vec2 landing = metersFromMm(nearest.x, nearest.y);
	for (int step = 1; step <= kLandingStepsMax; ++step) {
		if (geometry::pointInPolygon(nav::toMm(landing), outlineMm) == geometry::PointInPolygon::Outside) {
			break;
		}
		const geometry::Vec2d stepped = nearest + outward * (kLandingStepMm * static_cast<double>(step));
		landing						  = metersFromMm(stepped.x, stepped.y);
	}
	return landing;
}

void PlacedObstacleCollisionSystem::update(float /*deltaTime*/) {
	if (placement == nullptr || detail == nullptr) {
		return; // not wired (e.g. headless tests, early init): nothing to collide with
	}

	const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
	// Any placed entity whose padded collider can reach the agent has its position this close.
	const float queryHalfSize = nav::placedObstacleReachMeters(registry);

	struct Obstacle {
		const assets::PlacedEntity*	  entity	= nullptr;
		const assets::CollisionShape* collision = nullptr;
	};
	std::vector<Obstacle> obstacles; // reused per agent
	geometry::Ring		  outline;	 // reused per obstacle

	// Two relaxation iterations so an agent wedged between two adjacent obstacles
	// settles against both (iteration 1 clears the deepest, iteration 2 re-projects
	// the moved position against the other). Same pattern as WallCollisionSystem.
	for (int iteration = 0; iteration < 2; ++iteration) {
		// AgentRadius only picks out agents; the push is the centre's.
		for ([[maybe_unused]] auto [entity, position, agentRadius] : world->view<Position, AgentRadius>()) {
			glm::vec2 center = position.value;
			if (detail->snapHolds(entity)) {
				continue;
			}

			// --- Gather the nearby obstacles locally (position tiles == meters) ---
			obstacles.clear();
			const float queryMinX = center.x - queryHalfSize;
			const float queryMinY = center.y - queryHalfSize;
			const float queryMaxX = center.x + queryHalfSize;
			const float queryMaxY = center.y + queryHalfSize;

			const world::ChunkCoordinate firstChunk = world::worldToChunk({queryMinX, queryMinY});
			const world::ChunkCoordinate lastChunk	= world::worldToChunk({queryMaxX, queryMaxY});

			for (std::int32_t chunkY = firstChunk.y; chunkY <= lastChunk.y; ++chunkY) {
				for (std::int32_t chunkX = firstChunk.x; chunkX <= lastChunk.x; ++chunkX) {
					const assets::SpatialIndex* index = placement->getChunkIndex({chunkX, chunkY});
					if (index == nullptr) {
						continue;
					}
					// queryRect filters by entity position, which is why the box is
					// padded by the collision reach.
					std::vector<const assets::PlacedEntity*> hits = index->queryRect(queryMinX, queryMinY, queryMaxX, queryMaxY);
					for (const assets::PlacedEntity* placed : hits) {
						const assets::AssetDefinition* def = registry.getDefinition(placed->defName);
						if (def == nullptr) {
							continue;
						}
						const assets::CollisionShape& collision = def->collisionFor(placed->traits.ageClass);
						if (!collision.blocks()) {
							continue;
						}
						// This collider's own reach, not the largest any def has: most of what the
						// query finds is trees whose trunk is nowhere near the agent.
						const float		reach  = nav::placedObstacleLocalReachMeters(collision) * std::abs(placed->scale);
						const glm::vec2 offset = center - placed->position;
						if (glm::dot(offset, offset) > reach * reach) {
							continue;
						}
						if (!detail->placedObstacleInNavMesh({chunkX, chunkY}, *placed)) {
							continue;
						}
						if (registry.maxCollisionReachMeters() == 0.0F && !warnedZeroReach) {
							warnedZeroReach = true;
							LOG_WARNING(Engine, "[PlacedObstacleCollision] %s blocks but no collider reach is recorded: colliders off their position are not pushed against",
										placed->defName.c_str());
						}
						obstacles.push_back({placed, &collision});
					}
				}
			}

			if (obstacles.empty()) {
				continue;
			}

			// Deterministic ordering: resolve in a fixed (position x, then y, then def) order so
			// the accumulated push-out is independent of queryRect/chunk layout.
			std::sort(obstacles.begin(), obstacles.end(), [](const Obstacle& left, const Obstacle& right) {
				if (left.entity->position.x != right.entity->position.x) {
					return left.entity->position.x < right.entity->position.x;
				}
				if (left.entity->position.y != right.entity->position.y) {
					return left.entity->position.y < right.entity->position.y;
				}
				return left.entity->defName < right.entity->defName;
			});

			for (const Obstacle& obstacle : obstacles) {
				if (!nav::placedObstacleOutline(*obstacle.entity, *obstacle.collision, outline)) {
					continue;
				}
				if (const std::optional<glm::vec2> corrected = resolveCenterAgainstOutline(center, outline)) {
					center = *corrected;
				}
			}

			position.value = center;
		}
	}
}

} // namespace ecs
