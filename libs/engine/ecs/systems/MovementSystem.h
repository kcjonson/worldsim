#pragma once

#include "../ISystem.h"

namespace ecs {

/// Processes entities with MovementTarget to set their velocity.
/// Priority: 100 (runs before PhysicsSystem)
class MovementSystem : public ISystem {
public:
    void update(float deltaTime) override;

    [[nodiscard]] int priority() const override { return 100; }
    [[nodiscard]] const char* name() const override { return "Movement"; }
};

}  // namespace ecs
