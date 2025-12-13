#include "MovementSystem.h"

#include "../World.h"
#include "../components/Movement.h"
#include "../components/Task.h"
#include "../components/Transform.h"

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

        // DEBUG: Log movement direction (only occasionally to avoid spam)
        static int frameCounter = 0;
        if (frameCounter++ % 60 == 0) {  // Log once per second at 60fps
            // Calculate cardinal direction for human-readable output
            const char* dirLabel = "UNKNOWN";
            if (std::abs(toTarget.x) > std::abs(toTarget.y)) {
                dirLabel = (toTarget.x > 0) ? "EAST" : "WEST";
            } else {
                // NOTE: In our world, +Y = SOUTH (Y-down convention)
                dirLabel = (toTarget.y > 0) ? "SOUTH" : "NORTH";
            }
            LOG_DEBUG(
                Engine,
                "[Movement] Entity %llu: pos=(%.2f, %.2f) -> target=(%.2f, %.2f), "
                "delta=(%.2f, %.2f), dist=%.2f, dir=%s",
                static_cast<unsigned long long>(entity),
                pos.value.x, pos.value.y,
                target.target.x, target.target.y,
                toTarget.x, toTarget.y,
                distance,
                dirLabel
            );
        }
    }

    // Update facing direction based on velocity
    for (auto [entity, rot, vel] : world->view<Rotation, Velocity>()) {
        if (glm::length(vel.value) > 0.01f) {
            rot.radians = std::atan2(vel.value.y, vel.value.x);
        }
    }
}

}  // namespace ecs
