#pragma once

// PlacedObstacleCollisionSystem - Tier-3 safety-net collision against placed obstacles' colliders.
//
// Agents normally PATH around placed obstacles; the navmesh carves an obstacle for
// each one and routes the agent CENTER to stay outside it. This system is the
// fallback for the case the path can't cover: an agent shoved into a placed obstacle
// by agent-agent separation (CollisionSystem) or by beelining a goal before the
// mesh exists. It pushes such an agent's center back out, while staying flush with
// the navmesh boundary so it never fights the path.
//
// THE BOUNDARY: the very ring the nav mesh cuts for the obstacle, nav::placedObstacleOutline, in
// integer mm. Every collider shape comes through it the same way (the registry's shape in the def's
// pivot frame, put in the world by assets::InstanceTransform): a Rect is its four corners padded by
// kPlacedObstaclePadMm in local space before scale, a Polygon its points as authored, convex or
// concave. The push is the agent CENTER against that ring, at clearance 0; AgentRadius (0.3 m) is
// used ONLY to identify agents (view<Position, AgentRadius>), never as the clearance, since a disc
// push would overshoot the navmesh boundary the path keeps to and the two would fight.
//
// Runs at priority 270, after CollisionSystem (250) does agent-agent separation
// and WallCollisionSystem (260), so an agent pushed into an obstacle this frame is
// corrected the same frame. Before ActionSystem (350). Writes only to Position;
// Velocity stays PhysicsSystem's. A position-only correction is enough for a
// safety net and avoids fighting the mover.
//
// ONE RULE WITH THE SNAP: a mover whose centre is inside a placed obstacle in the nav mesh at the start
// of the tick belongs to the forward snap (NavigationSystem::snapMovers), which moves it forward along
// its route or leaves it where it stands; one the snap left inside (NavigationSystem::snapHolds) is
// never pushed here, so it never goes back. This system keeps a mover that was outside from entering:
// it pushes out of an obstacle only once the obstacle is in the nav mesh
// (NavigationSystem::placedObstacleInNavMesh). No route is let through: a route never crosses a placed
// obstacle.
//
// The PlacementExecutor and the NavigationSystem are injected by the caller (either null => no-op).
// Obstacles are gathered LOCALLY per agent each frame from placement (no caching): the chunks around
// the agent, out to nav::placedObstacleReachMeters, for entities whose def has a blocking collider.

#include "../ISystem.h"

#include <polygon/Polygon.h>

#include <glm/vec2.hpp>

#include <optional>

namespace assets {
	class PlacementExecutor;
}

namespace ecs {

class NavigationSystem;

// Push `center` (world metres) out of `outlineMm`, a simple ring in world mm of either winding, convex
// or concave. When `center` rounded to the mm is strictly inside (geometry::pointInPolygon, exact),
// returns the boundary point nearest `center` (geometry::closestOnSegment on the unrounded centre, so
// the coordinate along the edge is kept), stepped outward by the least multiple of a quarter mm that
// makes the result, rounded to the mm as nav will round it, strictly outside; nullopt when the centre
// is outside or on the boundary. The nearest boundary point is the push along the outward normal by
// the penetration depth, and at a concave corner it is the corner itself. Ties between equally near
// edges go to the first in ring order. No input gives a NaN. Pure and deterministic.
[[nodiscard]] std::optional<glm::vec2> resolveCenterAgainstOutline(glm::vec2 center, const geometry::Ring& outlineMm);

class PlacedObstacleCollisionSystem : public ISystem {
  public:
	PlacedObstacleCollisionSystem() = default;

	void update(float deltaTime) override;

	[[nodiscard]] int		  priority() const override { return 270; }
	[[nodiscard]] const char* name() const override { return "PlacedObstacleCollisionSystem"; }

	// Setter injection, mirroring WallCollisionSystem. Null until wired (e.g.
	// headless tests, early init): update() is a no-op.
	void setPlacementData(const assets::PlacementExecutor* executor) { placement = executor; }
	void setDetailSource(const NavigationSystem* navigation) { detail = navigation; }

  private:
	const assets::PlacementExecutor* placement = nullptr;
	const NavigationSystem*			 detail	   = nullptr;
	bool							 warnedZeroReach = false;
};

} // namespace ecs
