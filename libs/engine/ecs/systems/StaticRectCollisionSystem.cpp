#include "StaticRectCollisionSystem.h"

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
	// while its center sits outside the unpadded box. Trunk rects are <= 0.1 m
	// half-extent, so 2 m is safe with wide headroom.
	constexpr float kStaticRectQueryMarginMeters = 2.0F;

	// Build the world-meters OBB for a placed entity from its transform and the
	// def's collision rect (offset = local center, halfExtents = local half size).
	// Mirrors nav floraRingFor / toWorldMm exactly: pad in LOCAL space first, then
	// scale, then rotate, then translate -- so the world boundary is
	// scale*(halfExtent + pad) on both axes, matching the navmesh obstacle boundary
	// at ALL scales. The returned OBB is pre-inflated; callers pass clearance 0.
	OrientedRect orientedRectFor(const engine::assets::PlacedEntity& e, const engine::assets::CollisionShape& collision) {
		const float cosR      = std::cos(e.rotation);
		const float sinR      = std::sin(e.rotation);
		const float sc        = e.scale;
		const float padMeters = static_cast<float>(engine::nav::kFloraColliderPadMm) / 1000.0F;

		// rotate(v) = {v.x*cosR - v.y*sinR, v.x*sinR + v.y*cosR}
		const glm::vec2 off = collision.offsetMeters * sc;
		const glm::vec2 rotatedOffset{off.x * cosR - off.y * sinR, off.x * sinR + off.y * cosR};

		OrientedRect rect;
		rect.center      = e.position + rotatedOffset;
		rect.axisX       = {cosR, sinR};
		rect.axisY       = {-sinR, cosR};
		rect.halfExtents = sc * (collision.halfExtentsMeters + glm::vec2{padMeters, padMeters});
		return rect;
	}

} // namespace

std::optional<glm::vec2> resolveCenterAgainstRect(glm::vec2 center, float clearance, const OrientedRect& rect) {
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
	if (m_placement == nullptr) {
		return; // not wired (e.g. headless tests, early init): nothing to collide with
	}

	const engine::assets::AssetRegistry& registry = engine::assets::AssetRegistry::Get();
	// The OBB from orientedRectFor is pre-inflated by the nav flora pad (already
	// baked into halfExtents), so the query box needs only the spatial margin.
	const float boxHalf = kStaticRectQueryMarginMeters;

	std::vector<OrientedRect> rects; // reused per agent

	// Two relaxation iterations so an agent wedged between two adjacent rects
	// settles against both (iteration 1 clears the deepest, iteration 2 re-projects
	// the moved position against the other). Same pattern as WallCollisionSystem.
	for (int iter = 0; iter < 2; ++iter) {
		for (auto [entity, pos, radius] : world->view<Position, AgentRadius>()) {
			glm::vec2 p = pos.value;

			// --- Gather the nearby rects locally (position tiles == meters) ---
			rects.clear();
			const float boxMinX = p.x - boxHalf;
			const float boxMinY = p.y - boxHalf;
			const float boxMaxX = p.x + boxHalf;
			const float boxMaxY = p.y + boxHalf;

			const engine::world::ChunkCoordinate cMin =
				engine::world::worldToChunk({boxMinX, boxMinY});
			const engine::world::ChunkCoordinate cMax =
				engine::world::worldToChunk({boxMaxX, boxMaxY});

			for (std::int32_t cy = cMin.y; cy <= cMax.y; ++cy) {
				for (std::int32_t cx = cMin.x; cx <= cMax.x; ++cx) {
					const engine::assets::SpatialIndex* index = m_placement->getChunkIndex({cx, cy});
					if (index == nullptr) {
						continue;
					}
					// queryRect filters by entity CENTER, which is why the box is
					// padded by kStaticRectQueryMarginMeters.
					std::vector<const engine::assets::PlacedEntity*> hits =
						index->queryRect(boxMinX, boxMinY, boxMaxX, boxMaxY);
					for (const engine::assets::PlacedEntity* e : hits) {
						const engine::assets::AssetDefinition* def = registry.getDefinition(e->defName);
						if (def == nullptr || def->collision.type != engine::assets::CollisionShapeType::Rect) {
							continue;
						}
						rects.push_back(orientedRectFor(*e, def->collision));
					}
				}
			}

			if (rects.empty()) {
				continue;
			}

			// Deterministic ordering: resolve in a fixed (center x, then y) order so
			// the accumulated push-out is independent of queryRect/chunk layout.
			std::sort(rects.begin(), rects.end(), [](const OrientedRect& l, const OrientedRect& r) {
				if (l.center.x != r.center.x) {
					return l.center.x < r.center.x;
				}
				return l.center.y < r.center.y;
			});

			for (const OrientedRect& rect : rects) {
				// clearance 0: the OBB is already pre-inflated by the nav flora pad.
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
