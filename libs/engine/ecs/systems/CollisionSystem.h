#pragma once

// CollisionSystem - positional separation pass for physical agents (P1).
//
// Runs at priority 250, after PhysicsSystem (200) has integrated velocity into
// position. Each frame it rebuilds a spatial hash of all AgentRadius entities
// and runs two relaxation iterations: for each overlapping pair (A,B) it pushes
// them apart by the penetration depth, split proportionally by inverse mass.
//
// Writes only to Position. Does NOT write to Velocity (no velocity feedback in
// P1; ORCA velocity avoidance is a later phase). Does NOT do wall / static
// collision (Nav C2, later phase).
//
// Deterministic by design: pair ordering is enforced by EntityID comparison;
// the coincident-agent fallback normal is derived from the id pair with integer
// arithmetic only (no rand()/time).

#include "../ISystem.h"
#include "../spatial/AgentSpatialHash.h"

#include <vector>

namespace ecs {

class CollisionSystem : public ISystem {
  public:
    CollisionSystem() = default;

    void update(float deltaTime) override;

    [[nodiscard]] int        priority() const override { return 250; }
    [[nodiscard]] const char* name()   const override { return "Collision"; }

  private:
    AgentSpatialHash         m_hash{1.0f};
    std::vector<EntityID>    m_scratch; // reused per query, no per-frame alloc
};

} // namespace ecs
