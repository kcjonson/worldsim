#include "DynamicEntityRenderSystem.h"

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/FacingDirection.h"
#include "../components/Packaged.h"
#include "../components/Transform.h"

#include "assets/AssetRegistry.h"

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

    // Constants for packaged item rendering
    constexpr float kPackagedScaleFactor = 0.85F;  // Scale packaged items to 85% of tile size
    constexpr float kCrateWorldHeight = 0.2F;      // PackagingCrate's worldHeight
    constexpr float kCrateWidth = 1.0F;            // PackagingCrate is 1m wide
    constexpr float kItemLiftOffset = 0.03F;       // Lift item up slightly (~2px at typical zoom)

    auto& assetRegistry = engine::assets::AssetRegistry::Get();

    // Collect all entities with position and appearance
    for (auto [entity, pos, rot, appearance] :
         world->view<Position, Rotation, Appearance>()) {
        // Check if entity is packaged
        auto* packaged = world->getComponent<Packaged>(entity);

        // Skip entities that are being carried by a colonist
        if (packaged != nullptr && packaged->beingCarried) {
            continue;
        }

        // For packaged entities (not being carried), render with crate overlay
        if (packaged != nullptr) {
            // Get item's worldHeight for proper bottom alignment
            float itemWorldHeight = 0.6F;  // Default fallback
            if (const auto* itemDef = assetRegistry.getDefinition(appearance.defName)) {
                itemWorldHeight = itemDef->worldHeight;
            }
            float scaledItemHeight = itemWorldHeight * kPackagedScaleFactor;

            // Entity position is the bottom/baseline - offset each sprite by its height
            // so both bottoms align at the entity position
            float bottomY = pos.value.y;

            // Crate is centered on entity position
            float crateCenterX = pos.value.x;
            float crateLeftX = crateCenterX - kCrateWidth * 0.5F;

            // First render the crate (so it appears behind the item)
            engine::assets::PlacedEntity crate;
            crate.defName = "PackagingCrate";
            crate.position = glm::vec2(crateLeftX, bottomY - kCrateWorldHeight);
            crate.rotation = 0.0F;
            crate.scale = 1.0F;
            crate.colorTint = glm::vec4(1.0F, 1.0F, 1.0F, 1.0F);
            renderData.push_back(std::move(crate));

            // Then render the packaged item (shrunk, centered in crate)
            engine::assets::PlacedEntity item;
            item.defName = appearance.defName;
            // Estimate item width from height (assume ~1.4:1 aspect ratio like BasicBox 40x28)
            constexpr float kItemAspectRatio = 1.4F;
            float scaledItemWidth = itemWorldHeight * kItemAspectRatio * kPackagedScaleFactor;
            float itemLeftX = crateCenterX - scaledItemWidth * 0.5F;
            item.position = glm::vec2(itemLeftX, bottomY - scaledItemHeight - kItemLiftOffset);
            item.rotation = 0.0F;
            item.scale = appearance.scale * kPackagedScaleFactor;
            item.colorTint = appearance.colorTint;
            renderData.push_back(std::move(item));
            continue;
        }

        // Normal (non-packaged) entity rendering
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
        // Colonist: 60x100 SVG units, scaled to ~0.69x1.0m world units
        constexpr float kSpriteWidthOffset = -0.35F;   // Half of ~0.69m width
        constexpr float kSpriteHeightOffset = -0.5F;   // Adjust for feet position
        placed.position = glm::vec2(pos.value.x + kSpriteWidthOffset, pos.value.y + kSpriteHeightOffset);
        placed.rotation = 0.0F;  // Dynamic entities don't rotate - use FacingDirection for sprites
        placed.scale = appearance.scale;
        placed.colorTint = appearance.colorTint;

        renderData.push_back(std::move(placed));
    }
}

}  // namespace ecs
