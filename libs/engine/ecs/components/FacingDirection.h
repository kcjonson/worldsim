#pragma once

namespace ecs {

/// Cardinal direction for sprite selection (4-way directional sprites)
enum class CardinalDirection {
    Up,     // +Y direction (facing away from camera in top-down view)
    Down,   // -Y direction (facing toward camera - default for colonists)
    Left,   // -X direction
    Right   // +X direction
};

/// Component tracking entity's facing direction for directional sprite selection.
/// Updated by MovementSystem based on velocity, read by DynamicEntityRenderSystem
/// to select the appropriate directional sprite variant.
struct FacingDirection {
    CardinalDirection direction = CardinalDirection::Down;  // Default: facing camera
};

}  // namespace ecs
