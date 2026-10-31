#include "CollisionSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Transform.h"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>

namespace ecs {

// Query radius uses 2 * kMaxAgentRadius so we catch all possible overlapping
// pairs regardless of each agent's individual radius.
static constexpr float kMaxAgentRadius = 0.5f;

// Eight pre-normalized fallback directions for coincident agents, indexed by an
// integer id-hash. A fixed table of literal constants is identical on every
// platform; std::cos/std::sin vary across libm versions and would break the
// determinism the separation needs for multiplayer replay.
static constexpr float kInvSqrt2 = 0.70710678118654752440f;
static const glm::vec2 kFallbackDirs[8] = {
    {1.0f, 0.0f},  {kInvSqrt2, kInvSqrt2},   {0.0f, 1.0f},  {-kInvSqrt2, kInvSqrt2},
    {-1.0f, 0.0f}, {-kInvSqrt2, -kInvSqrt2}, {0.0f, -1.0f}, {kInvSqrt2, -kInvSqrt2},
};

void CollisionSystem::update(float /*deltaTime*/) {
    // Two relaxation iterations converge faster than one for dense clusters
    // without the cost of full LCP solvers.
    for (int iter = 0; iter < 2; ++iter) {
        // Rebuild the hash from current positions at the start of each iteration:
        // iteration 1's pushes move agents, so iteration 2 must query against the
        // updated positions, not a hash built from stale ones.
        m_hash.clear();
        for (auto [entity, pos, radius] : world->view<Position, AgentRadius>()) {
            m_hash.insert(entity, pos.value);
        }

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
                    // Coincident agents: pick a deterministic fallback direction from a
                    // fixed table by an integer id-hash. No rand()/time and no trig, so
                    // the result is identical across platforms (multiplayer replay safe).
                    n = kFallbackDirs[(entityA * 131u + entityB) % 8u];
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
