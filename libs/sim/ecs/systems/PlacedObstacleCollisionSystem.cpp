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

#include <predicates/Predicates.h>

#include <utils/Log.h>

#include <world/chunk/ChunkCoordinate.h>

#include <glm/vec2.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace ecs {

std::optional<glm::vec2> resolveCenterAgainstOutline(glm::vec2 center, const geometry::Ring& outlineMm) {
	const std::size_t count = outlineMm.size();
	if (count < 3) {
		return std::nullopt;
	}
	const geometry::Vec2i64 centerMm = nav::toMm(center);
	if (geometry::pointInPolygon(centerMm, outlineMm) != geometry::PointInPolygon::Inside) {
		return std::nullopt;
	}

	double		bestDistance = std::numeric_limits<double>::max();
	double		bestX		 = 0.0;
	double		bestY		 = 0.0;
	for (std::size_t i = 0; i < count; ++i) {
		const geometry::Vec2i64&	from	= outlineMm[i];
		const geometry::Vec2i64&	to		= outlineMm[(i + 1) % count];
		const geometry::SegmentPoint nearest = geometry::closestOnSegment(centerMm, from, to);
		if (nearest.distanceMm < bestDistance) {
			bestDistance = nearest.distanceMm;
			bestX		 = static_cast<double>(from.x) + static_cast<double>(to.x - from.x) * nearest.t;
			bestY		 = static_cast<double>(from.y) + static_cast<double>(to.y - from.y) * nearest.t;
		}
	}
	const double perMeter = static_cast<double>(geometry::kMillimetersPerMeter);
	return glm::vec2{static_cast<float>(bestX / perMeter), static_cast<float>(bestY / perMeter)};
}

void PlacedObstacleCollisionSystem::update(float /*deltaTime*/) {
	if (placement == nullptr || detail == nullptr) {
		return; // not wired (e.g. headless tests, early init): nothing to collide with
	}

	const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
	// Any placed entity whose padded collider can reach the agent has its position this close.
	const float boxHalf = nav::placedObstacleReachMeters(registry);

	std::vector<const assets::PlacedEntity*> obstacles; // reused per agent
	geometry::Ring							 outline;	// reused per obstacle

	// Two relaxation iterations so an agent wedged between two adjacent obstacles
	// settles against both (iteration 1 clears the deepest, iteration 2 re-projects
	// the moved position against the other). Same pattern as WallCollisionSystem.
	for (int iter = 0; iter < 2; ++iter) {
		for (auto [entity, pos, radius] : world->view<Position, AgentRadius>()) {
			glm::vec2 p = pos.value;
			if (detail->snapHolds(entity)) {
				continue;
			}

			// --- Gather the nearby obstacles locally (position tiles == meters) ---
			obstacles.clear();
			const float boxMinX = p.x - boxHalf;
			const float boxMinY = p.y - boxHalf;
			const float boxMaxX = p.x + boxHalf;
			const float boxMaxY = p.y + boxHalf;

			const world::ChunkCoordinate cMin = world::worldToChunk({boxMinX, boxMinY});
			const world::ChunkCoordinate cMax = world::worldToChunk({boxMaxX, boxMaxY});

			for (std::int32_t cy = cMin.y; cy <= cMax.y; ++cy) {
				for (std::int32_t cx = cMin.x; cx <= cMax.x; ++cx) {
					const assets::SpatialIndex* index = placement->getChunkIndex({cx, cy});
					if (index == nullptr) {
						continue;
					}
					// queryRect filters by entity position, which is why the box is
					// padded by the collision reach.
					std::vector<const assets::PlacedEntity*> hits = index->queryRect(boxMinX, boxMinY, boxMaxX, boxMaxY);
					for (const assets::PlacedEntity* e : hits) {
						const assets::AssetDefinition* def = registry.getDefinition(e->defName);
						if (def == nullptr || !def->collisionFor(e->traits.ageClass).blocks() ||
							!detail->placedObstacleInNavMesh({cx, cy}, *e)) {
							continue;
						}
						if (registry.maxCollisionReachMeters() == 0.0F && !warnedZeroReach) {
							warnedZeroReach = true;
							LOG_WARNING(Engine, "[PlacedObstacleCollision] %s blocks but no collider reach is recorded: colliders off their position are not pushed against",
										e->defName.c_str());
						}
						obstacles.push_back(e);
					}
				}
			}

			if (obstacles.empty()) {
				continue;
			}

			// Deterministic ordering: resolve in a fixed (position x, then y, then def) order so
			// the accumulated push-out is independent of queryRect/chunk layout.
			std::sort(obstacles.begin(), obstacles.end(), [](const assets::PlacedEntity* l, const assets::PlacedEntity* r) {
				if (l->position.x != r->position.x) {
					return l->position.x < r->position.x;
				}
				if (l->position.y != r->position.y) {
					return l->position.y < r->position.y;
				}
				return l->defName < r->defName;
			});

			for (const assets::PlacedEntity* obstacle : obstacles) {
				if (!nav::placedObstacleOutline(*obstacle, registry, outline)) {
					continue;
				}
				if (const std::optional<glm::vec2> corrected = resolveCenterAgainstOutline(p, outline)) {
					p = *corrected;
				}
			}

			pos.value = p;
		}
	}
}

} // namespace ecs
