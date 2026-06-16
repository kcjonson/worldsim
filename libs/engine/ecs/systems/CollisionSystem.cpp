#include "CollisionSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Transform.h"

#include <cmath>
#include <glm/geometric.hpp>

namespace ecs {

// Query radius uses 2 * kMaxAgentRadius so we catch all possible overlapping
// pairs regardless of each agent's individual radius.
static constexpr float kMaxAgentRadius = 0.5f;

void CollisionSystem::update(float /*deltaTime*/) {
    // Rebuild spatial hash from current positions.
    m_hash.clear();
    for (auto [entity, pos, radius] : world->view<Position, AgentRadius>()) {
        m_hash.insert(entity, pos.value);
    }

    // Two relaxation iterations converge faster than one for dense clusters
    // without the cost of full LCP solvers.
    for (int iter = 0; iter < 2; ++iter) {
        for (auto [entityA, posA, radiusA] : world->view<Position, AgentRadius>()) {
            m_hash.queryNeighbors(posA.value, 2.0f * kMaxAgentRadius, m_scratch);

            for (EntityID entityB : m_scratch) {
                if (entityB <= entityA) {
                    // Process each unordered pair exactly once (lower id is A).
                    continue;
                }

                Position*    posCompB    = world->getComponent<Position>(entityB);
                AgentRadius* radiusCompB = world->getComponent<AgentRadius>(entityB);
                if (!posCompB || !radiusCompB) {
                    continue;
                }

                glm::vec2 dvec = posCompB->value - posA.value;
                float     dist = glm::length(dvec);
                float     overlap = (radiusA.radiusMeters + radiusCompB->radiusMeters) - dist;

                if (overlap <= 0.0f) {
                    continue;
                }

                glm::vec2 n;
                if (dist > 1e-4f) {
                    n = dvec / dist;
                } else {
                    // Coincident agents: derive a deterministic normal from the id pair.
                    // No rand()/time — safe for deterministic multiplayer replay.
                    float ang = static_cast<float>((entityA * 131u + entityB) % 628u) / 100.0f;
                    n = {std::cos(ang), std::sin(ang)};
                }

                // 5% separation bias so agents settle with a hair of clearance.
                // Clamped to kMaxAgentRadius per iteration to prevent explosion
                // from deep initial overlaps.
                float pushDist = overlap * 1.05f;
                if (pushDist > kMaxAgentRadius) {
                    pushDist = kMaxAgentRadius;
                }

                float totalInvMass = radiusA.invMass + radiusCompB->invMass;
                if (totalInvMass == 0.0f) {
                    continue; // both pinned
                }

                float wA = radiusA.invMass      / totalInvMass;
                float wB = radiusCompB->invMass / totalInvMass;

                posA.value          -= n * (pushDist * wA);
                posCompB->value     += n * (pushDist * wB);
            }
        }
    }
}

} // namespace ecs
