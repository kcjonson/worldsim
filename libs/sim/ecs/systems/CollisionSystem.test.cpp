#include "CollisionSystem.h"

#include "../World.h"
#include "../components/AgentRadius.h"
#include "../components/Transform.h"

#include <cmath>
#include <gtest/gtest.h>

using namespace ecs;

namespace {

// Build a world with two agents and return the system reference.
// Positions and radii come from the caller.
struct TwoAgentFixture {
    World            world;
    EntityID         a;
    EntityID         b;
    CollisionSystem* sys = nullptr;

    TwoAgentFixture(glm::vec2 posA, glm::vec2 posB,
                    float rA = 0.3f, float rB = 0.3f,
                    float invMassA = 1.0f, float invMassB = 1.0f) {
        a = world.createEntity();
        world.addComponent<Position>(a, Position{posA});
        world.addComponent<AgentRadius>(a, AgentRadius{rA, invMassA});

        b = world.createEntity();
        world.addComponent<Position>(b, Position{posB});
        world.addComponent<AgentRadius>(b, AgentRadius{rB, invMassB});

        sys = &world.registerSystem<CollisionSystem>();
    }
};

} // namespace

TEST(CollisionSystem, CoincidentAgentsSeparateToAtLeastSumOfRadii) {
    TwoAgentFixture f({0.0f, 0.0f}, {0.0f, 0.0f});

    // A few updates to let relaxation converge.
    for (int i = 0; i < 5; ++i) {
        f.sys->update(0.0f);
    }

    auto* posA = f.world.getComponent<Position>(f.a);
    auto* posB = f.world.getComponent<Position>(f.b);
    ASSERT_NE(posA, nullptr);
    ASSERT_NE(posB, nullptr);

    float dx = posB->value.x - posA->value.x;
    float dy = posB->value.y - posA->value.y;
    float dist = std::sqrt(dx * dx + dy * dy);

    // They must be at least ~0.6 apart (2 * 0.3) — allow a small epsilon.
    EXPECT_GE(dist, 0.55f);

    // Neither should have teleported to infinity.
    EXPECT_LT(std::abs(posA->value.x), 5.0f);
    EXPECT_LT(std::abs(posA->value.y), 5.0f);
    EXPECT_LT(std::abs(posB->value.x), 5.0f);
    EXPECT_LT(std::abs(posB->value.y), 5.0f);
}

TEST(CollisionSystem, OverlappingAlongXSeparatesSymmetrically) {
    // Place them 0.2m apart along X, but their combined diameter is 0.6m —
    // so they overlap by 0.4m. Equal mass → each should move ~0.2m.
    TwoAgentFixture f({0.0f, 0.0f}, {0.2f, 0.0f});
    f.sys->update(0.0f);

    auto* posA = f.world.getComponent<Position>(f.a);
    auto* posB = f.world.getComponent<Position>(f.b);

    // B should be to the right of A after separation.
    EXPECT_LT(posA->value.x, posB->value.x);

    float dist = posB->value.x - posA->value.x;
    // Should have increased toward 0.6 (sum of radii).
    EXPECT_GT(dist, 0.2f);

    // Symmetric: A moved left roughly the same amount B moved right.
    // posA should be negative (pushed left) and posB positive (pushed right).
    EXPECT_LT(posA->value.x, 0.0f);
    EXPECT_GT(posB->value.x, 0.2f);
}

TEST(CollisionSystem, PinnedAgentDoesNotMove) {
    // Agent B is pinned (invMass=0); only A should move.
    TwoAgentFixture f({0.0f, 0.0f}, {0.2f, 0.0f},
                      0.3f, 0.3f,
                      /*invMassA=*/1.0f, /*invMassB=*/0.0f);

    glm::vec2 pinnedPos = {0.2f, 0.0f};
    f.sys->update(0.0f);

    auto* posA = f.world.getComponent<Position>(f.a);
    auto* posB = f.world.getComponent<Position>(f.b);

    // B must not move.
    EXPECT_FLOAT_EQ(posB->value.x, pinnedPos.x);
    EXPECT_FLOAT_EQ(posB->value.y, pinnedPos.y);

    // A must have moved left (away from B).
    EXPECT_LT(posA->value.x, 0.0f);
}

TEST(CollisionSystem, NonOverlappingAgentsAreUnchanged) {
    // Place agents 1.0m apart; sum of radii = 0.6m, so no overlap.
    TwoAgentFixture f({0.0f, 0.0f}, {1.0f, 0.0f});
    f.sys->update(0.0f);

    auto* posA = f.world.getComponent<Position>(f.a);
    auto* posB = f.world.getComponent<Position>(f.b);

    EXPECT_FLOAT_EQ(posA->value.x, 0.0f);
    EXPECT_FLOAT_EQ(posA->value.y, 0.0f);
    EXPECT_FLOAT_EQ(posB->value.x, 1.0f);
    EXPECT_FLOAT_EQ(posB->value.y, 0.0f);
}
