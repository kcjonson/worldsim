#pragma once

#include "../ISystem.h"
#include "assets/placement/SpatialIndex.h"

#include <vector>

namespace ecs {

/// Collects renderable entities and produces PlacedEntity data for EntityRenderer.
/// Priority: 900 (runs late, after all movement/physics updates)
class DynamicEntityRenderSystem : public ISystem {
public:
    void update(float deltaTime) override;

    [[nodiscard]] int priority() const override {
        return 900;
    }

    /// Get the render data for this frame.
    /// Call this after update() to get entities for rendering.
    [[nodiscard]] const std::vector<engine::assets::PlacedEntity>& getRenderData() const {
        return m_renderData;
    }

private:
    std::vector<engine::assets::PlacedEntity> m_renderData;
};

}  // namespace ecs
