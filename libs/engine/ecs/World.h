#pragma once

#include "ISystem.h"
#include "Registry.h"
#include "View.h"

#include <algorithm>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace ecs {

/// Top-level ECS container owning the Registry and all Systems.
/// Provides entity management and system scheduling.
class World {
public:
    World() = default;
    ~World() = default;

    // Non-copyable, movable
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = default;
    World& operator=(World&&) = default;

    // ─────────────────────────────────────────────────────────────────────────
    // Entity Management (delegated to Registry)
    // ─────────────────────────────────────────────────────────────────────────

    /// Create a new entity
    [[nodiscard]] EntityID createEntity() {
        return m_registry.createEntity();
    }

    /// Destroy an entity
    void destroyEntity(EntityID entity) {
        m_registry.destroyEntity(entity);
    }

    /// Check if entity is alive
    [[nodiscard]] bool isAlive(EntityID entity) const {
        return m_registry.isAlive(entity);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Component Management (delegated to Registry)
    // ─────────────────────────────────────────────────────────────────────────

    /// Add a component to an entity
    template <typename T, typename... Args>
    T& addComponent(EntityID entity, Args&&... args) {
        return m_registry.addComponent<T>(entity, std::forward<Args>(args)...);
    }

    /// Get a component from an entity
    template <typename T>
    [[nodiscard]] T* getComponent(EntityID entity) {
        return m_registry.getComponent<T>(entity);
    }

    /// Get a component from an entity (const)
    template <typename T>
    [[nodiscard]] const T* getComponent(EntityID entity) const {
        return m_registry.getComponent<T>(entity);
    }

    /// Check if entity has a component
    template <typename T>
    [[nodiscard]] bool hasComponent(EntityID entity) const {
        return m_registry.hasComponent<T>(entity);
    }

    /// Remove a component from an entity
    template <typename T>
    void removeComponent(EntityID entity) {
        m_registry.removeComponent<T>(entity);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // View (Query) System
    // ─────────────────────────────────────────────────────────────────────────

    /// Create a view to iterate entities with specific components
    template <typename... Components>
    [[nodiscard]] View<Components...> view() {
        return View<Components...>(m_registry);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // System Management
    // ─────────────────────────────────────────────────────────────────────────

    /// Register a system with the world
    template <typename T, typename... Args>
    T& registerSystem(Args&&... args) {
        static_assert(std::is_base_of_v<ISystem, T>, "T must derive from ISystem");

        auto system = std::make_unique<T>(std::forward<Args>(args)...);
        system->setWorld(this);
        T& ref = *system;

        auto typeIndex = std::type_index(typeid(T));
        m_systemMap[typeIndex] = system.get();
        m_systems.push_back(std::move(system));
        m_sorted = false;

        return ref;
    }

    /// Get a registered system by type
    template <typename T>
    [[nodiscard]] T& getSystem() {
        auto it = m_systemMap.find(std::type_index(typeid(T)));
        assert(it != m_systemMap.end() && "System not registered");
        return *static_cast<T*>(it->second);
    }

    /// Get a registered system by type (const)
    template <typename T>
    [[nodiscard]] const T& getSystem() const {
        auto it = m_systemMap.find(std::type_index(typeid(T)));
        assert(it != m_systemMap.end() && "System not registered");
        return *static_cast<const T*>(it->second);
    }

    /// Update all systems in priority order
    void update(float deltaTime) {
        sortSystemsIfNeeded();

        for (auto& system : m_systems) {
            system->update(deltaTime);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Registry Access
    // ─────────────────────────────────────────────────────────────────────────

    /// Get direct access to the registry
    [[nodiscard]] Registry& getRegistry() {
        return m_registry;
    }

    /// Get direct access to the registry (const)
    [[nodiscard]] const Registry& getRegistry() const {
        return m_registry;
    }

private:
    void sortSystemsIfNeeded() {
        if (m_sorted) {
            return;
        }

        std::sort(m_systems.begin(), m_systems.end(),
                  [](const std::unique_ptr<ISystem>& a, const std::unique_ptr<ISystem>& b) {
                      return a->priority() < b->priority();
                  });

        m_sorted = true;
    }

    Registry m_registry;
    std::vector<std::unique_ptr<ISystem>> m_systems;
    std::unordered_map<std::type_index, ISystem*> m_systemMap;
    bool m_sorted = false;
};

}  // namespace ecs
