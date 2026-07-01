#include "PhysicsSystem.h"

#include "../World.h"
#include "../components/Movement.h"
#include "../components/Transform.h"
#include "TimeSystem.h"

namespace ecs {

void PhysicsSystem::update(float deltaTime) {
    // Integration runs on game time, so fast-forward (3x/10x) moves colonists faster and pause
    // freezes them, consistent with the clock, needs decay, and actions. TimeSystem is always
    // registered in-game; tools/tests that omit it fall back to real time (scale 1.0). MovementSystem
    // caps each step to the current waypoint, so the larger fast-forward step can't overshoot the
    // arrival threshold and strand a colonist circling its target.
    float timeScale = 1.0F;
    if (auto* timeSystem = world->tryGetSystem<TimeSystem>()) {
        timeScale = timeSystem->effectiveTimeScale();
    }
    const float scaledDt = deltaTime * timeScale;

    // Simple Euler integration: position += velocity * dt
    for (auto [entity, pos, vel] : world->view<Position, Velocity>()) {
        pos.value += vel.value * scaledDt;
    }
}

}  // namespace ecs
