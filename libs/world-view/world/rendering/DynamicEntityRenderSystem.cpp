#include "DynamicEntityRenderSystem.h"

#include <ecs/World.h>
#include <ecs/components/AnimationState.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/FacingDirection.h>
#include <ecs/components/Movement.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/Transform.h>

#include "assets/AssetRegistry.h"
#include "assets/MotionEval.h"
#include "assets/placement/WorldDepthSort.h"		// computeAnchorY (canonical ground-contact rule)
#include "world/rendering/AssetInstanceTransform.h" // shared local<->world affine (no drift)
#include "world/rendering/PackagedLayout.h"			// shared crate+item placement (no drift)

#include <cmath>
#include <unordered_map>

namespace worldview {

void DynamicEntityRenderSystem::update(float deltaTime) {
    renderData.clear();
    m_partXformStore.clear();

    auto& assetRegistry = assets::AssetRegistry::Get();

    // Collect all entities with position and appearance
    for (auto [entity, pos, rot, appearance] :
         world->view<ecs::Position, ecs::Rotation, ecs::Appearance>()) {
        // Check if entity is packaged
        auto* packaged = world->getComponent<ecs::Packaged>(entity);

        // Skip entities that are being carried by a colonist
        if (packaged != nullptr && packaged->beingCarried) {
            continue;
        }

        // For packaged entities (not being carried), render crate-behind-item via the
        // shared layout so the packaged hit-test/outline match the drawn sprite.
        if (packaged != nullptr) {
            float itemWorldHeight = 0.6F; // fallback
            if (const auto* itemDef = assetRegistry.getDefinition(appearance.defName)) {
                itemWorldHeight = itemDef->worldHeight;
            }
            const worldview::PackagedLayout pl = worldview::packagedLayout(
                pos.value, appearance.defName, appearance.scale, appearance.colorTint, itemWorldHeight
            );
            renderData.push_back(pl.crate); // crate first: drawn behind the item
            renderData.push_back(pl.item);
            continue;
        }

        // Normal (non-packaged) entity rendering
        assets::PlacedEntity placed;

        // Check if entity has FacingDirection for directional sprite selection
        std::string defName = appearance.defName;
        if (auto* facing = world->getComponent<ecs::FacingDirection>(entity)) {
            defName += ecs::directionSuffix(facing->direction);
        }

        placed.defName = defName;

        // Re-center the mesh on its bbox (entity position is the center) and derive the
        // world transform through the shared helper so render, outline, and hit-test agree.
        glm::vec2   boundsCenter{0.0F, 0.0F};
        float       meshMaxY = 0.0F;  // Bottom-most local-Y (feet); drives the depth anchor
        const auto* mesh	 = assetRegistry.getTemplate(defName);
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
            boundsCenter = {(minX + maxX) * 0.5F, (minY + maxY) * 0.5F};
            meshMaxY	 = maxY;
        }

        placed.position = worldview::dynamicInstanceTransform(pos.value, appearance.scale, boundsCenter).origin;
        placed.rotation = 0.0F;  // Dynamic entities don't rotate - use FacingDirection for sprites
        placed.scale = appearance.scale;
        placed.colorTint = appearance.colorTint;
        // Feet (bottom-most world-Y) are the 2.5D depth anchor.
        placed.anchorY = assets::computeAnchorY(placed.position.y, meshMaxY, placed.scale);

        // Walk animation: advance this entity's phase from its movement speed and evaluate its
        // motion clip into per-part transforms (aligned to the template's MeshParts). Only entities
        // with an AnimationState + a resolved motion + a parted mesh animate; when standing they
        // reset to the rest pose and fall back to the fast instanced render path.
        if (auto* anim = world->getComponent<ecs::AnimationState>(entity);
            anim != nullptr && mesh != nullptr && !mesh->parts.empty()) {
            const assets::MotionDef* motion = assetRegistry.getMotion(defName);
            if (motion != nullptr && !motion->clips.empty()) {
                float speed = 0.0F;
                if (auto* vel = world->getComponent<ecs::Velocity>(entity)) {
                    speed = std::sqrt(vel->value.x * vel->value.x + vel->value.y * vel->value.y);
                }
                const assets::MotionClip* walk = motion->findClip("walk");
                const assets::MotionClip* clip = (walk != nullptr) ? walk : &motion->clips.front();
                constexpr float kMoveEps = 0.05F; // m/s below which the colonist is "standing"
                if (speed > kMoveEps) {
                    const float stride = (clip->stride > 0.01F) ? clip->stride : 0.85F;
                    anim->phase += speed * deltaTime / stride;
                    anim->phase -= std::floor(anim->phase);

                    std::unordered_map<std::string, assets::PartTransform> xforms;
                    assets::evaluateClip(*clip, anim->phase, xforms);
                    if (!xforms.empty()) {
                        std::vector<assets::PartTransform> aligned(mesh->parts.size());
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

}  // namespace worldview
