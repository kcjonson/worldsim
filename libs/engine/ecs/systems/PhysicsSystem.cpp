#include "PhysicsSystem.h"

#include "../World.h"
#include "../components/Movement.h"
#include "../components/Transform.h"

namespace ecs {

void PhysicsSystem::update(float deltaTime) {
    // Simple Euler integration: position += velocity * dt
    for (auto [entity, pos, vel] : m_world->view<Position, Velocity>()) {
        pos.value += vel.value * deltaTime;
    }
}

}  // namespace ecs
