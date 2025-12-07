#pragma once

#include "../ISystem.h"

namespace ecs {

/// Integrates velocity into position.
/// Priority: 200 (runs after MovementSystem)
class PhysicsSystem : public ISystem {
public:
    void update(float deltaTime) override;

    [[nodiscard]] int priority() const override {
        return 200;
    }
};

}  // namespace ecs
