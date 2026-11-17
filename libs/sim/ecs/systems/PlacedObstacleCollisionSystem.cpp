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

	// Rounding the ring's points and the centre to the mm each moves a point up to 0.71 mm. That holds
	// near the landing site; far out the float spacing is coarser and this is not enough (WOR-972).
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

	double dot(const geometry::Vec2d& left, const geometry::Vec2d& right) { return left.x * right.x + left.y * right.y; }

	struct BoundaryPoint {
		geometry::Vec2d point;
		std::size_t		edge = 0;
		double			t	 = 0.0; // along the edge; 0 or 1 at a corner
	};

	// The boundary point nearest `centerMm`, off the mm lattice, so a push keeps the coordinate along
	// the edge. Ties go to the first edge in ring order.
	BoundaryPoint nearestBoundaryPoint(const geometry::Vec2d& centerMm, const geometry::Ring& outlineMm) {
		const std::size_t count		   = outlineMm.size();
		double			  bestDistance = std::numeric_limits<double>::max();
		BoundaryPoint	  nearest;
		for (std::size_t i = 0; i < count; ++i) {
			const geometry::Vec2i64&	 from	 = outlineMm[i];
			const geometry::Vec2i64&	 to		 = outlineMm[(i + 1) % count];
			const geometry::SegmentPoint closest = geometry::closestOnSegment(centerMm, from, to);
			if (closest.distanceMm < bestDistance) {
				bestDistance = closest.distanceMm;
				nearest		 = {{static_cast<double>(from.x) + static_cast<double>(to.x - from.x) * closest.t,
								 static_cast<double>(from.y) + static_cast<double>(to.y - from.y) * closest.t},
								i,
								closest.t};
			}
		}
		return nearest;
	}

	struct CenterSide {
		bool			inside = false;
		geometry::Vec2d normal; // outward: the edge's, or at a corner the bisector of its two edges'
	};

	// Which side of the boundary the unrounded centre is on. Its rounding is inside, but beside a
	// slanted edge the centre itself can be up to 0.71 mm outside, and pushing it toward the boundary
	// from there would push it in. At a corner: inside a convex one when inside both edges, inside a
	// reflex one when inside either.
	CenterSide centerSide(const geometry::Vec2d& centerMm, const BoundaryPoint& nearest, const geometry::Ring& outlineMm) {
		const std::size_t	  count			   = outlineMm.size();
		const bool			  counterClockwise = geometry::windingOrder(outlineMm) != geometry::Winding::Clockwise;
		const geometry::Vec2d fromNearest	   = centerMm - nearest.point;
		const geometry::Vec2d edgeNormal = edgeOutward(outlineMm[nearest.edge], outlineMm[(nearest.edge + 1) % count], counterClockwise);
		if (nearest.t > 0.0 && nearest.t < 1.0) {
			return {dot(fromNearest, edgeNormal) < 0.0, edgeNormal};
		}
		const std::size_t		 corner			= nearest.t <= 0.0 ? nearest.edge : (nearest.edge + 1) % count;
		const geometry::Vec2i64& before			= outlineMm[(corner + count - 1) % count];
		const geometry::Vec2i64& at				= outlineMm[corner];
		const geometry::Vec2i64& after			= outlineMm[(corner + 1) % count];
		const geometry::Vec2d	 incomingNormal = edgeOutward(before, at, counterClockwise);
		const geometry::Vec2d	 outgoingNormal = edgeOutward(at, after, counterClockwise);
		const bool				 insideIncoming = dot(fromNearest, incomingNormal) < 0.0;
		const bool				 insideOutgoing = dot(fromNearest, outgoingNormal) < 0.0;
		const double turn = static_cast<double>(at.x - before.x) * static_cast<double>(after.y - at.y) -
							static_cast<double>(at.y - before.y) * static_cast<double>(after.x - at.x);
		const bool		convex	= counterClockwise ? turn > 0.0 : turn < 0.0;
		geometry::Vec2d bisector = unit(incomingNormal + outgoingNormal);
		if (geometry::length(bisector) == 0.0) {
			bisector = edgeNormal;
		}
		return {convex ? (insideIncoming && insideOutgoing) : (insideIncoming || insideOutgoing), bisector};
	}

} // namespace

std::optional<glm::vec2> landOutside(const geometry::Vec2d& boundaryMm, const geometry::Vec2d& outward, const geometry::Ring& outlineMm) {
	const auto roundsOutside = [&](glm::vec2 landing) {
		return geometry::pointInPolygon(nav::toMm(landing), outlineMm) == geometry::PointInPolygon::Outside;
	};

	// Never finer than the float spacing at the position, which far from the origin is coarser than a mm.
	const glm::vec2 boundary = metersFromMm(boundaryMm);
	const double	step	 = std::max({kLandingStepMm, floatSpacingMm(boundary.x), floatSpacingMm(boundary.y)});
	for (int index = 0; index <= kLandingStepsMax; ++index) {
		const glm::vec2 landing = metersFromMm(boundaryMm + outward * (step * static_cast<double>(index)));
		if (roundsOutside(landing)) {
			return landing;
		}
	}

	// The walk can miss in a slot a few mm wide (out of a corner at its bottom every rounded step lands
	// on a wall). The grid's nearest outside point need not be in the same pocket of a concave shape.
	const double			 cell = std::max(1.0, step);
	std::optional<glm::vec2> closest;
	double					 closestDistance = std::numeric_limits<double>::max();
	for (int across = -kLandingGridHalfSize; across <= kLandingGridHalfSize; ++across) {
		for (int down = -kLandingGridHalfSize; down <= kLandingGridHalfSize; ++down) {
			const geometry::Vec2d candidate = boundaryMm + geometry::Vec2d{static_cast<double>(across) * cell, static_cast<double>(down) * cell};
			const glm::vec2		  landing	= metersFromMm(candidate);
			const double		  distance	= geometry::length(candidate - boundaryMm);
			if (distance < closestDistance && roundsOutside(landing)) {
				closestDistance = distance;
				closest			= landing;
			}
		}
	}
	// Nothing outside in reach: better the mover stays put than lands inside; the forward snap owns it.
	return closest;
}

std::optional<glm::vec2> resolveCenterAgainstOutline(glm::vec2 center, const geometry::Ring& outlineMm) {
	if (outlineMm.size() < 3 || geometry::pointInPolygon(nav::toMm(center), outlineMm) != geometry::PointInPolygon::Inside) {
		return std::nullopt;
	}
	const geometry::Vec2d centerMm{static_cast<double>(center.x) * kMillimetersPerMeter, static_cast<double>(center.y) * kMillimetersPerMeter};
	const BoundaryPoint	  nearest = nearestBoundaryPoint(centerMm, outlineMm);
	const CenterSide	  side	  = centerSide(centerMm, nearest, outlineMm);

	// Out through the boundary point from inside; on from it through the centre when the centre is a
	// hair outside; along the normal when the centre is on the boundary.
	geometry::Vec2d outward = unit(side.inside ? nearest.point - centerMm : centerMm - nearest.point);
	if (geometry::length(centerMm - nearest.point) < 1e-6 || geometry::length(outward) == 0.0) {
		outward = side.normal;
	}
	return landOutside(nearest.point, outward, outlineMm);
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
