#include "DynamicEntityRenderSystem.h"

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/FacingDirection.h"
#include "../components/Transform.h"

namespace ecs {

namespace {

/// Get direction suffix for asset name based on FacingDirection
const char* getDirectionSuffix(CardinalDirection dir) {
    switch (dir) {
        case CardinalDirection::Up:    return "_up";
        case CardinalDirection::Down:  return "_down";
        case CardinalDirection::Left:  return "_left";
        case CardinalDirection::Right: return "_right";
    }
    return "_down";  // Default fallback
}

}  // namespace

void DynamicEntityRenderSystem::update(float /*deltaTime*/) {
    renderData.clear();

    // Collect all entities with position and appearance
    for (auto [entity, pos, rot, appearance] :
         world->view<Position, Rotation, Appearance>()) {
        engine::assets::PlacedEntity placed;

        // Check if entity has FacingDirection for directional sprite selection
        std::string defName = appearance.defName;
        if (auto* facing = world->getComponent<FacingDirection>(entity)) {
            defName += getDirectionSuffix(facing->direction);
        }

        placed.defName = defName;
        // Offset sprite so entity position is at feet (bottom-center)
        // SVG coords start at (0,0) top-left, so we need to:
        // - Shift X left by half width to center horizontally
        // - Shift Y to place feet at entity position
        // Colonist: 60x87 SVG units, scaled to ~0.69x1.0m world units
        constexpr float kSpriteWidthOffset = -0.35f;   // Half of ~0.69m width
        constexpr float kSpriteHeightOffset = -0.5f;   // Adjust for feet position
        placed.position = glm::vec2(pos.value.x + kSpriteWidthOffset, pos.value.y + kSpriteHeightOffset);
        placed.rotation = 0.0f;  // Dynamic entities don't rotate - use FacingDirection for sprites
        placed.scale = appearance.scale;
        placed.colorTint = appearance.colorTint;

        renderData.push_back(std::move(placed));
    }
}

}  // namespace ecs
