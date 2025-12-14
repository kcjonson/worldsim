#include "MovementSystem.h"

#include "../World.h"
#include "../components/FacingDirection.h"
#include "../components/Movement.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <numbers>

#include <utils/Log.h>

#include <cmath>
#include <glm/geometric.hpp>

namespace ecs {

void MovementSystem::update(float deltaTime) {
    (void)deltaTime;  // Not used directly - we set velocity, PhysicsSystem applies it

    // Process all entities with movement targets
    for (auto [entity, pos, vel, target] :
         world->view<Position, Velocity, MovementTarget>()) {
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

            // Update Task state if entity has a Task component
            if (auto* task = world->getComponent<Task>(entity)) {
                if (task->state == TaskState::Moving) {
                    task->state = TaskState::Arrived;
                }
            }
            continue;
        }

        // Set velocity toward target at movement speed
        glm::vec2 direction = toTarget / distance;  // Normalize
        vel.value = direction * target.speed;
    }

    // Update facing direction based on velocity
    for (auto [entity, rot, vel] : world->view<Rotation, Velocity>()) {
        if (glm::length(vel.value) > 0.01f) {
            rot.radians = std::atan2(vel.value.y, vel.value.x);
        }
    }

    // Update FacingDirection for directional sprite selection (4-way quantization)
    for (auto [entity, facing, vel] : world->view<FacingDirection, Velocity>()) {
        if (glm::length(vel.value) > 0.01f) {
            // Quantize angle to 4 cardinal directions
            float angle = std::atan2(vel.value.y, vel.value.x);
            constexpr float kQuarterPi = 0.25f * std::numbers::pi_v<float>;

            // Right: -45° to 45° (-0.25π to 0.25π)
            // Up: 45° to 135° (0.25π to 0.75π)
            // Left: 135° to -135° (0.75π to -0.75π, wrapping)
            // Down: -135° to -45° (-0.75π to -0.25π)
            if (angle >= -kQuarterPi && angle < kQuarterPi) {
                facing.direction = CardinalDirection::Right;
            } else if (angle >= kQuarterPi && angle < 3.0f * kQuarterPi) {
                facing.direction = CardinalDirection::Up;
            } else if (angle >= -3.0f * kQuarterPi && angle < -kQuarterPi) {
                facing.direction = CardinalDirection::Down;
            } else {
                facing.direction = CardinalDirection::Left;
            }
        }
    }
}

}  // namespace ecs
