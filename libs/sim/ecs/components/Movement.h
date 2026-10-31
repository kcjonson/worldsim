#pragma once

#include <glm/vec2.hpp>

namespace ecs {

/// Current velocity of an entity
struct Velocity {
    glm::vec2 value{0.0f, 0.0f};
};

/// Target position for movement
struct MovementTarget {
    glm::vec2 target{0.0f, 0.0f};
    float speed = 2.0f;   // meters per second
    bool active = false;  // whether entity is moving toward target
};

}  // namespace ecs
