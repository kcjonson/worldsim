#pragma once

// WallCollisionSystem - safety-net static collision against built walls (Nav C2).
//
// Colonists normally PATH around walls; the navmesh handles that. This system is
// the fallback for the cases the path can't cover: an agent with no path yet
// (mesh still building, or beelining an unreachable goal), or one shoved into a
// wall by crowd separation. It keeps such an agent from clipping through a solid
// built wall, while leaving door gaps passable.
//
// Runs at priority 260, after PhysicsSystem (200) integrates velocity and
// CollisionSystem (250) does agent-agent separation, so an agent pushed into a
// wall this frame is corrected the same frame. Writes only to Position; Velocity
// stays PhysicsSystem's. A position-only correction is enough for a safety net
// and avoids fighting the mover (which keeps re-supplying its intended velocity).
//
// The construction world is injected by the caller (null => no-op). Wall
// thickness and opening widths come from ConstructionRegistry::Get().

#include "../ISystem.h"

namespace engine::construction {
	class ConstructionWorld;
}

namespace ecs {

class WallCollisionSystem : public ISystem {
  public:
	WallCollisionSystem() = default;

	void update(float deltaTime) override;

	[[nodiscard]] int		  priority() const override { return 260; }
	[[nodiscard]] const char* name() const override { return "WallCollisionSystem"; }

	// Setter injection, mirroring NavigationSystem/ConstructionSystem. Null until
	// wired (GameScene wires it once DrawingSystem owns the ConstructionWorld).
	void setConstructionWorld(const engine::construction::ConstructionWorld* world) { m_construction = world; }

  private:
	const engine::construction::ConstructionWorld* m_construction = nullptr;
};

} // namespace ecs
