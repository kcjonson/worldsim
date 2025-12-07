#include "MovementSystem.h"

#include "../World.h"
#include "../components/Movement.h"
#include "../components/Transform.h"

#include <cmath>
#include <glm/geometric.hpp>

namespace ecs {

void MovementSystem::update(float deltaTime) {
    (void)deltaTime;  // Not used directly - we set velocity, PhysicsSystem applies it

    // Process all entities with movement targets
    for (auto [entity, pos, vel, target] :
         m_world->view<Position, Velocity, MovementTarget>()) {
        if (!target.active) {
            continue;
        }

        glm::vec2 toTarget = target.target - pos.value;
        float distance = glm::length(toTarget);

        // Arrival threshold - stop when close enough
        constexpr float kArrivalThreshold = 0.1f;

        if (distance < kArrivalThreshold) {
            // Arrived at target
            vel.value = {0.0f, 0.0f};
            target.active = false;
            continue;
        }

        // Set velocity toward target at movement speed
        glm::vec2 direction = toTarget / distance;  // Normalize
        vel.value = direction * target.speed;
    }

    // Update facing direction based on velocity
    for (auto [entity, rot, vel] : m_world->view<Rotation, Velocity>()) {
        if (glm::length(vel.value) > 0.01f) {
            rot.radians = std::atan2(vel.value.y, vel.value.x);
        }
    }
}

}  // namespace ecs
