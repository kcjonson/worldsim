#pragma once

// StaticRectCollisionSystem - Tier-3 safety-net collision against placed flora
// rects (trees, etc).
//
// Agents normally PATH around blocking flora; the navmesh carves an obstacle for
// each one and routes the agent CENTER to stay outside it. This system is the
// fallback for the case the path can't cover: an agent shoved into a flora rect
// by agent-agent separation (CollisionSystem) or by beelining a goal before the
// mesh exists. It pushes such an agent's center back out, while staying flush with
// the navmesh boundary so it never fights the path.
//
// THE BOUNDARY: the navmesh inflates each flora collision rect by kFloraColliderPadMm
// (50 mm = 0.05 m) in LOCAL space before scale, then transforms to world -- so the
// world boundary is scale*(halfExtent + 0.05). This system matches that EXACTLY:
// orientedRectFor() bakes the same pad in local space before scale, producing a
// pre-inflated OBB, and the push-out runs at clearance 0 against that OBB. The
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
// The PlacementExecutor is injected by the caller (null => no-op). Rects are
// gathered LOCALLY per agent each frame from placement (no caching, no nav
// coupling): query the chunks around the agent for rect-collision entities, keep
// only those whose AssetDefinition has CollisionShapeType::Rect.

#include "../ISystem.h"

#include <glm/vec2.hpp>

#include <optional>

namespace engine::assets {
	class PlacementExecutor;
}

namespace ecs {

// The nav flora pad in meters (engine::nav::kFloraColliderPadMm / 1000). Exposed
// here for tests that want to assert on the expected boundary without depending
// on the heavy NavInputBuilder.h include chain. orientedRectFor() bakes this pad
// into the OBB in LOCAL space before scale, so the world boundary is
// scale*(halfExtent + pad) -- matching the navmesh. The system calls
// resolveCenterAgainstRect with clearance 0 (not this constant) because the OBB
// is already pre-inflated.
constexpr float kStaticRectClearanceMeters = 0.05F;

// A placed flora rect resolved to world meters as an oriented bounding box. axisX
// and axisY are unit (rotation only); halfExtents include entity scale AND the nav
// flora pad (baked in local space before scale by orientedRectFor). The boundary
// coincides with the navmesh obstacle boundary at all scales.
struct OrientedRect {
	glm::vec2 center{0.0F, 0.0F};
	glm::vec2 axisX{1.0F, 0.0F};
	glm::vec2 axisY{0.0F, 1.0F};
	glm::vec2 halfExtents{0.0F, 0.0F};
};

// Push `center` out of `rect` inflated by `clearance`. Returns the corrected
// world-space center, or nullopt when the center is already outside the inflated
// OBB (no penetration). Pure and deterministic: it expresses the center in the
// rect's local frame, and when inside pushes along the local axis of LEAST
// penetration (ties and the exact-center case break toward +axis). No libm: the
// caller supplies the precomputed axes.
[[nodiscard]] std::optional<glm::vec2> resolveCenterAgainstRect(glm::vec2 center, float clearance, const OrientedRect& rect);

class StaticRectCollisionSystem : public ISystem {
  public:
	StaticRectCollisionSystem() = default;

	void update(float deltaTime) override;

	[[nodiscard]] int		  priority() const override { return 270; }
	[[nodiscard]] const char* name() const override { return "StaticRectCollisionSystem"; }

	// Setter injection, mirroring WallCollisionSystem. Null until wired (e.g.
	// headless tests, early init): update() is a no-op.
	void setPlacementData(const engine::assets::PlacementExecutor* placement) { m_placement = placement; }

  private:
	const engine::assets::PlacementExecutor* m_placement = nullptr;
};

} // namespace ecs
