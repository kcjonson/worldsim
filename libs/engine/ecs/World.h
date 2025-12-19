#pragma once

#include "ISystem.h"
#include "Registry.h"
#include "View.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <typeindex>
#include <unordered_map>
#include <vector>

// Enable ECS system timing (set to 0 for release builds if profiling not needed)
#ifndef ECS_ENABLE_SYSTEM_TIMING
#define ECS_ENABLE_SYSTEM_TIMING 1
#endif

namespace ecs {

/// Timing information for a single system
struct SystemTiming {
    const char* name;
    float durationMs;
};

} // namespace ecs

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
        return registry.createEntity();
    }

    /// Destroy an entity
    void destroyEntity(EntityID entity) {
        registry.destroyEntity(entity);
    }

    /// Check if entity is alive
    [[nodiscard]] bool isAlive(EntityID entity) const {
        return registry.isAlive(entity);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Component Management (delegated to Registry)
    // ─────────────────────────────────────────────────────────────────────────

    /// Add a component to an entity
    template <typename T, typename... Args>
    T& addComponent(EntityID entity, Args&&... args) {
        return registry.addComponent<T>(entity, std::forward<Args>(args)...);
    }

    /// Get a component from an entity
    template <typename T>
    [[nodiscard]] T* getComponent(EntityID entity) {
        return registry.getComponent<T>(entity);
    }

    /// Get a component from an entity (const)
    template <typename T>
    [[nodiscard]] const T* getComponent(EntityID entity) const {
        return registry.getComponent<T>(entity);
    }

    /// Check if entity has a component
    template <typename T>
    [[nodiscard]] bool hasComponent(EntityID entity) const {
        return registry.hasComponent<T>(entity);
    }

    /// Remove a component from an entity
    template <typename T>
    void removeComponent(EntityID entity) {
        registry.removeComponent<T>(entity);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // View (Query) System
    // ─────────────────────────────────────────────────────────────────────────

    /// Create a view to iterate entities with specific components
    template <typename... Components>
    [[nodiscard]] View<Components...> view() {
        return View<Components...>(registry);
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
        systemMap[typeIndex] = system.get();
        systems.push_back(std::move(system));
        sorted = false;

        return ref;
    }

    /// Get a registered system by type
    template <typename T>
    [[nodiscard]] T& getSystem() {
        auto it = systemMap.find(std::type_index(typeid(T)));
        assert(it != systemMap.end() && "System not registered");
        return *static_cast<T*>(it->second);
    }

    /// Get a registered system by type (const)
    template <typename T>
    [[nodiscard]] const T& getSystem() const {
        auto it = systemMap.find(std::type_index(typeid(T)));
        assert(it != systemMap.end() && "System not registered");
        return *static_cast<const T*>(it->second);
    }

    /// Update all systems in priority order
    void update(float deltaTime) {
        sortSystemsIfNeeded();

#if ECS_ENABLE_SYSTEM_TIMING
        systemTimings.clear();
        // Note: capacity is retained from previous frames, no allocation after first frame

        for (auto& system : systems) {
            auto start = std::chrono::high_resolution_clock::now();
            system->update(deltaTime);
            auto end = std::chrono::high_resolution_clock::now();

            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            systemTimings.push_back({
                system->name(),
                duration.count() / 1000.0F
            });
        }
#else
        for (auto& system : systems) {
            system->update(deltaTime);
        }
#endif
    }

    /// Get timing information from last update (for profiling)
    [[nodiscard]] const std::vector<SystemTiming>& getSystemTimings() const {
        return systemTimings;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Registry Access
    // ─────────────────────────────────────────────────────────────────────────

    /// Get direct access to the registry
    [[nodiscard]] Registry& getRegistry() {
        return registry;
    }

    /// Get direct access to the registry (const)
    [[nodiscard]] const Registry& getRegistry() const {
        return registry;
    }

private:
    void sortSystemsIfNeeded() {
        if (sorted) {
            return;
        }

        std::sort(systems.begin(), systems.end(),
                  [](const std::unique_ptr<ISystem>& a, const std::unique_ptr<ISystem>& b) {
                      return a->priority() < b->priority();
                  });

        sorted = true;
    }

    Registry registry;
    std::vector<std::unique_ptr<ISystem>> systems;
    std::unordered_map<std::type_index, ISystem*> systemMap;
    std::vector<SystemTiming> systemTimings;
    bool sorted = false;
};

}  // namespace ecs
