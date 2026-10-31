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

/// Suffix appended to a base defName to pick the directional sprite/template variant
/// (e.g. "Colonist" -> "Colonist_down"). The single source shared by the render path
/// (DynamicEntityRenderSystem) and selection (silhouette lookup) so they can't drift.
inline const char* directionSuffix(CardinalDirection dir) {
    switch (dir) {
        case CardinalDirection::Up:    return "_up";
        case CardinalDirection::Down:  return "_down";
        case CardinalDirection::Left:  return "_left";
        case CardinalDirection::Right: return "_right";
    }
    return "_down";
}

}  // namespace ecs
