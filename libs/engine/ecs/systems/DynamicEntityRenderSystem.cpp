#include "DynamicEntityRenderSystem.h"

#include "../World.h"
#include "../components/AnimationState.h"
#include "../components/Appearance.h"
#include "../components/FacingDirection.h"
#include "../components/Movement.h"
#include "../components/Packaged.h"
#include "../components/Transform.h"

#include "assets/AssetRegistry.h"
#include "assets/MotionEval.h"

#include <cmath>
#include <unordered_map>

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

void DynamicEntityRenderSystem::update(float deltaTime) {
    renderData.clear();
    m_partXformStore.clear();

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

        // Calculate centering offset from mesh bounds
        // Entity position is the center - we need to offset so the mesh renders centered
        float centerOffsetX = 0.0F;
        float centerOffsetY = 0.0F;
        const auto* mesh = assetRegistry.getTemplate(defName);
        if (mesh != nullptr && !mesh->vertices.empty()) {
            float minX = mesh->vertices[0].x;
            float maxX = mesh->vertices[0].x;
            float minY = mesh->vertices[0].y;
            float maxY = mesh->vertices[0].y;
            for (const auto& v : mesh->vertices) {
                minX = std::min(minX, v.x);
                maxX = std::max(maxX, v.x);
                minY = std::min(minY, v.y);
                maxY = std::max(maxY, v.y);
            }
            centerOffsetX = -(minX + maxX) * 0.5F;
            centerOffsetY = -(minY + maxY) * 0.5F;
        }

        placed.position = glm::vec2(pos.value.x + centerOffsetX, pos.value.y + centerOffsetY);
        placed.rotation = 0.0F;  // Dynamic entities don't rotate - use FacingDirection for sprites
        placed.scale = appearance.scale;
        placed.colorTint = appearance.colorTint;

        // Walk animation: advance this entity's phase from its movement speed and evaluate its
        // motion clip into per-part transforms (aligned to the template's MeshParts). Only entities
        // with an AnimationState + a resolved motion + a parted mesh animate; when standing they
        // reset to the rest pose and fall back to the fast instanced render path.
        if (auto* anim = world->getComponent<AnimationState>(entity);
            anim != nullptr && mesh != nullptr && !mesh->parts.empty()) {
            const engine::assets::MotionDef* motion = assetRegistry.getMotion(defName);
            if (motion != nullptr && !motion->clips.empty()) {
                float speed = 0.0F;
                if (auto* vel = world->getComponent<Velocity>(entity)) {
                    speed = std::sqrt(vel->value.x * vel->value.x + vel->value.y * vel->value.y);
                }
                const engine::assets::MotionClip* walk = motion->findClip("walk");
                const engine::assets::MotionClip* clip = (walk != nullptr) ? walk : &motion->clips.front();
                constexpr float kMoveEps = 0.05F; // m/s below which the colonist is "standing"
                if (speed > kMoveEps) {
                    const float stride = (clip->stride > 0.01F) ? clip->stride : 0.85F;
                    anim->phase += speed * deltaTime / stride;
                    anim->phase -= std::floor(anim->phase);

                    std::unordered_map<std::string, engine::assets::PartTransform> xforms;
                    engine::assets::evaluateClip(*clip, anim->phase, xforms);
                    if (!xforms.empty()) {
                        std::vector<engine::assets::PartTransform> aligned(mesh->parts.size());
                        for (size_t k = 0; k < mesh->parts.size(); ++k) {
                            auto it = xforms.find(mesh->parts[k].name);
                            if (it != xforms.end()) {
                                aligned[k] = it->second;
                            }
                        }
                        m_partXformStore.push_back(std::move(aligned));
                        placed.partTransforms = &m_partXformStore.back();
                    }
                } else {
                    anim->phase = 0.0F; // stand at rest
                }
            }
        }

        renderData.push_back(std::move(placed));
    }
}

}  // namespace ecs
