#pragma once

#include "assets/placement/SpatialIndex.h"
#include <ecs/ISystem.h>

#include <deque>
#include <vector>

namespace worldview {

/// Collects renderable entities and produces PlacedEntity data for EntityRenderer.
/// Priority: 900 (runs late, after all movement/physics updates)
class DynamicEntityRenderSystem : public ecs::ISystem {
public:
    void update(float deltaTime) override;

    [[nodiscard]] int priority() const override { return 900; }
    [[nodiscard]] const char* name() const override { return "DynamicEntityRender"; }

    /// Get the render data for this frame.
    /// Call this after update() to get entities for rendering.
    [[nodiscard]] const std::vector<assets::PlacedEntity>& getRenderData() const {
        return renderData;
    }

private:
    std::vector<assets::PlacedEntity> renderData;

    // Per-frame backing store for animated entities' per-part transforms. A deque so push_back
    // keeps earlier entries' addresses stable (PlacedEntity::partTransforms points in here).
    // Cleared at the top of each update.
    std::deque<std::vector<assets::PartTransform>> m_partXformStore;
};

}  // namespace worldview
