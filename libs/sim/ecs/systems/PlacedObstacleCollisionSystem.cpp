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

	// The least step outward while looking for a landing whose mm rounding is outside: a few of these
	// clear the 0.71 mm a rounding can move a point.
	constexpr double kLandingStepMm	  = 0.25;
	constexpr int	 kLandingStepsMax = 16;
	// Cells each side of the boundary point the fallback grid searches.
	constexpr int kLandingGridHalfSize = 4;

	// Rounding the ring's points and the centre to the mm each moves a point up to 0.71 mm.
	constexpr float kReachSlackMeters = 0.002F;

	glm::vec2 metersFromMm(const geometry::Vec2d& pointMm) {
		return {static_cast<float>(pointMm.x / kMillimetersPerMeter), static_cast<float>(pointMm.y / kMillimetersPerMeter)};
	}

	geometry::Vec2d unit(const geometry::Vec2d& vector) {
		const double size = geometry::length(vector);
		return size > 0.0 ? vector * (1.0 / size) : geometry::Vec2d{};
	}

	// The gap, in mm, between a stored coordinate and the next float past it: what a position there can
	// move by at all. Under a mm near the origin, 4 mm 50 km out, half a metre 6,000 km out.
	double floatSpacingMm(float coordinate) {
		const float magnitude = std::abs(coordinate);
		return static_cast<double>(std::nextafter(magnitude, std::numeric_limits<float>::infinity()) - magnitude) * kMillimetersPerMeter;
	}

	// Outward unit normal of the ring's edge from `from` to `to`.
	geometry::Vec2d edgeOutward(const geometry::Vec2i64& from, const geometry::Vec2i64& to, bool counterClockwise) {
		const double alongX = static_cast<double>(to.x - from.x);
		const double alongY = static_cast<double>(to.y - from.y);
		return unit(counterClockwise ? geometry::Vec2d{alongY, -alongX} : geometry::Vec2d{-alongY, alongX});
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
	double				  nearestT	  = 0.0;
	for (std::size_t i = 0; i < count; ++i) {
		const geometry::Vec2i64&	 from	 = outlineMm[i];
		const geometry::Vec2i64&	 to		 = outlineMm[(i + 1) % count];
		const geometry::SegmentPoint closest = geometry::closestOnSegment(centerMm, from, to);
		if (closest.distanceMm < bestDistance) {
			bestDistance = closest.distanceMm;
			nearestEdge	 = i;
			nearestT	 = closest.t;
			nearest		 = {static_cast<double>(from.x) + static_cast<double>(to.x - from.x) * closest.t,
							static_cast<double>(from.y) + static_cast<double>(to.y - from.y) * closest.t};
		}
	}

	// Which side of the boundary the centre really is on. Rounded it is inside, but on a slanted edge
	// the unrounded centre can be up to 0.71 mm outside, and pushing "toward the nearest point" from
	// there would push it in. Against an edge's interior the side is the edge normal's; at a corner it
	// is inside a convex corner when inside both edges, inside a reflex one when inside either.
	const bool			  counterClockwise = geometry::windingOrder(outlineMm) != geometry::Winding::Clockwise;
	const geometry::Vec2d edgeNormal	   = edgeOutward(outlineMm[nearestEdge], outlineMm[(nearestEdge + 1) % count], counterClockwise);
	const geometry::Vec2d fromNearest	   = centerMm - nearest;
	geometry::Vec2d		  cornerNormal	   = edgeNormal;
	bool				  reallyInside	   = (fromNearest.x * edgeNormal.x + fromNearest.y * edgeNormal.y) < 0.0;
	if (nearestT <= 0.0 || nearestT >= 1.0) {
		const std::size_t		 corner	  = nearestT <= 0.0 ? nearestEdge : (nearestEdge + 1) % count;
		const geometry::Vec2i64& before	  = outlineMm[(corner + count - 1) % count];
		const geometry::Vec2i64& at		  = outlineMm[corner];
		const geometry::Vec2i64& after	  = outlineMm[(corner + 1) % count];
		const geometry::Vec2d	 normalIn = edgeOutward(before, at, counterClockwise);
		const geometry::Vec2d	 normalOut = edgeOutward(at, after, counterClockwise);
		const bool				 insideIn = (fromNearest.x * normalIn.x + fromNearest.y * normalIn.y) < 0.0;
		const bool				 insideOut = (fromNearest.x * normalOut.x + fromNearest.y * normalOut.y) < 0.0;
		const double			 turn = static_cast<double>(at.x - before.x) * static_cast<double>(after.y - at.y) -
							static_cast<double>(at.y - before.y) * static_cast<double>(after.x - at.x);
		const bool convex = counterClockwise ? turn > 0.0 : turn < 0.0;
		reallyInside	  = convex ? (insideIn && insideOut) : (insideIn || insideOut);
		cornerNormal	  = unit(normalIn + normalOut);
		if (geometry::length(cornerNormal) == 0.0) {
			cornerNormal = edgeNormal;
		}
	}

	// Outward: away from the centre through the nearest point when it is inside (at a reflex corner that
	// bisects the corner's two edges), away from the nearest point through the centre when it is already
	// a hair outside. A centre on the boundary has no direction, so the edge's or corner's normal stands in.
	geometry::Vec2d outward = unit(reallyInside ? nearest - centerMm : centerMm - nearest);
	if (geometry::length(fromNearest) < 1e-6 || geometry::length(outward) == 0.0) {
		outward = cornerNormal;
	}

	// Land where the next tick's rounding (the position as the float it is stored in, to the mm) is
	// strictly outside: on the boundary or a rounded hair inside, nav would take the mover as standing in
	// the obstacle and the forward snap would move it on. A step is never finer than the float spacing
	// there, which far from the origin is coarser than the mm (see floatSpacingMm).
	const double step = std::max({kLandingStepMm, floatSpacingMm(metersFromMm(nearest).x), floatSpacingMm(metersFromMm(nearest).y)});
	for (int index = 0; index <= kLandingStepsMax; ++index) {
		const glm::vec2 landing = metersFromMm(nearest + outward * (step * static_cast<double>(index)));
		if (geometry::pointInPolygon(nav::toMm(landing), outlineMm) == geometry::PointInPolygon::Outside) {
			return landing;
		}
	}
	// The straight walk can miss in a slot a few mm wide (heading out of a corner at the bottom of it,
	// every rounded step lands on a wall). Then the nearest point of a grid round the boundary point,
	// a step apart, that rounds outside.
	const double			 cell = std::max(1.0, step);
	std::optional<glm::vec2> closest;
	double					 closestDistance = std::numeric_limits<double>::max();
	for (int across = -kLandingGridHalfSize; across <= kLandingGridHalfSize; ++across) {
		for (int down = -kLandingGridHalfSize; down <= kLandingGridHalfSize; ++down) {
			const geometry::Vec2d candidate = nearest + geometry::Vec2d{static_cast<double>(across) * cell, static_cast<double>(down) * cell};
			const glm::vec2		  landing	= metersFromMm(candidate);
			if (geometry::pointInPolygon(nav::toMm(landing), outlineMm) != geometry::PointInPolygon::Outside) {
				continue;
			}
			const double distance = geometry::length(candidate - nearest);
			if (distance < closestDistance) {
				closestDistance = distance;
				closest			= landing;
			}
		}
	}
	// Nothing outside in reach either: leave the mover where it stands rather than put it somewhere
	// inside; the forward snap owns a mover inside an obstacle.
	return closest;
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
						// query finds is trees whose trunk is nowhere near the agent. The slack covers
						// the mm rounding of the ring and of the centre the inside test takes.
						const float		reach  = nav::placedObstacleLocalReachMeters(collision) * std::abs(placed->scale) + kReachSlackMeters;
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
