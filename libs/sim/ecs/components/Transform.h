#pragma once

#include <glm/vec2.hpp>

namespace ecs {

/// World position of an entity
struct Position {
    glm::vec2 value{0.0f, 0.0f};
};

/// The turn of an entity's local frame: radians, positive from +X toward +Y, clockwise on the screen
/// (rotatable-placed-items 3). A placed item's front is its local +Y side, so at 0 it faces down the screen.
/// MovementSystem writes a mover's heading here, the direction of its velocity (0 heading +X); a mover draws
/// by FacingDirection, not by this.
struct Rotation {
    float radians = 0.0f;
};

}  // namespace ecs
