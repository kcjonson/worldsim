#pragma once

#include <glm/vec2.hpp>

namespace ecs {

/// World position of an entity
struct Position {
    glm::vec2 value{0.0f, 0.0f};
};

/// Facing direction of an entity
struct Rotation {
    float radians = 0.0f;  // 0 = facing right (+X), PI/2 = facing up (+Y)
};

}  // namespace ecs
