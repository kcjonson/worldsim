#pragma once

namespace ecs {

class World;

/// Base interface for all ECS systems.
/// Systems process entities with specific component combinations.
class ISystem {
public:
    virtual ~ISystem() = default;

    /// Called each frame to update the system
    /// @param deltaTime Time elapsed since last frame in seconds
    virtual void update(float deltaTime) = 0;

    /// Get the priority of this system. Lower values run first.
    /// Recommended ranges:
    /// - 0-99: Input handling
    /// - 100-199: AI and movement decisions
    /// - 200-299: Physics and position updates
    /// - 300-899: Game logic
    /// - 900-999: Rendering preparation
    [[nodiscard]] virtual int priority() const = 0;

    /// Set the world reference (called by World::registerSystem)
    void setWorld(World* world) {
        m_world = world;
    }

protected:
    World* m_world = nullptr;
};

}  // namespace ecs
