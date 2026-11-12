#pragma once

// StaticRectCollisionSystem - Tier-3 safety-net collision against placed obstacles' collider
// rects.
//
// Agents normally PATH around placed obstacles; the navmesh carves an obstacle for
// each one and routes the agent CENTER to stay outside it. This system is the
// fallback for the case the path can't cover: an agent shoved into a placed obstacle's rect
// by agent-agent separation (CollisionSystem) or by beelining a goal before the
// mesh exists. It pushes such an agent's center back out, while staying flush with
// the navmesh boundary so it never fights the path.
//
// THE BOUNDARY: the navmesh inflates each placed obstacle's collision rect by kPlacedObstaclePadMm
// (50 mm = 0.05 m) in LOCAL space before scale, then transforms to world -- so the
// world boundary is |scale|*(halfExtent + 0.05). This system pushes against the very same box,
// nav::placedObstacleBox (which the mesh ring is built from), pre-inflated, at clearance 0. The
// boundary is NOT a disc-vs-OBB push-out using AgentRadius (0.3 m); using the disc
// radius would overshoot the navmesh boundary and create a fight (jitter, agents
// shoved off valid nav paths). AgentRadius is used ONLY to identify agents
// (view<Position, AgentRadius>), never as the clearance.
//
// Runs at priority 270, after CollisionSystem (250) does agent-agent separation
// and WallCollisionSystem (260), so an agent pushed into a rect this frame is
// corrected the same frame. Before ActionSystem (350). Writes only to Position;
// Velocity stays PhysicsSystem's. A position-only correction is enough for a
// safety net and avoids fighting the mover.
//
// MESH GATE: every mover is pushed out of a placed obstacle wherever that obstacle is in the nav mesh
// (NavigationSystem::placedObstacleInNavMesh), and not before: the forward snap moves a mover out of an
// obstacle it finds itself in before this system ever sees it, so the two never disagree about one. A
// mover following a route that walks through detail at its price (NavigationSystem::routeCrossesPricedFaces)
// is let through.
//
// The PlacementExecutor and the NavigationSystem are injected by the caller (either null => no-op). Rects are
// gathered LOCALLY per agent each frame from placement (no caching): query the chunks around the agent for rect-collision entities, keep
// only those whose AssetDefinition has CollisionShapeType::Rect (a polygon collider is in the nav
// mesh but not pushed against here).

#include "../ISystem.h"

#include <nav/PlacedObstacle.h>

#include <glm/vec2.hpp>

#include <optional>

namespace assets {
	class PlacementExecutor;
}

namespace ecs {

class NavigationSystem;

// The placed-obstacle pad in meters (nav::kPlacedObstaclePadMm / 1000), for tests that assert on the
// expected boundary. The system calls resolveCenterAgainstRect with clearance 0 (not this
// constant) because nav::placedObstacleBox's box is already pre-inflated.
constexpr float kStaticRectClearanceMeters = 0.05F;

// Push `center` out of `rect` inflated by `clearance`. Returns the corrected
// world-space center, or nullopt when the center is already outside the inflated
// OBB (no penetration). Pure and deterministic: it expresses the center in the
// rect's local frame, and when inside pushes along the local axis of LEAST
// penetration (ties and the exact-center case break toward +axis). No libm: the
// caller supplies the precomputed axes.
[[nodiscard]] std::optional<glm::vec2> resolveCenterAgainstRect(glm::vec2 center, float clearance, const nav::OrientedRect& rect);

class StaticRectCollisionSystem : public ISystem {
  public:
	StaticRectCollisionSystem() = default;

	void update(float deltaTime) override;

	[[nodiscard]] int		  priority() const override { return 270; }
	[[nodiscard]] const char* name() const override { return "StaticRectCollisionSystem"; }

	// Setter injection, mirroring WallCollisionSystem. Null until wired (e.g.
	// headless tests, early init): update() is a no-op.
	void setPlacementData(const assets::PlacementExecutor* placement) { m_placement = placement; }
	void setDetailSource(const NavigationSystem* navigation) { detail = navigation; }

  private:
	const assets::PlacementExecutor* m_placement = nullptr;
	const NavigationSystem*			 detail		 = nullptr;
};

} // namespace ecs
