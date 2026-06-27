#include "MovementSystem.h"

#include "../World.h"
#include "../components/FacingDirection.h"
#include "../components/Movement.h"
#include "../components/NavPath.h"
#include "../components/Task.h"
#include "../components/Transform.h"

#include <numbers>

#include <utils/Log.h>

#include <cmath>
#include <glm/geometric.hpp>

namespace ecs {

void MovementSystem::update(float deltaTime) {
    (void)deltaTime;  // Not used directly - we set velocity, PhysicsSystem applies it

    // Final-waypoint arrival threshold - stop when close enough. The same value gates arrival
    // whether the colonist is following a NavPath (the in-game route) or moving directly (only
    // the headless no-NavigationSystem case), so ActionSystem's arrival hand-offs fire at the
    // same distance either way.
    constexpr float kArrivalThreshold = 0.1f;

    // Intermediate-waypoint advance radius: a colonist need only get near a corner
    // before turning toward the next one, so this is looser than the final arrival.
    constexpr float kWaypointAdvance = 0.15f;

    // Process all entities with movement targets
    for (auto [entity, pos, vel, target] :
         world->view<Position, Velocity, MovementTarget>()) {
        if (!target.active) {
            continue;
        }

        // Navmesh path-following is THE way a colonist moves in the running game: steer along
        // the NavPath waypoints the AI planned. The direct-movement block below runs only when
        // no valid NavPath is present, which in-game cannot happen for a Moving task (the AI
        // requests a route, or HOLDS, before this system runs). It is exercised solely by the
        // headless test harness, which wires no NavigationSystem and so builds no mesh.
        auto* navPath = world->getComponent<NavPath>(entity);
        if (navPath != nullptr && navPath->valid && !navPath->done()) {
            glm::vec2 waypoint = navPath->waypoints[navPath->current];
            glm::vec2 toWaypoint = waypoint - pos.value;
            float distance = glm::length(toWaypoint);

            const bool isFinal = (navPath->current + 1 >= navPath->waypoints.size());
            const float threshold = isFinal ? kArrivalThreshold : kWaypointAdvance;

            if (distance < threshold) {
                ++navPath->current;
                if (navPath->done()) {
                    // Reached the goal: same arrival hand-off the direct-movement block performs.
                    vel.value = {0.0f, 0.0f};
                    target.active = false;
                    navPath->valid = false;

                    if (auto* task = world->getComponent<Task>(entity)) {
                        if (task->state == TaskState::Moving) {
                            task->state = TaskState::Arrived;
                        }
                    }
                    continue;
                }
                // Re-aim at the next waypoint this same frame.
                waypoint = navPath->waypoints[navPath->current];
                toWaypoint = waypoint - pos.value;
                distance = glm::length(toWaypoint);
            }

            if (distance > 0.0001f) {
                vel.value = (toWaypoint / distance) * target.speed;
            } else {
                vel.value = {0.0f, 0.0f};
            }
            continue;
        }

        glm::vec2 toTarget = target.target - pos.value;
        float distance = glm::length(toTarget);

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
