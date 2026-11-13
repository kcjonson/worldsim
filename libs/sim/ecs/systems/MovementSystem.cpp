#include "MovementSystem.h"

#include "../World.h"
#include "../components/FacingDirection.h"
#include "../components/Movement.h"
#include "../components/NavPath.h"
#include "../components/Task.h"
#include "../components/Transform.h"
#include "NavigationSystem.h"
#include "TimeSystem.h"

#include <numbers>

#include <utils/Log.h>

#include <algorithm>
#include <cmath>
#include <vector>
#include <glm/geometric.hpp>

namespace ecs {

namespace {
// When fast-forwarding, cap the per-tick speed so one scaled integration step lands on the target
// at `distance` instead of overshooting it. PhysicsSystem integrates pos += vel * deltaTime *
// timeScale, so at 10x a 2 m/s colonist would step ~0.33 m -- past the 0.1 m arrival threshold --
// and circle its goal without ever registering Arrived. Capping the step also stops a colonist
// cutting across a path corner (through a wall) when a big step would skip a waypoint. A no-op at 1x
// (scaledDt == deltaTime) and in TimeSystem-less tests, so existing movement behavior is unchanged.
float clampStepSpeed(float speed, float distance, float scaledDt, bool fastForward) {
    if (fastForward && speed * scaledDt > distance) {
        return distance / scaledDt;
    }
    return speed;
}
}  // namespace

void MovementSystem::update(float deltaTime) {
    // Movement runs on game time: PhysicsSystem scales the integration step by the same factor, and
    // a route's step is spent along it so fast-forward can't overshoot a waypoint; the direct
    // movement below caps its step the same way (see clampStepSpeed). At 1x and in tests without a
    // TimeSystem, timeScale is 1.0.
    float timeScale = 1.0F;
    if (auto* timeSystem = world->tryGetSystem<TimeSystem>()) {
        timeScale = timeSystem->effectiveTimeScale();
    }
    const float scaledDt = deltaTime * timeScale;
    const bool  fastForward = scaledDt > deltaTime;
    const NavigationSystem* nav = world->tryGetSystem<NavigationSystem>();

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
        // An inactive target means standing still, whoever switched it off (arrival here, an
        // in-place need action, a nav hold, taking control). Zeroing here, before PhysicsSystem
        // integrates, is the one stop: a colonist never coasts on the last stride's velocity.
        if (!target.active) {
            vel.value = {0.0f, 0.0f};
            continue;
        }

        // Navmesh path-following is THE way a colonist moves in the running game: steer along
        // the NavPath waypoints the AI planned. The direct-movement block below runs only when
        // no valid NavPath is present, which in-game cannot happen for a Moving task (the AI
        // requests a route, or HOLDS, before this system runs). It is exercised solely by the
        // headless test harness, which wires no NavigationSystem and so builds no mesh.
        auto* navPath = world->getComponent<NavPath>(entity);
        if (navPath != nullptr && navPath->valid && !navPath->done()) {
            const std::vector<glm::vec2>& waypoints = navPath->waypoints;
            const auto isFinal = [&] { return navPath->current + 1 >= waypoints.size(); };

            if (isFinal() && glm::distance(pos.value, waypoints[navPath->current]) < kArrivalThreshold) {
                // Reached the goal: same arrival hand-off the direct-movement block performs.
                ++navPath->current;
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
            // Corners within the advance radius of where it stands count as rounded.
            while (!isFinal() && glm::distance(pos.value, waypoints[navPath->current]) < kWaypointAdvance) {
                ++navPath->current;
            }

            // This frame's stride is spent along the route, through every corner it reaches, so
            // corners closer together than a stride (a route threading trees) are walked, never
            // overshot and stepped back to; the stride ends on the final waypoint, not past it. Each
            // leg of it stops where he would see past meshed ground, and a corner he stops short of
            // is not rounded.
            const auto within = [&](glm::vec2 from, glm::vec2 to) { return nav != nullptr ? nav->stepWithinSight(entity, from, to) : to; };
            float     left = target.speed * scaledDt;
            glm::vec2 at = pos.value;
            while (!isFinal() && glm::distance(at, waypoints[navPath->current]) <= left) {
                const glm::vec2 corner = within(at, waypoints[navPath->current]);
                if (corner != waypoints[navPath->current]) {
                    at = corner;
                    left = 0.0f;
                    break;
                }
                left -= glm::distance(at, corner);
                at = corner;
                ++navPath->current;
            }
            const glm::vec2 toWaypoint = waypoints[navPath->current] - at;
            const float     distance = glm::length(toWaypoint);
            const glm::vec2 end = within(at, distance > 0.0001f ? at + toWaypoint * (std::min(left, distance) / distance) : at);
            if (scaledDt > 0.0f) {
                vel.value = (end - pos.value) / scaledDt;
            } else {
                // Paused: nothing moves, but the mover still faces where it is going.
                const glm::vec2 heading = waypoints[navPath->current] - pos.value;
                const float     headingLength = glm::length(heading);
                vel.value = headingLength > 0.0001f ? heading * (target.speed / headingLength) : glm::vec2{0.0f, 0.0f};
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
        vel.value = direction * clampStepSpeed(target.speed, distance, scaledDt, fastForward);
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

            // World +Y draws lower on screen (toward the viewer), so +Y velocity is Down.
            // Right: -45° to 45° (-0.25π to 0.25π)
            // Down: 45° to 135° (0.25π to 0.75π)
            // Left: 135° to -135° (0.75π to -0.75π, wrapping)
            // Up: -135° to -45° (-0.75π to -0.25π)
            if (angle >= -kQuarterPi && angle < kQuarterPi) {
                facing.direction = CardinalDirection::Right;
            } else if (angle >= kQuarterPi && angle < 3.0f * kQuarterPi) {
                facing.direction = CardinalDirection::Down;
            } else if (angle >= -3.0f * kQuarterPi && angle < -kQuarterPi) {
                facing.direction = CardinalDirection::Up;
            } else {
                facing.direction = CardinalDirection::Left;
            }
        }
    }
}

}  // namespace ecs
