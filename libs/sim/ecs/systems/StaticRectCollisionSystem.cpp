#include "StaticRectCollisionSystem.h"

#include "NavigationSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Transform.h"

#include <assets/AssetDefinition.h>
#include <assets/AssetRegistry.h>
#include <assets/placement/PlacementExecutor.h>
#include <assets/placement/SpatialIndex.h>

#include <nav/NavInputBuilder.h>

#include <world/chunk/ChunkCoordinate.h>

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ecs {

namespace {

	// Generous fixed half-size margin added to the query box (meters). It MUST
	// exceed the largest collider half-extent + clearance so queryRect (which
	// filters by entity CENTER) never misses a rect whose body overlaps the agent
	// while its center sits outside the unpadded box. Collider rects here are <= 0.1 m
	// half-extent, so 2 m is safe with wide headroom.
	constexpr float kStaticRectQueryMarginMeters = 2.0F;

} // namespace

std::optional<glm::vec2> resolveCenterAgainstRect(glm::vec2 center, float clearance, const nav::OrientedRect& rect) {
	const glm::vec2 d  = center - rect.center;
	const float		lx = glm::dot(d, rect.axisX);
	const float		ly = glm::dot(d, rect.axisY);

	const float ex = rect.halfExtents.x + clearance;
	const float ey = rect.halfExtents.y + clearance;

	const float absLx = std::abs(lx);
	const float absLy = std::abs(ly);
	if (absLx >= ex || absLy >= ey) {
		return std::nullopt; // center outside the inflated OBB on some axis: no penetration
	}

	// Inside: push out along the local axis of LEAST penetration. sign(0) -> +1 so
	// an agent exactly on an axis (or at the rect center) resolves deterministically.
	const float penX = ex - absLx;
	const float penY = ey - absLy;

	float newLx = lx;
	float newLy = ly;
	if (penX <= penY) {
		newLx = (lx < 0.0F) ? -ex : ex;
	} else {
		newLy = (ly < 0.0F) ? -ey : ey;
	}

	return rect.center + newLx * rect.axisX + newLy * rect.axisY;
}

void StaticRectCollisionSystem::update(float /*deltaTime*/) {
	if (m_placement == nullptr || detail == nullptr) {
		return; // not wired (e.g. headless tests, early init): nothing to collide with
	}

	const assets::AssetRegistry& registry = assets::AssetRegistry::Get();
	// The box from nav::placedObstacleBox is pre-inflated by the placed-obstacle pad (already
	// baked into halfExtents), so the query box needs only the spatial margin.
	const float boxHalf = kStaticRectQueryMarginMeters;

	std::vector<nav::OrientedRect> rects; // reused per agent

	// Two relaxation iterations so an agent wedged between two adjacent rects
	// settles against both (iteration 1 clears the deepest, iteration 2 re-projects
	// the moved position against the other). Same pattern as WallCollisionSystem.
	for (int iter = 0; iter < 2; ++iter) {
		for (auto [entity, pos, radius] : world->view<Position, AgentRadius>()) {
			glm::vec2 p = pos.value;
			if (detail->snapHolds(entity)) {
				continue;
			}

			// --- Gather the nearby rects locally (position tiles == meters) ---
			rects.clear();
			const float boxMinX = p.x - boxHalf;
			const float boxMinY = p.y - boxHalf;
			const float boxMaxX = p.x + boxHalf;
			const float boxMaxY = p.y + boxHalf;

			const world::ChunkCoordinate cMin =
				world::worldToChunk({boxMinX, boxMinY});
			const world::ChunkCoordinate cMax =
				world::worldToChunk({boxMaxX, boxMaxY});

			for (std::int32_t cy = cMin.y; cy <= cMax.y; ++cy) {
				for (std::int32_t cx = cMin.x; cx <= cMax.x; ++cx) {
					const assets::SpatialIndex* index = m_placement->getChunkIndex({cx, cy});
					if (index == nullptr) {
						continue;
					}
					// queryRect filters by entity CENTER, which is why the box is
					// padded by kStaticRectQueryMarginMeters.
					std::vector<const assets::PlacedEntity*> hits =
						index->queryRect(boxMinX, boxMinY, boxMaxX, boxMaxY);
					for (const assets::PlacedEntity* e : hits) {
						const assets::AssetDefinition* def = registry.getDefinition(e->defName);
						if (def == nullptr || !detail->placedObstacleInNavMesh({cx, cy}, *e)) {
							continue;
						}
						// Rect colliders only: a polygon one is in the nav mesh but not pushed against here.
						if (const std::optional<nav::OrientedRect> box = nav::placedObstacleBox(*e, def->collisionFor(e->traits.ageClass))) {
							rects.push_back(*box);
						}
					}
				}
			}

			if (rects.empty()) {
				continue;
			}

			// Deterministic ordering: resolve in a fixed (center x, then y) order so
			// the accumulated push-out is independent of queryRect/chunk layout.
			std::sort(rects.begin(), rects.end(), [](const nav::OrientedRect& l, const nav::OrientedRect& r) {
				if (l.center.x != r.center.x) {
					return l.center.x < r.center.x;
				}
				return l.center.y < r.center.y;
			});

			for (const nav::OrientedRect& rect : rects) {
				// clearance 0: the OBB is already pre-inflated by the placed-obstacle pad.
				std::optional<glm::vec2> corrected = resolveCenterAgainstRect(p, 0.0F, rect);
				if (corrected.has_value()) {
					p = *corrected;
				}
			}

			pos.value = p;
		}
	}
}

} // namespace ecs
