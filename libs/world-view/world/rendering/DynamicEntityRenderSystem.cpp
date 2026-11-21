#include "DynamicEntityRenderSystem.h"

#include <ecs/World.h>
#include <ecs/components/AgentRadius.h>
#include <ecs/components/Colonist.h>
#include <ecs/components/AnimationState.h>
#include <ecs/components/Appearance.h>
#include <ecs/components/FacingDirection.h>
#include <ecs/components/Movement.h>
#include <ecs/components/Packaged.h>
#include <ecs/components/ResourceStack.h>
#include <ecs/components/Transform.h>
#include <ecs/systems/PlayerVisibilitySystem.h>

#include "assets/AssetRegistry.h"
#include "assets/MotionEval.h"
#include "assets/placement/WorldDepthSort.h"		// computeAnchorY (canonical ground-contact rule)
#include "world/rendering/PackagedLayout.h"			// shared crate+item placement (no drift)
#include "world/rendering/PileLayout.h"				// shared loose-pile placement (no drift)
#include "world/rendering/TemplateBounds.h"

#include <cmath>
#include <unordered_map>

namespace worldview {

void DynamicEntityRenderSystem::update(float deltaTime) {
    renderData.clear();
    m_partXformStore.clear();

    auto& assetRegistry = assets::AssetRegistry::Get();
    const ecs::PlayerVisibilitySystem* visibility = ecs::PlayerVisibilitySystem::gate(*world);

    // Collect all entities with position and appearance
    for (auto [entity, pos, rot, appearance] :
         world->view<ecs::Position, ecs::Rotation, ecs::Appearance>()) {
        // A thing that moves by itself shows only while a colonist can see it (player-visibility.md 3.6);
        // colonists always draw.
        if (visibility != nullptr && world->getComponent<ecs::AgentRadius>(entity) != nullptr &&
            world->getComponent<ecs::Colonist>(entity) == nullptr && !visibility->isVisible(pos.value)) {
            continue;
        }

        // Check if entity is packaged
        auto* packaged = world->getComponent<ecs::Packaged>(entity);

        // Skip entities that are being carried by a colonist
        if (packaged != nullptr && packaged->beingCarried) {
            continue;
        }

        // Packaged furniture (not being carried): the item inside a two-layer crate via the
        // shared layout, so the packaged hit-test/outline match the drawn sprites. Pushed back,
        // item, front: equal anchorY + the stable depth sort keep that order.
        if (packaged != nullptr) {
            const worldview::PackagedLayout pl = worldview::packagedLayout(
                pos.value, appearance.defName, appearance.scale, appearance.colorTint,
                worldview::templateBounds(assetRegistry.getTemplate(appearance.defName, {}))
            );
            renderData.push_back(pl.crateBack);
            renderData.push_back(pl.item);
            renderData.push_back(pl.crateFront);
            continue;
        }

        // Normal (non-packaged) entity rendering
        assets::PlacedEntity placed;

        // Check if entity has FacingDirection for directional sprite selection
        std::string defName = appearance.defName;
        if (auto* facing = world->getComponent<ecs::FacingDirection>(entity)) {
            defName += ecs::directionSuffix(facing->direction);
        }

        placed.traits = appearance.traits;
        placed.templateId = assetRegistry.templateId(defName, appearance.traits);

        // A parts item enters the stream as its depth strips, each anchored on its own stretch of the
        // item's collider, its turn carried by the strip layout (rotatable-placed-items 8). Told by the
        // template's def, which takes no name lookup.
        if (const assets::AssetDefinition* templateDef = assetRegistry.templateDefinition(placed.templateId);
            templateDef != nullptr && templateDef->parts != nullptr) {
            pushItemStrips(placed.templateId, appearance, pos.value, assets::turnQuantaFromRadians(rot.radians));
            continue;
        }
        placed.defName = defName;

        // The mesh is about the def's pivot, which the entity's position is: drawn at it, as
        // the outline and hit-test place it.
        const auto*            mesh   = assetRegistry.getTemplate(placed.templateId);
        const Foundation::Rect bounds = worldview::templateBounds(mesh);
        const assets::MeshYExtent meshY{.minY = bounds.y, .maxY = bounds.y + bounds.height}; // maxY: the feet

        // Loose ground pile: a heap of sprites sized by its count, laid out by the shared pile
        // layout so the hit-test and outline match what's drawn.
        if (const auto* stack = world->getComponent<ecs::ResourceStack>(entity)) {
            const worldview::PileLayout pile = worldview::pileLayout(
                pos.value, defName, placed.templateId, appearance.scale, appearance.colorTint, bounds,
                worldview::pileItem(assetRegistry.getDefinition(appearance.defName)), stack->quantity
            );
            for (std::size_t i = 0; i < pile.count; ++i) {
                renderData.push_back(pile.sprites[i]);
            }
            continue;
        }

        placed.position = pos.value;
        placed.rotation = 0.0F;  // Dynamic entities don't rotate - use FacingDirection for sprites
        placed.scale = appearance.scale;
        placed.colorTint = appearance.colorTint;
        // Feet, or a footprint's lower edge (a tree's trunk foot), are the 2.5D depth anchor.
        placed.anchorY = assets::computeAnchorY(
            placed.position.y, assets::anchorLocalY(assetRegistry.getDefinition(defName), placed.traits.ageClass, meshY, 0.0F), placed.scale
        );

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

void DynamicEntityRenderSystem::pushItemStrips(uint32_t templateId, const ecs::Appearance& appearance, glm::vec2 position, uint32_t turnQuanta) {
    const assets::ItemLayout* layout = assets::AssetRegistry::Get().stripLayout(templateId, turnQuanta);
    if (layout == nullptr) {
        return;
    }
    // Carried by template id and layout: no name on the records.
    for (uint32_t strip = 0; strip < layout->layout.strips.size(); ++strip) {
        assets::PlacedEntity placed;
        placed.traits = appearance.traits;
        placed.templateId = templateId;
        placed.position = position;
        placed.colorTint = appearance.colorTint;
        placed.anchorY = assets::computeAnchorY(position.y, layout->layout.strips[strip].anchorLocalY, 1.0F);
        placed.itemLayout = layout;
        placed.strip = strip;
        renderData.push_back(std::move(placed));
    }
}

}  // namespace worldview
