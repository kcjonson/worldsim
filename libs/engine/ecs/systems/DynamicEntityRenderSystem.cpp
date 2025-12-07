#include "DynamicEntityRenderSystem.h"

#include "../World.h"
#include "../components/Appearance.h"
#include "../components/Transform.h"

namespace ecs {

void DynamicEntityRenderSystem::update(float /*deltaTime*/) {
    renderData.clear();

    // Collect all entities with position and appearance
    for (auto [entity, pos, rot, appearance] :
         world->view<Position, Rotation, Appearance>()) {
        engine::assets::PlacedEntity placed;
        placed.defName = appearance.defName;
        placed.position = pos.value;
        placed.rotation = rot.radians;
        placed.scale = appearance.scale;
        placed.colorTint = appearance.colorTint;

        renderData.push_back(std::move(placed));
    }
}

}  // namespace ecs
